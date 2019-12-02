#!/bin/env python
#
# ratevsthreshold_collect.py - script to collect the many output files
#                              produced by ratevsthreshold_allchan into
#                              a single output stream written to stdout.
#
# author: richard.t.jones at uconn.edu
# version: september 30, 2018

import sys

def usage():
   print "Usage: ./ratevsthreshold_collect <suffix> [<suffix2> ... ]"
   print "   where <suffix> is the 5-character string that is found"
   print "   in the output filenames from ratevsthreshold_allchan,"
   print "   as in 'DSC_ratevthresh_roctagm2_<suffix>_s05c09.txt'"
   sys.exit(1)

if len(sys.argv) < 2:
   usage()
else:
   prefix = "DSC_ratevthresh_roctagm2_"

rates = {}
for suffix in sys.argv[1:]:
   for slot in range(4,12):
     for chan in range(0,16):
        ext = "_s" + str(slot).zfill(2) + "c" + str(chan).zfill(2) + ".txt"
        for line in open("DSC_ratevthresh_roctagm2_" + suffix + ext):
           fields = line.split()
           thresh = int(fields[0])
           count = float(fields[1])
           if not thresh in rates:
              rates[thresh] = {}
           if not slot in rates[thresh]:
              rates[thresh][slot] = {}
           rates[thresh][slot][chan] = count
  
for thresh in sorted(rates.iterkeys()):
   print "threshold", thresh
   for slot in sorted(rates[thresh].iterkeys()):
      print "slot", str(slot) + ': ',
      for chan in range(0,16):
         print rates[thresh][slot][chan],
      print
