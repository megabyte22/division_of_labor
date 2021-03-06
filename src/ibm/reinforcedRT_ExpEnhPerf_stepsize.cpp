// C++ code for the evolutionary reinforced threshold model, 
//
// see Duarte (2012) Evolution of threshold reinforcement leading to division
// of labor. Chapter 4 of PhD Thesis: 
// Evolution of Self-organized Division of Labor in Social Insects 
// University of Groningen
// 
//
// based on the Fixed threshold model
// with stimulus update per ant, or per timestep (see define)
//
//
// TODO: 
// - multiple tasks
// - make sure want_task is indeed only one task 

//---------------------------------------------------------------------------
#include <ctime>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <string>
#include <cmath>
#include <cassert>
#include <vector>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <cstring>
#include <termios.h>
#include <omp.h>
#include <unistd.h>
#include <sys/stat.h>

//#define DEBUG
//#define SIMULTANEOUS_UPDATE
//#define STOPCODE
//#define WRITE_LASTGEN_PERSTEP
//---------------------------------------------------------------------------

using namespace std;

// random number generator 
// see http://www.gnu.org/software/gsl/manual/html_node/Random-Number-Generation.html#Random-Number-Generation 
gsl_rng_type const * T; // gnu scientific library rng type
gsl_rng *rng_global; // gnu scientific rng 


struct Params
{
    int N; //number of workers
    int Col; // number of colonies
    int maxtime; // time steps
    double p;  // quitting probability
    unsigned int tasks;
    double mutp;//mutation probability
    int maxgen;
    double beta_fit, gamma_fit; // exponents of the fitness function
    int seed;
    double recomb;
    int timecost;
    double mutstep;
    double threshold_noise; // noise when comparing thresholds to stimulus levels
    double initStim; // initial stimulus value
    double p_wait; // probability that ant has to wait c time steps before switching
    int tau; // time step from which fitness is counted
    double initForget; // initial Forget value
    double initLearn; // initial Learn
    double step_gain_exp; // stepsize when gaining experience points
    double step_lose_exp; // stepsize when losing experience points
    double K; // speed with which efficiency increases with experience

    vector<double> meanT; // mean threshold 
    vector<double> delta; // rate of increase in stimulus 
    vector<double> alpha_max; // maximum efficiency with which work is done (when fully experienced)
    vector<double>alpha_min; // minimum efficiency with which work is done (when inexperied)
    vector<double> beta;
    
    istream & Init_Params(istream & inp);



};

struct Ant
{
    // genome
    double learn;
    double forget;
    // behaviour
    vector < double > threshold;
    vector < double > alpha; // Strenght with which experience level affects efficiency
    vector < int > countacts;   // counter of acts done by this ant
    vector < bool > want_task; // whether an individual would accept an offered task (does not mean it will do the task)
    vector < double > experience_points; // e_ij in Duarte 2012 chapter 5
    int last_act; // keep track of the last act that an individual did
    int curr_act; // keep track of current act an individual is doing
    int switches; // number transitions to a different task
    int workperiods; // number of working periods 
    double D; // specialization value
    bool mated; // only for queens, keep track of who is already mated
    double Dx; // Franjo's specialization value
    int count_time; //COUNTER OF TIMESTEPS TO SWITCH TASK
    int ID_ant; // individual ID of an ant
};

// declare populations of Workers and Sexuals
typedef vector < Ant > Workers;
typedef vector < Ant > Sexuals;


// ok, define a colonoy
struct Colony
{
    Workers MyAnts; // ants in the colony
    Ant male, queen; // king & queen
    int ID; // id of the colony
    
    vector<double> stim; // the different stimuli for the various tasks in the colony 
    vector<double> newstim; // I don't know

    vector<double> workfor;  // number acts * eff each time step
    vector < int > numacts_step; // number of acts performed per task each time step
    vector < int > numacts_total; // number of total acts performed per task

    double idle; // proportion workers that _never_ worked in the simulation 
    double inactive; // proportion workers that were idle each time step

    vector <double> fitness_work; //number of acts * eff performed in the time steps counting for fitness 
    double fitness;
    double rel_fit; // fitness relative to whole population
    double cum_fit; //cumulative fitness

    // number of acts performed per task each time step
    // yet only counted in the interval that colony productivity is counted
    // i.e., maxtime - tau
    vector<double> mean_work_alloc; 

    double mean_D;
    double var_D;
    double mean_Dx;
    double var_Dx;
    int num_offspring;
    double mean_switches;
    double var_switches;
    double mean_workperiods;
    double var_workperiods;
};

// define a population of colonies
typedef vector < Colony > Population;

// Sexual individuals that are going to found a new colony
Sexuals mySexuals;

// keep track of the ID of the parental colony
vector <int> parentCol;

// some stats
double sum_fitness = 0;
int simstart_generation = 0;

// if one big simulation is broken up into several 'parts' (e.g., because it takes very long)
// denote the current part
int simpart; 


int mygetch(void)
{
    struct termios oldt, newt;
    int ch;
    tcgetattr( STDIN_FILENO, &oldt );
    newt = oldt;
    newt.c_lflag &= ~( ICANON | ECHO );
    tcsetattr( STDIN_FILENO, TCSANOW, &newt );
    ch = getchar();
    tcsetattr( STDIN_FILENO, TCSANOW, &oldt );
    return ch;
}

