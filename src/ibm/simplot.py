#!/usr/bin/env python3

# plot the division of labor simulations

import pandas as pd
import itertools
import numpy as np
import sys
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import axes3d
from matplotlib import rcParams
import matplotlib.patches as mpatches
import matplotlib.gridspec as gridspec
from matplotlib.ticker import AutoMinorLocator
from matplotlib import cm

plt.style.use('base')

rcParams['axes.labelsize'] = 15
rcParams['text.usetex'] = True
rcParams['font.family'] = 'sans-serif'

# see http://stackoverflow.com/questions/2537868/sans-serif-math-with-latex-in-matplotlib 
rcParams['text.latex.preamble'] = [
       r'\usepackage{siunitx}',   # i need upright \micro symbols, but you need...
       r'\sisetup{detect-all}',   # ...this to force siunitx to actually use your fonts
       r'\usepackage{helvet}',    # set the normal font here
       r'\usepackage{sansmath}',  # load up the sansmath so that math -> helvet
       r'\sansmath'               # <- tricky! -- gotta actually tell tex to use!
]

# get branch data
branch_data = pd.read_csv(
        filepath_or_buffer=sys.argv[1], 
        sep=";", 
        header=None, # no header
        names=["generation","learn","forget"] # hence provide header names
        )

# generate the pivot table that is necessary to plot it in imshow()
def generate_pivot(the_data, generation_col_name, x, yvars):

    # ok get the unique number of generations
    generations = the_data[generation_col_name].unique().sort_values()

    for yvar_i in yvars:

        # get min and max of data
        min(the_d

        bins = seq(

        # make a histogram for each generation and store it
        the_data[the_data



    the_pivot = the_data.pivot_table(
            values=z, 
            index=y, 
            columns=x)

    x, y = np.meshgrid(
            the_pivot.columns.values, 
            the_pivot.index.values)

    z = the_pivot.values

    return(x, y, z)



# initialize the figure
fig = plt.figure(figsize=(14,5))

widths = [ 1, 1, 1, 0.05 ]
heights = [ 1, 0.1]

gs = gridspec.GridSpec(
        nrows=len(heights),
        ncols=len(widths),
        width_ratios=widths,
        height_ratios=heights)

ax = plt.subplot(gs[0,0])

ax.imshow

(xqS, yqS, zqS) = generate_pivot(data, x="generation", y="bin", z="qS")