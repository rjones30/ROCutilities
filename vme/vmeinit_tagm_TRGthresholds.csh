#!/bin/tcsh
#
# vmeinit_tagm_TRGthresholds.sh - script to be run each time the ROC restarts
#                                 on roctagm2, to initialize the channel-dependent
#                                 TRIG thresholds in the TAGM vme discriminators.
#
# author: richard.t.jones at uconn.edu
# version: december 3, 2018

set threshold_file=/gluex/CALIB/ALL/dsc/spring-2018/roctagm2_dsc_fall_2018_v1.cnf
set homedir=/home/jonesrt/online/ROCutilities

source $homedir/vme/setup.csh

$homedir/dscTDCutilities/vmeDSCSetThresholds 2 $threshold_file