// initialize the parameters from a textfile file in the local folder
// which is all generated through python
istream & Params::Init_Params(istream & in)
{
    tasks = 2; 
    meanT.reserve(tasks);
    delta.reserve(tasks);
    alpha_max.reserve(tasks);
    alpha_min.reserve(tasks);
    beta.reserve(tasks);

    // read in parameter values from the input stream (file)
    string tmp; // temporary string value to hold value in parameter value

    // read in population size
    getline(in, tmp, ';'); 
    N = stoi(tmp);

    // skip the remainder after the semicolon in params.txt
    getline(in, tmp); 
    tmp="";

    // read in colony number 
    getline(in, tmp, ';'); 
    Col = stoi(tmp);
    
    // skip the remainder after the semicolon in params.txt
    getline(in, tmp); 
    tmp="";

    // read in maxtime
    getline(in, tmp, ';'); 
    maxtime = stoi(tmp);
    
    // skip the remainder after the semicolon
    getline(in, tmp); 
    tmp="";

    // get initial threshold values for each task
    for (unsigned int i = 0; i < tasks; ++i) 
    {
        getline(in, tmp, ';'); 
        meanT.push_back(stof(tmp));
    
        // skip the remainder
        getline(in, tmp); 
        tmp="";
    }

    // get increase in stimulus intensity for each task
    for (unsigned int i = 0; i < tasks; ++i) 
    {    
        getline(in, tmp, ';'); 
        delta.push_back(stof(tmp));
    
        // skip the remainder
        getline(in, tmp); 
        tmp="";
    }

    // get maximum efficiency of work for each task
    for (unsigned int i = 0; i < tasks; ++i)
    {
        getline(in, tmp, ';'); 
        alpha_max.push_back(stof(tmp));
    
        // skip the remainder
        getline(in, tmp); 
        tmp="";
    }

    // get minimum efficiency of work for each task
    for (unsigned int i = 0; i < tasks; ++i)
    {
        getline(in, tmp, ';'); 
        alpha_min.push_back(stof(tmp));
    
        // skip the remainder
        getline(in, tmp); 
        tmp="";
    }

    // get stimulus decay for each task
    for (unsigned int i = 0; i < tasks; ++i) 
    {
        getline(in, tmp, ';'); 
        beta.push_back(stof(tmp));
    
        // skip the remainder
        getline(in, tmp); 
        tmp="";
    }

    // obtain the quitting probability p
    getline(in, tmp, ';'); 
    p = stof(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";


    // obtain the mutation rate
    getline(in, tmp, ';'); 
    mutp = stof(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";

    // obtain the maximum number of generations
    getline(in, tmp, ';'); 
    maxgen = stoi(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";


    // obtain the exponent of the first task in the fitness function
    getline(in, tmp, ';'); 
    beta_fit = stof(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";


    // obtain the exponent of the second task in the fitness function
    getline(in, tmp, ';'); 
    gamma_fit = stof(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";
   

    // obtain the recombination rate
    getline(in, tmp, ';'); 
    recomb = stof(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";
   

    // obtain the timecost
    getline(in, tmp, ';'); 
    timecost = stoi(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";


    // obtain the mutational step size
    getline(in, tmp, ';'); 
    mutstep = stof(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";


    // obtain the initial level of the stimulus
    getline(in, tmp, ';'); 
    initStim = stof(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";


    // obtain the probability that ant has to 
    // wait c time steps before switching
    getline(in, tmp, ';'); 
    p_wait = stof(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";
    
    
    // obtain timestep from which fitness is counted
    getline(in, tmp, ';'); 
    tau = stoi(tmp);

    assert(tau < maxtime);

    // skip the remainder
    getline(in, tmp); 
    tmp="";
    
    
    getline(in, tmp, ';'); 
    initForget = stof(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";
    
    
    getline(in, tmp, ';'); 
    initLearn = stof(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";
    
    
    getline(in, tmp, ';'); 
    step_gain_exp = stof(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";
    
    
    getline(in, tmp, ';'); 
    step_lose_exp = stof(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";


    getline(in, tmp, ';'); 
    threshold_noise = stof(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";

    
    getline(in, tmp, ';'); 
    K = stof(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";

    
    getline(in, tmp, ';'); 
    seed = stoi(tmp);

    // skip the remainder
    getline(in, tmp); 
    tmp="";

    return in;
}

//=================================================================================================================
// Function to check if a file exists already
bool FileExists(string strFilename) 
{
  struct stat stFileInfo;
  bool blnReturn;
  int intStat;

  // Attempt to get the file attributes
  intStat = stat(strFilename.c_str(),&stFileInfo);
  if(intStat == 0) {
    // We were able to get the file attributes
    // so the file obviously exists.
    blnReturn = true;
  } else {
    // We were not able to get the file attributes.
    // This may mean that we don't have permission to
    // access the folder which contains this file. If you
    // need to do that level of checking, lookup the
    // return values of stat which will give you
    // more details on why stat failed.
    blnReturn = false;
  }
  
  return(blnReturn);
}


//===========================================================================================
//CopyFile is a simple function that copies a file from arg1 to arg2

int CopyFile(string initialFilePath, string outputFilePath)
{		
	ifstream initialFile(initialFilePath.c_str(), ios::in|ios::binary);	
	ofstream outputFile(outputFilePath.c_str(), ios::out|ios::binary);	
	//defines the size of the buffer	
	initialFile.seekg(0, ios::end);	
	long fileSize = initialFile.tellg();	
	//Requests the buffer of the predefined size	
	//As long as both the input and output files are open...	
	if(initialFile.is_open() && outputFile.is_open())	
	    {		
	    short * buffer = new short[fileSize];		
	    //Determine the file's size		
	    //Then starts from the beginning		
	    initialFile.seekg(0, ios::beg);		
	    //Then read enough of the file to fill the buffer		
	    initialFile.read((char*)buffer, fileSize);		
	    //And then write out all that was read		
	    outputFile.write((char*)buffer, fileSize);		
	    delete[] buffer;	
	    }	

	//If there were any problems with the copying process, let the user know	
	else if(!outputFile.is_open())	
	    {		
	    cout<<"I couldn't open "<<outputFilePath<<" for copying!\n";		
	    return 0;	
	    }	
	    else if(!initialFile.is_open())	
		{		
		cout<<"I couldn't open "<<initialFilePath<<" for copying!\n";		
		return 0;	
		}			

//	initialFile.close();	
	outputFile.close();	
	return 1;
} // end of CopyFile()
//=================================================================================================================
//=================================================================================================================
// Function that initializes founders from (previous) data 

void Read_LastGen_Data(istream & in, Params & Par, Population & Pop)
{
	in >> simpart 
	   >> simstart_generation; 

	for (unsigned int i = 0; i < Pop.size(); i++)
    {
        in >> Pop[i].male.learn
            >> Pop[i].male.forget
            >> Pop[i].queen.learn
            >> Pop[i].queen.forget;
    }
}

void Show_Colonies(Population &Pop)
{
    for (unsigned int col = 0; 
            col < Pop.size(); ++col)
	{
        cout << "colony: " << col
            << " fitness: " << Pop[col].fitness
            << " rel fitness: " << Pop[col].rel_fit
            << " cum fitness: " << Pop[col].cum_fit << endl;
    }
}

//============================================================================================

void Show_Ants(Colony & anyCol)
{
    cout << "=======================================" << endl;
    cout << "Current values of:" << endl; 
    cout << "\t" << endl;
    for (unsigned int ant=0; ant < anyCol.MyAnts.size(); ant++)
	{
	    cout << "ant " << ant << endl;
        cout << "\t" << endl;
	    cout << "count acts " << anyCol.MyAnts[ant].countacts[0] << "\t" << anyCol.MyAnts[ant].countacts[1]  << endl;
        cout << "thresholds " << anyCol.MyAnts[ant].threshold[0] << "\t" << anyCol.MyAnts[ant].threshold[1] << endl;
        cout << "effic " << anyCol.MyAnts[ant].alpha[0] << "\t" << anyCol.MyAnts[ant].alpha[1] << endl;
	    cout << "D " << anyCol.MyAnts[ant].D << endl; // specialization value
	    cout << "switches " << anyCol.MyAnts[ant].switches << endl;
	    cout << "workperiods " << anyCol.MyAnts[ant].workperiods << endl;
        cout << "\t" << endl;
	}
}

//--------------------------------------------------------------------------------
void Show_Params(Params & Par)
{
    cout << "Workers " << Par.N << endl;
    cout << "Colonies " << Par.Col << endl;
    cout << "Timesteps " << Par.maxtime << endl;

    for (unsigned int task=0; task<Par.tasks; ++task)
    {
        cout << "Initial T" <<task <<"\t" << Par.meanT[task] << endl;
    }

    for (unsigned int task=0; task<Par.tasks; ++task)
    {
        cout << "Delta " <<task <<"\t"  << Par.delta[task] << endl;
    }

    for (unsigned int task=0; task<Par.tasks; ++task)
    {
        cout << "max effic " << task << "\t" << Par.alpha_max[task] << endl;
    }

    for (unsigned int task=0; task<Par.tasks; ++task)
    {
        cout << "min effic " << task << "\t" << Par.alpha_min[task] << endl;
    }

    for (unsigned int task=0; task<Par.tasks; ++task)
    {
        cout << "Decay " << task << "\t" << Par.beta[task] << endl;
    }

    cout << "prob quit" << Par.p << endl;
    cout << "Task number " << Par.tasks << endl;
    cout << "mut prob " << Par.mutp << endl;
    cout << "Max gen " <<  Par.maxgen << endl;
    cout << "Exp task 1 " <<  Par.beta_fit << endl;
    cout << "Exp task 2 " <<  Par.gamma_fit << endl;
    cout << "seed " << Par.seed << endl;
    cout << "recombination " << Par.recomb << endl;
    cout << "timecost " << Par.timecost << endl;
    cout << "mutstep " << Par.mutstep << endl;
    cout << "tau " << Par.tau << endl;
    cout << "initial Stimulus " << Par.initStim << endl;
    cout << "p_wait " << Par.p_wait << endl;
    cout << "threshold noise " << Par.threshold_noise << endl;
    cout << "initial Learn " << Par.initLearn << endl;
    cout << "initial Forget " << Par.initForget << endl;
    cout << "stepsize gain exp " << Par.step_gain_exp << endl;
    cout << "stepsize to lose exp " << Par.step_lose_exp << endl;
    cout << "K " << Par.K << endl;
}
//========================================================================================
// Function to initialize founders at generation 0
void Init_Founders_Generation_0(Population &Pop, Params &Par)
{
#ifdef DEBUG  
    // some bits are taylored for two tasks so we have to throw this assert for now
    assert(Par.tasks==2);
    cout <<Par.Col << endl;
#endif
    Pop.resize(Par.Col);
    //cout << "Initializing founders!" << endl;
    for (unsigned int i = 0; i < Pop.size(); i++)
    {
        // now specify the sizes of the threshold vectors for the 
        // number of tasks which you define in the parameter file
        Pop[i].male.threshold.resize(Par.tasks);
        Pop[i].queen.threshold.resize(Par.tasks);

        // set the initial thresholds for each individual
        for (unsigned int task=0; task<Par.tasks; task++)
        {
            Pop[i].male.threshold[task]= Par.meanT[task];
            Pop[i].queen.threshold[task]= Par.meanT[task];
        }

        // set the initial learn and forget values for the colony
        Pop[i].male.learn = Par.initLearn;
        Pop[i].male.forget = Par.initForget;
        Pop[i].queen.learn = Par.initLearn;
        Pop[i].queen.forget = Par.initForget;

        Pop[i].male.mated =true;
        Pop[i].queen.mated = true;
    }
}
//----------------------------------------------------------------------------------------------------------------------
//=============================================================================
//end of Init_Founders_Generation_0()

void Mutation(double & trait, double & parent, Params &Par, gsl_rng *rng_r)
    {
        if (Par.mutp > gsl_rng_uniform(rng_r))
            trait = parent + gsl_ran_gaussian(rng_r, Par.mutstep);
        else trait = parent;
    }

//==============================================================================
//end of Mutation()
void Inherit(Ant &Daughter, Ant &Mom, Ant &Dad, Params &Par, gsl_rng *rng_r)
{
    double rec = gsl_rng_uniform(rng_r);

    // ok, full recombination 
    if (rec < Par.recomb)
    {
        // with probabability 0.5, mom transmits learn
        // dad transmits forget
        if (gsl_rng_uniform(rng_r) < 0.5)
        {
            Mutation(Daughter.learn, Mom.learn, Par, rng_r);
            Mutation(Daughter.forget, Dad.forget, Par, rng_r); 
        }
        else // with prob 0.5 vice versa
        {
            Mutation(Daughter.learn, Dad.learn, Par, rng_r);
            Mutation(Daughter.forget, Mom.forget, Par, rng_r);
        }
    } 
    else  // no recombination
    {
        if (gsl_rng_uniform(rng_r) < 0.5)
        {
            Mutation(Daughter.learn, Mom.learn, Par, rng_r);
            Mutation(Daughter.forget, Mom.forget, Par, rng_r);
        }
        else 
        {
            Mutation(Daughter.learn, Dad.learn, Par, rng_r);
            Mutation(Daughter.forget, Dad.forget, Par, rng_r);
        }
    }

    // values for learning and forgetting cannot be negative
    if (Daughter.learn < 0)
    {
        Daughter.learn = 0;
    }

    if (Daughter.forget < 0) 
    {
        Daughter.forget = 0;
    }

} // end of Inherit

//=======================================================================================

// update performance efficiency 
// performance efficiency for task i is alpha_i
// where
// alpha_i = alpha_max * alpha_min * exp(K * eij) / (alpha_min * exp(K * eij) + 1-alpha_min)
//
// see Otto & Day ch 4 for specication of sigmoidal
// K affects steepness of sigmoidal
void UpdateEfficiency(Ant & anyAnt, Params & Par)
{
    for (unsigned int task_i = 0; task_i < Par.tasks; ++task_i)
    {
        double tmp_1 = Par.K * anyAnt.experience_points[task_i];
        double tmp_2 = Par.alpha_min[task_i] * exp(tmp_1);

        anyAnt.alpha[task_i] = Par.alpha_max[task_i] * tmp_2 / 
            (tmp_2 + (1 - Par.alpha_min[task_i])); 
    }

}
// end UpdateEfficiency
//=======================================================================================================================
//


// now initialize an ant
void Init_Ants(Ant & myAnt, Params & Par, Colony & myCol, int numID, gsl_rng *rng_r)
{
    myAnt.ID_ant = numID;
    myAnt.threshold.resize(Par.tasks);
    myAnt.alpha.resize(Par.tasks);
    myAnt.experience_points.resize(Par.tasks);

    for (unsigned int task=0; task < Par.tasks; ++task)
    {
        myAnt.threshold[task]= Par.meanT[task];
        myAnt.experience_points[task] = 0;
    }

    if (Par.maxgen > 1)
    {
        Inherit(myAnt, myCol.queen, myCol.male, Par, rng_r);
    }
    else 
    {
        myAnt.learn= Par.initLearn;
        myAnt.forget = Par.initForget;
    }

    myAnt.countacts.resize(Par.tasks);
    myAnt.want_task.resize(Par.tasks);

    for (unsigned int task = 0; task < Par.tasks; ++task)
    {
        myAnt.countacts[task]=0;
        myAnt.want_task[task]=false;
    }

    myAnt.last_act = 7; // initiate it at an impossible value for a task, because 0 is a task

    // set current act to a value
    // beyond the actual tasks, indicating that the worker
    // is currently idle
    myAnt.curr_act = Par.tasks; 
    myAnt.switches = 0;
    myAnt.workperiods=0;
    myAnt.D = 10;
    myAnt.Dx = 10;
    myAnt.mated = false;
    myAnt.count_time=0;
    UpdateEfficiency(myAnt, Par); 
}


//------------------------------------------------------------------------------------

// initialize a colony from sexuals
void Init_Colony(Colony & Col, 
        unsigned int colony_number, 
        Params & Par,
        gsl_rng *rng_r)
{
    // resize the colony population to fit N individuals
    Col.MyAnts.resize(Par.N);

    // give colony particular id (for debugging purposes)
    Col.ID = colony_number;

    // set colony fitness to 0
    Col.fitness = 0;
    Col.rel_fit = 0;
    Col.cum_fit = 0;

    // set counter of idle workers to 0
    Col.idle = 0;
    Col.inactive = 0;

    // various specialization measurements
    Col.mean_D = 10; 
    Col.var_D=0;
    Col.mean_Dx = 10;
    Col.var_Dx = 0;
    Col.mean_switches = 0;
    Col.var_switches = 0;
    Col.mean_workperiods=0;
    Col.var_workperiods=0;

    // empty pools of workers, statistics, etc
    Col.workfor.erase(
            Col.workfor.begin(),
            Col.workfor.end());

    Col.fitness_work.erase(
            Col.fitness_work.begin(),
            Col.fitness_work.end());

    Col.numacts_step.erase(
            Col.numacts_step.begin(),
            Col.numacts_step.end());

    Col.numacts_total.erase(
            Col.numacts_total.begin(),
            Col.numacts_total.end());

    Col.stim.erase(
            Col.stim.begin(),
            Col.stim.end());

    Col.newstim.erase(
            Col.newstim.begin(),
            Col.newstim.end());

    Col.mean_work_alloc.erase(
            Col.mean_work_alloc.begin(),
            Col.mean_work_alloc.end());

    // allocate space in the various arrays
    Col.workfor.reserve(Par.tasks);
    Col.fitness_work.reserve(Par.tasks);
    Col.numacts_step.reserve(Par.tasks);
    Col.numacts_total.reserve(Par.tasks);
    Col.stim.reserve(Par.tasks);
    Col.newstim.reserve(Par.tasks);
    Col.mean_work_alloc.reserve(Par.tasks);

    // put an initial 0 in the vector
    for (unsigned int task_i = 0; task_i < Par.tasks; ++task_i)
    {
        Col.workfor.push_back(0);
        Col.fitness_work.push_back(0);
        Col.numacts_step.push_back(0);
        Col.numacts_total.push_back(0);
        Col.stim.push_back(Par.initStim);
        Col.newstim.push_back(0);
        Col.mean_work_alloc.push_back(0);
    }

    // go through all ants in the colony and initialize the
    // individual ants
    for (unsigned int ant_i = 0; 
            ant_i < Col.MyAnts.size(); 
            ++ant_i)
    {
        Init_Ants(Col.MyAnts[ant_i], Par, Col, ant_i, rng_r);
    }
} // end of Init()
//==================================================================================================================

// given that one ant has started working update the colony stimulus
// levels
void UpdateStimPerAnt(Params & Par, Colony & anyCol, Ant & anyAnt, int task)
{
#ifdef DEBUG
    cout << Par.N << endl;
    cout << anyCol.workfor[task] << endl;
    cout <<anyCol.numacts_step[task]<< endl;
    cout << anyCol.stim[task] << endl;
#endif
    
    // increment total amount of work being done in the colony
    // by adding an alpha_i * 1 (here 1 is the new worker who works on
    // task 'task'.
    anyCol.workfor[task] += anyAnt.alpha[task];    

    // update the stimulus accordingly (e.g., see eq. (3) in 
    // Bonabeau et al 1996
    anyCol.stim[task] -= (anyAnt.alpha[task]/Par.N); 
       
    // set boundary of the stimulus at 0
    if (anyCol.stim[task] < 0)
    {
        anyCol.stim[task] = 0;
    }
}

//====================================================================================================================
void UpdateThresholds_And_Experience (Ant & anyAnt, Params & Par)
{
    // update thresholds of all tasks
    for (unsigned int task_i = 0; task_i < Par.tasks; ++task_i)
    {
        // decrease thresholds and increase experience points
        // when this is the task the ant is currently working on
        if (anyAnt.curr_act == int(task_i))
        {
            anyAnt.threshold[task_i] -= anyAnt.learn;
            anyAnt.experience_points[task_i] += Par.step_gain_exp;
        }
        else
        {
            // for all other tasks (which the ant is not currently 
            // doing), increase thresholds and decrease experience points
            anyAnt.threshold[task_i] += anyAnt.forget;
            anyAnt.experience_points[task_i] -= Par.step_lose_exp;
        }

        // note that if ant is inactive she will increase her thresholds
        // and decrease experience points for all tasks

        
        // prevent thresholds and experience points from taking negative values
        if (anyAnt.threshold[task_i] < 0)
        {
            anyAnt.threshold[task_i] = 0;
        }

        if (anyAnt.experience_points[task_i] < 0)
        {
            anyAnt.experience_points[task_i] = 0;
        }
    }
}   
//========================================================================================================================

// record task switch when
// 1. ant is currently active
// 2. last act wasn't inactivity
// 3. last act was different from current act
void UpdateSwitches(Ant & anyAnt, Params & Par)
{
    if (anyAnt.curr_act < int(Par.tasks) && 
            anyAnt.last_act != 7 && 
            anyAnt.last_act != anyAnt.curr_act)
    {
        ++anyAnt.switches;
    }
}
//end UpdateSwitches
//=========================================================================================================================

// calculate whether ant is quitting a task
void QuitTask(Colony & anyCol, Ant & anyAnt, int job, Params & Par, gsl_rng *rng_r)
{
#ifdef DEBUG
    cout << "Quitting tasks" << endl;
    cout << "chance to quit: " << Par.p << endl;

#endif

    // draw random number to compare with quitting probability
    double q = gsl_rng_uniform(rng_r);

    // evaluate chance to quit
    if (q <= Par.p)
    {
        anyAnt.want_task[anyAnt.curr_act] = false;

        // set time worked to zero, 
        // she may choose the same or another task next
        anyAnt.count_time = 0;
        
        // set her current task to something beyond the current task options
        anyAnt.curr_act = Par.tasks;
    }
    else // ant does not quit
    {
        // only when stimulus levels are immediately updated
        // when ant starts to work
#ifndef SIMULTANEOUS_UPDATE  

        // update stimulus levels now that new ant has joined workforce
        UpdateStimPerAnt(Par, anyCol, anyAnt, job);
#endif
    }

}

//------------------------------------------------------------------------------
void DoTask(Params Par, Colony & anyCol, Ant & anyAnt,int job)
{

         anyAnt.curr_act = job; 
         anyAnt.workperiods +=1; 
         anyAnt.countacts[job] +=1;
        
#ifndef SIMULTANEOUS_UPDATE 
         UpdateStimPerAnt(Par, anyCol, anyAnt, job);
#endif
}
//end DoTask
//---------------------------------------------------------------------------------

// let ant evaluate threshold and see if she wants to perform a task
// several outcomes: ant may prefer one or multiple tasks. In the latter
// case, one of those tasks is selected as the preferred task
// she may also want to prefer no task yet
void WantTask (Params Par, 
        Colony & anyCol, 
        Ant & focalAnt,
        gsl_rng * rng_r
        )
{
    // make a list of all the task that this ants wants to do
    // and reserve space for it
    vector<int>wanted_task_ids;
    wanted_task_ids.reserve(Par.tasks); 

    // variable to store the focal ant's threshold value + noise for a task
    double t_noise;

    // loop through all tasks and calculate thresholds
    for (unsigned int task_i = 0; task_i < Par.tasks; ++task_i)
    {
        // calculate threshold + random noise
        t_noise = focalAnt.threshold[task_i] + gsl_ran_gaussian(rng_r, Par.threshold_noise);

        // threshold cannot be negative
        if (t_noise < 0)
        {
            t_noise = 0;
        }

        // ants want to work on tasks for which 
        // - their threshold exceeds the threshold + noise
        // - the stimulus level is nonzero (i.e., work needs to be done)
        if (anyCol.stim[task_i] >= t_noise && anyCol.stim[task_i] > 0) 
        {
            // store the wanted task
            wanted_task_ids.push_back(task_i);  
        } 
        else // ok ant does not want this task
        {
            // if ant's threshold not high enough, then quit with wanting task
            focalAnt.want_task[task_i] = false;
        }
    }

    // if more than one task is above threshold, select a random task
    // that ant wants to perform
    if (wanted_task_ids.size() > 1)
    {
        int job = gsl_rng_uniform_int(rng_r, wanted_task_ids.size());
        focalAnt.want_task[wanted_task_ids[job]] = true;
    }
    else if (!wanted_task_ids.empty())
    {
        assert(wanted_task_ids[0] >= 0);
        assert(wanted_task_ids[0] < int(Par.tasks));

        focalAnt.want_task[wanted_task_ids[0]] = true;
    }
}
//end WantTask
//-----------------------------------------------------------------------------

// evaluate whether ant should switch tasks
void EvalTaskSwitch(Params & Par, 
        Colony & anyCol, 
        Ant & anyAnt,
        int myjob, 
        gsl_rng *rng_r)
{
    // if it was doing this job previously 
    // or it did not do anything before
    // just perform the task
    if (myjob == anyAnt.last_act || anyAnt.last_act == 7) 
    {
        DoTask(Par, anyCol, anyAnt, myjob);
    }
    else  // ok, ant <is doing a different task than dei
    {
        // with a certain probability 
        if (Par.p_wait >= gsl_rng_uniform(rng_r) 
                && anyAnt.count_time < Par.timecost)    
        {
            //cout << "Ant wants to change task!" << endl;    
            anyAnt.curr_act=2; // stays idle for as long as count_time<timecost    
            ++anyAnt.count_time;
        }
        else
        {
            DoTask(Par, anyCol, anyAnt, myjob);
        }
    }
}
//end EvalTaskSwitch
//-------------------------------------------------------------------------------

// act of choosing a task
void TaskChoice(
        Params & Par, // parameter object
        Colony & anyCol, // current colony
        Ant & focalAnt,// the ant in question
        gsl_rng *rng_r) 
{ 
#ifdef DEBUG

    // debugging only: assert that ants do not want to do
    // multiple tasks at the same time
    bool wants_task = false;
    for (int task_i = 0; task_i < Par.tasks; ++task_i)
    {
        // ants wants to perform task i
        if (focalAnt.want_task[task_i])
        {
            // if it previously already did not already prefer a task
            if (!wants_task)
            {
                wants_task = true;
            }
            else
            {
                cout << "error: ant wants multiple tasks simultaneously";
                exit(1);
            }
        }
    }
#endif

     // find out if ant wants to perform a task
    for (unsigned int task_i = 0; task_i < Par.tasks; ++task_i)
    {
        // yes, ant wants to perform task so let's do it
        if (focalAnt.want_task[task_i])
        {
            EvalTaskSwitch(Par, anyCol, focalAnt, task_i, rng_r); 
            return;
        }
    }

    // ant does not want to perform a task

    // make ant want task
    WantTask(Par, anyCol, focalAnt, rng_r);

     // find out if ant now wants to perform a task
    for (unsigned int task_i = 0; task_i < Par.tasks; ++task_i)
    {
        // yes, ant wants to perform task so let's do it
        if (focalAnt.want_task[task_i])
        {
            EvalTaskSwitch(Par, anyCol, focalAnt, task_i, rng_r); 
            return;
        }
    }
} // end of TaskChoice()
//===============================================================================================

// if ant is working, see whether it might quit
// if ant is not working, see whether it might start a task
void Update_Ants(Colony & Col, Params & Par, gsl_rng *rng_r)
{
    // go through all tasks and reset their stats to 0
    for (unsigned int task_i = 0; task_i < Par.tasks; ++task_i)
    {
         Col.workfor[task_i] = 0; 
         Col.numacts_step[task_i] = 0;
    }

    // randomize order of ants 
    random_shuffle(Col.MyAnts.begin(), Col.MyAnts.end());
        
    // go through all ants and evaluate what they are doing/going to do
    for (unsigned int ant_i = 0; ant_i < Col.MyAnts.size(); ++ant_i)  
    {
        // check if ant is doing one of the tasks
        if (Col.MyAnts[ant_i].curr_act < int(Par.tasks))
        {
            // yes, ant is busy, hence record last act
            Col.MyAnts[ant_i].last_act = Col.MyAnts[ant_i].curr_act; 

            // evaluate whether ant will quit task
            QuitTask(Col, Col.MyAnts[ant_i], Col.MyAnts[ant_i].curr_act, Par, rng_r); 
        }

        // ant currently inactive, let it choose a task
        // (note that this can include an ant
        // who quit in the previous statement)
        if (Col.MyAnts[ant_i].curr_act >= int(Par.tasks))
        {
            TaskChoice(Par, Col, Col.MyAnts[ant_i], rng_r);
        }
            
        //update number of switches after choosing tasks
        UpdateSwitches(Col.MyAnts[ant_i], Par);

        // update the thresholds and experience levels
        UpdateThresholds_And_Experience(Col.MyAnts[ant_i], Par);

        // update the ant's efficiency
        UpdateEfficiency(Col.MyAnts[ant_i], Par);
    } // end for Col.MyAnts.
          
}  // end of Update_Ants()
//------------------------------------------------------------------------------

void Update_Col_Data(
        int step,  // current timestep
        Colony & Col, // the metapopulation
        Params & Par // the parameters
        )
{
    Col.inactive = 0;

    for (unsigned int ant_i = 0; ant_i < Col.MyAnts.size(); ++ant_i)
    {
        // check whether ant is active
        if (Col.MyAnts[ant_i].curr_act < int(Par.tasks))
        {
            // if active update act count
            Col.numacts_step[Col.MyAnts[ant_i].curr_act] += 1; 
        }
        else // ant inactive, count it
        {
            ++Col.inactive;
        }
        
        Col.inactive /= Col.MyAnts.size(); // proportion inactive workers 
    }
    
    // update counts of the total acts performed in the colony
    for (unsigned int task_i = 0; task_i < Par.tasks; ++task_i)
    {
        Col.numacts_total[task_i] += Col.numacts_step[task_i]; 
    
        // calculate fitness if within tau timesteps from the end
        if (step >= Par.tau) 
        {
            // add the number of workers to the fitness tally
            Col.fitness_work[task_i] += Col.workfor[task_i];
            Col.mean_work_alloc[task_i] += Col.numacts_step[task_i];
        }
    }

} // end of UpdateColony_data
//===================================================================================================
//
// update the stimulus levels
void Update_Stim(Colony &Col, Params & Par)   
{
    for (unsigned int task = 0; task < Par.tasks; ++task)
    {

#ifdef SIMULTANEOUS_UPDATE
        // update the stimulus for this task
        Col.newstim[task] = Col.stim[task] + Par.delta[task] - 
            (Par.beta[task]*Col.stim[task]) - (Col.workfor[task]/Par.N);
#endif

#ifndef SIMULTANEOUS_UPDATE
        Col.newstim[task] = Col.stim[task] + 
            Par.delta[task] - (Par.beta[task]*Col.stim[task]); 
#endif

        Col.stim[task] = Col.newstim[task];

        if (Col.stim[task] < 0)
        {
            Col.stim[task] = 0;
        }
    }
} // end Update_Stim()
//==============================================================================================

// calculate specialization value
void Calc_D(Colony & Col, Params & Par)
{
    Col.mean_D=0;
    Col.mean_Dx=0; 

    Col.mean_switches=0; 
    Col.mean_workperiods=0; 

    // calculate D = qbar / sum(p_i^2, i= 0, 1, 2, ... n_tasks) - 1
    // see eq. (5) in Duarte et al 2012 Behav Ecol Sociobiol
    // 66: 947-957, https://doi.org/10.1007/s00265-012-1343-2 
    
    // we need to calculate the proportion of acts for task i
    // as these are the p_i values in eq (5) of Duarte et al.
    double prop_work[Par.tasks];

    // with these p_i values we can then calculate the total
    // denominator of D
    double D_denominator = 0;

    // for this we need to ge the total amount of work
    double total_work = 0;

    // sum total work
    for (unsigned int task_i = 0; task_i < Par.tasks; ++task_i)
    {
        total_work += Col.numacts_total[task_i];
    }

    // then it is easy to calculate proportions
    for (unsigned int task_i = 0; task_i < Par.tasks; ++task_i)
    {
        prop_work[task_i] = Col.numacts_total[task_i] / total_work;

        // and calculate the total denominator
        D_denominator += prop_work[task_i] * prop_work[task_i];
    }

    double sumD=0; 
    double sumsquares_D=0;

    double sumDx =0; 
    double sumsquares_Dx =0;

    // we also want to calculate variances, which are given by 
    // Var[x] = E[x^2] - E[x]^2
    double sumsquares_switches=0; 
    double sumsquares_workperiods=0;

    double activ=0;
    
    // calculate the probability that an individual ant
    // switches between one timestep and the next
    double switch_prob;
   

    // go through all ants and calculate specialization stats
    for (unsigned int ant_i = 0; ant_i < Col.MyAnts.size(); ++ant_i)
    {
        assert(Col.MyAnts[ant_i].workperiods <= Par.maxtime);

        // set switching prob to 0
        switch_prob = 0;

        if (Col.MyAnts[ant_i].workperiods > 1)
        {
            Col.mean_switches += Col.MyAnts[ant_i].switches;
            Col.mean_workperiods += Col.MyAnts[ant_i].workperiods;

            // calculate sum of squares for workperiods
            sumsquares_workperiods += 
                Col.MyAnts[ant_i].workperiods * Col.MyAnts[ant_i].workperiods;

            sumsquares_switches += 
                Col.MyAnts[ant_i].switches * Col.MyAnts[ant_i].switches;

            // switching prob between one timestep and the next
            // is total number of switches divided by total possible
            // moments to switch (which is total number of workperiods - 1)
            // -1 as you cannot switch anymore during the last work period
            switch_prob = (double) Col.MyAnts[ant_i].switches / 
                (Col.MyAnts[ant_i].workperiods - 1.0);

            // D = qbar (see eq (5) in Duarte et al) is then given by
            // qbar = 1.0 - switch_prob

            // however, when we want to scale between -1 and 1,
            // we do:
            Col.MyAnts[ant_i].D = 1.0 - 2.0 * switch_prob;

            sumsquares_D += Col.MyAnts[ant_i].D * Col.MyAnts[ant_i].D;

            // or in case we want to correct for the fact that ants may
            // remain at the same task due to randomness, we have to divide
            // by D_denominator. We have to substract 1.0 to scale between
            // -1 and 1
            Col.MyAnts[ant_i].Dx = (1.0 - switch_prob) / D_denominator - 1.0;
            
            sumsquares_Dx += Col.MyAnts[ant_i].Dx * Col.MyAnts[ant_i].Dx;

            sumD += Col.MyAnts[ant_i].D;
            activ += 1.0;
            sumDx += Col.MyAnts[ant_i].Dx;
        }
    }

    Col.mean_switches /= activ;

    Col.mean_workperiods = Col.mean_workperiods/Col.MyAnts.size();
    
    Col.mean_D = sumD/activ;
    Col.mean_Dx = sumDx/activ;

    Col.var_switches = sumsquares_switches / activ 
        - Col.mean_switches * Col.mean_switches;

    Col.var_workperiods = sumsquares_workperiods / activ 
        - Col.mean_workperiods * Col.mean_workperiods;

    Col.var_D = sumsquares_D / activ - Col.mean_D * Col.mean_D;
    Col.var_Dx = sumsquares_Dx / activ - Col.mean_Dx * Col.mean_Dx;

} // end of Calc_D()
//=======================================================================================================================

// determine fitness
void Calc_Abs_Fitness(Colony & Col, Params & Par)
{
    Col.fitness = Col.fitness_work[0];
    Col.mean_work_alloc[0] /= Par.maxtime - Par.tau;

    for (unsigned int task_i = 1; task_i < Par.tasks; ++task_i)
    {
        // multiplicative fitness
        Col.fitness *= Col.fitness_work[task_i];
    
        // also average work allocation over all necessary timesteps
        Col.mean_work_alloc[task_i] /= Par.maxtime - Par.tau;
    }

    // see eq. 4.2 in Duarte 2012
    Col.fitness = sqrt(Col.fitness);
    
    Col.idle = 0;

    // calculate the number of idle ants 
    for (unsigned int j = 0; j < Col.MyAnts.size(); ++j)
    {
        if (Col.MyAnts[j].workperiods == 0)
        {
            Col.idle +=1;
        }
    }

}

// calculate relative fitness of all colonies
void Calc_Rel_Fitness(Population &Pop, Params &Par)
{
    // set the global sum_fitness variable to 0
    sum_fitness = 0;

    for (unsigned int col = 0; col < Pop.size(); ++col)
    {
        Pop[col].cum_fit = sum_fitness + Pop[col].fitness;

        sum_fitness = Pop[col].cum_fit;
    }

} 
//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------

// generate reproducing individuals
void Make_Sexuals(Population & Pop, Params & Par)
{
    mySexuals.resize(2 * Par.Col); // number of sexuals needed
    parentCol.resize(mySexuals.size());

    // make a list of doubles that contain all the random deviates
    // we need to sample in the cumulative fitness distribution
    // each random deviate stands for the sampling of one sexual individual
    // from the fitness distribution of all colonies
    vector < double > cumul_dist_samples(mySexuals.size(), 0);

    // fill the list of deviates
    for (unsigned int sample_i = 0; sample_i < mySexuals.size(); ++sample_i)
    {
        // store random values of the cumulative distribution in the array
        cumul_dist_samples[sample_i] = gsl_rng_uniform(rng_global) * sum_fitness;
    }

    // now sort the list of random deviates
    // from low to high.
    sort(cumul_dist_samples.begin(), cumul_dist_samples.end());

    unsigned int cumul_counter = 0;

    unsigned int new_sexual_ind = 0;

    // associate deviates with population
    for (unsigned int col_i = 0; col_i < Pop.size(); ++col_i)
    {
        // note that there 2*Ncol deviates to be found in the cumulative
        // distribution, meaning we'd hit each colony's cumulative fitness 
        // on average multiple times
        //
        // now see whether there are deviates lower than this value
        for (; cumul_counter < mySexuals.size(); ++cumul_counter) 
        {
//            cout << "colony: " << col_i 
//                << " cumul_sexual_counter: " << cumul_counter 
//                << " cumul fit: " << Pop[col_i].cum_fit 
//                << " sum fit: " << sum_fitness 
//                << " deviate: " << cumul_dist_samples[cumul_counter] << endl;

            // yes, deviate lower. make a new sexual individual from this colony
            if (cumul_dist_samples[cumul_counter] <= Pop[col_i].cum_fit)
            {
                assert(new_sexual_ind < mySexuals.size());

                // inherit loci from the colony's founders 
                Inherit(mySexuals[new_sexual_ind], 
                        Pop[parentCol[col_i]].queen, 
                        Pop[parentCol[col_i]].male, 
                        Par,
                        rng_global);

                ++new_sexual_ind;
            }
            else// deviate too large? Move on to the next element in cumul dist
            {
                break;
            }
        } // end for cumul_counter
    } // end for col_i

    assert(new_sexual_ind == mySexuals.size());

} // end of MakeSexuals
//-------------------------------------------------------------------------------------------
void Make_Colonies(Population &Pop)
{
    int mother, father;

    for (unsigned int col = 0; col < Pop.size(); ++col)
    {
        assert(mySexuals.size() >= 2);

        // sample random mother
        mother = gsl_rng_uniform_int(rng_global, mySexuals.size());

        // make this mother the queen of Colony col
        Pop[col].queen = mySexuals[mother];

        // remove this individual from stack of sexuals
        mySexuals.erase(mySexuals.begin() + mother);

        // sample random father
        father = gsl_rng_uniform_int(rng_global, mySexuals.size());

        // make this father the male of Colony col
        Pop[col].male = mySexuals[father];

        // remove break
        mySexuals.erase(mySexuals.begin() + father);

    } 
} // end Make_Colonies()
//-----------------------------------------------------------------------------------------------------
    
// write out the genetic values of all queens and males of the last
// generation. This is done in a file lastgen.txt which can be used
// to restart the simulation at the same point where we left off
void Write_Last_Generation(
        Population &Pop, 
        int generation,
        Params & Par
        ) 
{
    // open output file
    static ofstream last_gen_stream;

    // only plot the last generation once every 10 generations
    // or at the last generation of the simulation
    if (generation % 10 == 0 ||
            generation - simstart_generation == Par.maxgen - 1)
    {
        last_gen_stream.open("lastgen.txt");

        for (unsigned int col_i = 0; col_i < Pop.size(); ++col_i)
        {
            if (col_i == 0)
            {
                // simpart global variable denoting which part of the total
                // simulation we are currently running
                last_gen_stream << simpart + 1 << endl
                        << generation + 1 << endl;
            }

            last_gen_stream << Pop[col_i].male.learn << endl
                     << Pop[col_i].male.forget << endl
                     << Pop[col_i].queen.learn << endl
                     << Pop[col_i].queen.forget << endl;
        }

        last_gen_stream.close();
    }
} // end of WriteLastGen
//========================================================================================================
//
// give names to each of the datafiles
void Name_Data_Files(
        string &data1, 
        string &data2, 
        string &data3, 
        string &data4, 
        string &data5, 
        string &data6, 
        string &dataant
        )
{
    stringstream tmp;
    tmp << "data_work_alloc_" << simpart << ".txt";
    data1 = tmp.str();

    stringstream tmp2;
    tmp2 << "allele_distrib_" << simpart << ".txt";
    data2 = tmp2.str();

    stringstream tmp3;
    tmp3 << "f_dist_" << simpart << ".txt";
    data3 = tmp3.str();

    stringstream tmp4;
    tmp4 << "threshold.txt";
    data4 = tmp4.str();

    stringstream tmp5;
    tmp5 << "data_1gen_" << simpart << ".txt";
    data5 = tmp5.str();

    stringstream tmp6;
    tmp6 << "thresholds_" << simpart << ".txt";
    data6 = tmp6.str();

    stringstream tmp7;
    tmp7 << "ant_beh_" << simpart << ".txt";
    dataant = tmp7.str();
}
//=====================================================================================================

// is this a continuation of a previous run, yes or no?
// if yes, read in the last generation of the previous run and start from there
// if no, just initialize everything
void Continue_Previous_Run_Yes_No(Params & Par, Population & Pop) 
{
    // if a lastgen.txt file is present in the current directory
    // this means it is a continuation of an older run 
	if (FileExists("lastgen.txt"))
    {
        ifstream inp("lastgen.txt");
        Read_LastGen_Data(inp, Par, Pop);
    }
	else 
    {
        simpart = 1; 
        simstart_generation = 0; 
    }
}
//=====================================================================================================

//-------------------------------------------------------------------------------------------------------

// write out data of a single colony
void Write_Col_Data(
        Colony & Col,
        ofstream & mydata,
        Params & Par,
        int gen,
        int colony)
{
	mydata << gen << ";" << colony;

	for (unsigned int task = 0; task < Par.tasks; ++task)
    {
	    mydata << ";" << Col.fitness_work[task] 
	            << ";" << Col.mean_work_alloc[task];
    }

    mydata << ";" << Col.idle 
            << ";" << Col.inactive 
            << ";" << Col.fitness 
            << ";" << Col.stim[0] 
            << ";" << Col.stim[1] 
            << ";" << Col.mean_switches 
            << ";" << Col.mean_workperiods << endl;  
}
//------------------------------------------------------------------------------------------------------
// write out all the alleles to get an overview of
// the amount of within and between colony genetic variation
void Write_Alleles_Spec(
        Colony & Col, 
        ofstream & data_reinforcement,
        ofstream & data_f,
        Params & Par,
        int gen)
{
    data_reinforcement << gen << ";";
    data_reinforcement << Col.male.learn << ";"; 
    data_reinforcement << Col.male.forget << endl; 

	data_reinforcement << gen <<";";
    data_reinforcement << Col.queen.learn << ";"; 
    data_reinforcement << Col.queen.forget << endl; 

	data_f << gen <<";" 
                << Col.mean_Dx << ";" 
                << Col.mean_switches << ";" 
                << Col.mean_workperiods << ";"
                << Col.var_Dx << ";"
                << Col.var_switches <<";" 
                << Col.var_workperiods << endl;
} 


//==============================================================================================================================================

// add headers to the data files
void Header_data(ofstream & header, ofstream & header2)
{
	    header << "Gen" << ";" 
		<< "Col"  << ";" 
		<< "FitWork1" << ";" 
		<< "FitWork2" << ";" 
		<< "WorkAlloc1" << ";" 
		<< "WorkAlloc2" <<";" 
		<< "Idle"<< ";" 
		<< "Inactive" << ";"
		<<"Fitness" << ";" 
		<< "End_stim1" << ";" 
		<< "End_stim2" << ";"
		<< "mean_switches" << ";"
		<< "mean_workperiods" << endl; 

#ifdef WRITE_LASTGEN_PERSTEP
        header2 << "Time" << ";" 
		<< "Col" << ";" 
		<< "Stim1" << ";" 
		<< "Stim2" << ";" 
		<< "Workers1" << ";"
		<< "Workers2" << ";" 
		<< "Fitness" << ";" 
		<< "Mean_Dx" << endl;
#endif   
}
//=======================================================================================================================
//
//
//writing data of last generation step by step
void Write_Data_1Gen(ofstream & mydata, 
        Colony & Col, 
        unsigned int colony_number,
        Params & Par, 
        int timestep)
{
    // print timestep and colony number
    mydata << timestep << ";" << colony_number << ";"; 

    // plot the perceived stimulus levels per task
    for (int task = 0; task < Par.tasks; ++task) 
    {
        mydata << Col.stim[task] << ";"; 
    }

    // plot the number of acts for each task
    for (unsigned int task = 0; task < Par.tasks; ++task) 
    {
        mydata << Col.numacts_step[task] << ";";
    }

    // write down colony fitness and specialization values
    mydata << Col.fitness << ";" 
        << Col.mean_Dx << endl; 
}
//==========================================================================================================================

// write down individual ants
void Write_Ants_Beh(Colony & Col, 
        unsigned int colony_number,
        unsigned int time_step,
        unsigned int generation,
        ofstream & mydata,
        Params &Par) 
{
    vector <double> meanth(Par.tasks,0);
    vector <double> meancountact(Par.tasks,0);
    vector <double> meanexperiencepoint(Par.tasks,0);
    vector <double> meanalpha(Par.tasks,0);

    double meanswitches = 0;
    double meanworkperiods = 0;

    vector <double> ssth(Par.tasks,0);
    vector <double> sscountact(Par.tasks,0);
    vector <double> ssexperiencepoint(Par.tasks,0);
    vector <double> ssalpha(Par.tasks,0);

    double ssswitches = 0;
    double ssworkperiods = 0;

    for (unsigned int ant = 0; ant < Col.MyAnts.size(); ++ant)
    {
        for (unsigned int task_i = 0; task_i < Par.tasks; ++task_i)
        {
            meanth[task_i] += Col.MyAnts[ant].threshold[task_i];
            
            ssth[task_i] += Col.MyAnts[ant].threshold[task_i]
                * Col.MyAnts[ant].threshold[task_i];



            meancountact[task_i] += Col.MyAnts[ant].countacts[task_i];

            sscountact[task_i] += Col.MyAnts[ant].countacts[task_i]
                * Col.MyAnts[ant].countacts[task_i];


            meanexperiencepoint[task_i] += 
                Col.MyAnts[ant].experience_points[task_i];

            ssexperiencepoint[task_i] += 
                Col.MyAnts[ant].experience_points[task_i]
                *
                Col.MyAnts[ant].experience_points[task_i];



            meanalpha[task_i] += Col.MyAnts[ant].alpha[task_i];

            ssalpha[task_i] += Col.MyAnts[ant].alpha[task_i] * 
                Col.MyAnts[ant].alpha[task_i];
        }

        meanswitches += Col.MyAnts[ant].switches;
        ssswitches += Col.MyAnts[ant].switches * 
            Col.MyAnts[ant].switches;

        meanworkperiods += Col.MyAnts[ant].workperiods;
        ssworkperiods += Col.MyAnts[ant].workperiods * 
            Col.MyAnts[ant].workperiods;
    }

    meanswitches /= Par.N;
    meanworkperiods /= Par.N;
    ssswitches /= Par.N;
    ssworkperiods /= Par.N;

    if (colony_number == 0 && time_step == 0 && generation == 0)
    {
        mydata << "generation;time;col_id;"
            << "meanswitches;meanworkperiods;"
            << "sdswitches;sdworkperiods;";

        for (unsigned int task_i = 0; task_i < Par.tasks; ++task_i)
        {
            mydata << "meanthreshold" << (task_i + 1) << ";";
            mydata << "meancountact" << (task_i + 1) << ";";
            mydata << "meanexperiencepoints" << (task_i + 1) << ";";
            mydata << "meanalpha" << (task_i + 1) << ";";
            mydata << "sdthreshold" << (task_i + 1) << ";";
            mydata << "sdcountact" << (task_i + 1) << ";";
            mydata << "sdexperiencepoints" << (task_i + 1) << ";";
            mydata << "sdalpha" << (task_i + 1) << ";";
            mydata << "stim" << (task_i + 1) << ";";
        }

        mydata << endl;
    }

    mydata << generation << ";" 
        << time_step << ";"
        << colony_number << ";"
        << meanswitches << ";"
        << meanworkperiods << ";"
        << sqrt(ssswitches - meanswitches * meanswitches) << ";"
        << sqrt(ssworkperiods - meanworkperiods * meanworkperiods) << ";";

    for (unsigned int task_i = 0; task_i < Par.tasks; ++task_i)
    {
        meanth[task_i] /= Par.N;

        meancountact[task_i] /= Par.N;
        meanexperiencepoint[task_i] /= Par.N;
        meanalpha[task_i] /= Par.N;

        ssth[task_i] /= Par.N;
        sscountact[task_i] /= Par.N;
        ssexperiencepoint[task_i] /= Par.N;
        ssalpha[task_i] /= Par.N;

        mydata 
            << meanth[task_i] << ";"
            << meancountact[task_i] << ";"
            << meanexperiencepoint[task_i] << ";"
            << meanalpha[task_i] << ";"
            << sqrt(ssth[task_i] - meanth[task_i] * meanth[task_i]) << ";"
            << sqrt(sscountact[task_i] - 
            meancountact[task_i] * meancountact[task_i]) << ";"
            << sqrt(ssexperiencepoint[task_i] - 
            meanexperiencepoint[task_i] * meanexperiencepoint[task_i]) << ";"
            << sqrt(ssalpha[task_i] - 
            meanalpha[task_i] * meanalpha[task_i]) << ";"
            << Col.stim[task_i] << ";";
    }

    mydata << endl;
}
//=========================================================================================================
// writing ants' thresholds 
void Write_Ants_Thresholds(Population & Pop, ofstream & mydata, int timestep, int gen) 
{
    for (unsigned int col = 0; col < Pop.size(); ++col)
    {
        for (unsigned int ant = 0; ant < Pop[col].MyAnts.size(); ++ant)
        {
            mydata << gen << ";" << timestep << ";" << col << ";" << Pop[col].MyAnts[ant].ID_ant << ";" 
                << Pop[col].MyAnts[ant].threshold[0] << ";" 
                << Pop[col].MyAnts[ant].threshold[1] << ";" 
                << Pop[col].MyAnts[ant].countacts[0] << ";"
                << Pop[col].MyAnts[ant].countacts[1] << ";"
                << Pop[col].MyAnts[ant].experience_points[0] << ";"
                << Pop[col].MyAnts[ant].experience_points[1] << ";"
                << Pop[col].MyAnts[ant].alpha[0] << ";"
                << Pop[col].MyAnts[ant].alpha[1] << endl; 
        }
    }
}
//================================================================================
int main(int argc, char* argv[])
{
    // initialize object to store all parameters
    Params myPars;
    
    // get parameters from file
    ifstream inp("params.txt");

    // add these parameters to parameter object
    myPars.Init_Params(inp);

    int skip_threshold = myPars.maxgen / 1000;

    // set up the random number generators
    // (from the gnu gsl library)
    gsl_rng_env_setup();
    T = gsl_rng_default;
    rng_global = gsl_rng_alloc(T);
    gsl_rng_set(rng_global, myPars.seed);

    // also set the seed for one call to random_shuffle
    srand(myPars.seed);


    // initialize the founders of all the colonies
    Population MyColonies;
    Init_Founders_Generation_0(MyColonies, myPars);

    // this simulation run might a a continuation of a previous 
    // simulation, for example when that simulation was broken off
    // prematurely. This function checks whether lastgen.txt (the output 
    // of the previous simulation is present and initializes the simulation
    // accordingly
    Continue_Previous_Run_Yes_No(myPars, MyColonies);

    // all the files to which data is written to
    string datafile1, 
           datafile2, 
           datafile3, 
           datafile4, 
           datafile5, 
           datafile6, 
           dataants;

    // function to give the datafiles particular names
    Name_Data_Files(
            datafile1, 
            datafile2, 
            datafile3, 
            datafile4, 
            datafile5, 
            datafile6, 
            dataants
            );

    // the corresponding output files
    static ofstream out1; 
    static ofstream out2;
    static ofstream out3;
    static ofstream out4;
    static ofstream out5;
    static ofstream out6;
    static ofstream header1;
    static ofstream header2;
    static ofstream out_ants;

    out1.open(datafile1.c_str());

    header1.open("header_1.txt");

#ifdef WRITE_LASTGEN_PERSTEP 
    header2.open("header2.txt");
#endif

    // write headers to datafiles
    Header_data(header1, header2);
    
    // data for the allelic distribution
    out2.open(datafile2.c_str());

    // add data headers
    out2 << "generation;learn;forget" << endl;

    out3.open(datafile3.c_str());    
    
    
    out4.open(datafile4.c_str());    

#ifdef WRITE_LASTGEN_PERSTEP 
    out_ants.open(dataants.c_str()); 
    out5.open(datafile5.c_str());
    out6.open(datafile6.c_str());
#endif

    // calculate maximum number of generations
    int maxgen = simstart_generation + myPars.maxgen;

    // now go evolve
    for (int current_generation = simstart_generation; 
            current_generation < maxgen; ++current_generation)
    {
        cout << "generation: " << current_generation << " end " << endl;

        double start_time = omp_get_wtime();

        // now go through all colonies and let them do work
        // for myPars.maxtime timesteps
# pragma omp parallel num_threads(5)
        {
# pragma omp for

            for (unsigned int col_i = 0; col_i < myPars.Col; ++col_i)
            {
                // make a local copy of the colony to prevent 
                // false sharing of MyColonies among threads
                Colony Current_Colony = MyColonies[col_i];

                // local parameter object
                Params localParams = myPars;

                // make a local random number generator
                gsl_rng *rng_local = gsl_rng_alloc(T);
    
                gsl_rng_set(rng_local, localParams.seed);

                // initialize each colony from sexuals
                Init_Colony(Current_Colony, col_i, localParams, rng_local);

                // timesteps during colony development
                for (int k = 0; k < localParams.maxtime; ++k)
                {
                    // update all the stimuli of the ants 
                    // and what they are doing
                    Update_Ants(Current_Colony, localParams, rng_local);

                    // calculate specialization values
                    Calc_D(Current_Colony, localParams); 

                    // update statistics and if beyond tau, fitness values
                    Update_Col_Data(k, Current_Colony, localParams);	

                    // calculate at the end of the timestep: 
                    // the ants have done something
                    // which has consequences for stimulus levels, 
                    // which you update here
                    Update_Stim(Current_Colony, localParams);

#ifdef WRITE_LASTGEN_PERSTEP 
                    Write_Ants_Beh(
                            Current_Colony,
                            col_i,
                            k,
                            current_generation,
                            out_ants,
                            localParams);
#endif
                }

                // calculate absolute fitness of this population
                // in the last timestep
                Calc_Abs_Fitness(Current_Colony, localParams);
                


                // return the current colony to the stack
                MyColonies[col_i] = Current_Colony;

            }
        }

        double stop_time = omp_get_wtime();

        cout << "time: " << (stop_time - start_time) << endl;
        
        start_time = omp_get_wtime();

        // calculate relative fitness values
        Calc_Rel_Fitness(MyColonies, myPars);
        
        stop_time = omp_get_wtime();
        cout << "time produce sexuals: " << (stop_time - start_time) << endl;

        // now calculate relative fitness 
        // write stats and let colonies reproduce
        //
        // we cannot use any multicore processing here,
        // as relative fitnes needs to be calculated wrt colony order
        for (unsigned int col_i = 0; col_i < MyColonies.size(); ++col_i)
        {
            // write out the data 
            Write_Col_Data(MyColonies[col_i], 
                    out1, 
                    myPars, 
                    current_generation, 
                    col_i);

            // write out alleles
            Write_Alleles_Spec(
                    MyColonies[col_i], 
                    out2, 
                    out3, 
                    myPars, 
                    current_generation);

#ifdef WRITE_LASTGEN_PERSTEP
            //do you want to write out the last generation step by step?
            if (current_generation == simstart_generation + myPars.maxgen-1) 
            {
                Write_Data_1Gen(out5, 
                        MyColonies[col_i], 
                        col_i, 
                        myPars, 
                        myPars.maxtime);
            }
#endif

        }
      
//        // write threshold data once every while
//        if (current_generation % skip_threshold == 0)
//        {
//            Write_Ants_Thresholds(
//                    MyColonies,
//                    out4,
//                    0,
//                    current_generation);
//        }



        Write_Last_Generation(
                MyColonies, 
                current_generation, 
                myPars);

        if (current_generation < myPars.maxgen - 1)
        {
            Make_Sexuals(MyColonies, myPars);
            
            Make_Colonies(MyColonies);
        }
        
    } // end for generations
}
