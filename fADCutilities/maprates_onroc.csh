#!/bin/tcsh

#HOST=hostname

setenv LINUXVME_INC /home/dalton/git/include
setenv LINUXVME_LIB /home/dalton/git/lib
setenv LD_LIBRARY_PATH ${LINUXVME_LIB}:${LD_LIBRARY_PATH}


cd /gluonfs1/home/dalton/svn/daq_dev_vers/daq/vme/src/epics/data/$HOST
/gluonfs1/home/dalton/svn/daq_dev_vers/daq/vme/src/vmefa/test/faMapRates 95 105

