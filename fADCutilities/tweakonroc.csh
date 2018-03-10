####!/bin/tcsh


setenv LINUXVME_INC /home/dalton/git/include
setenv LINUXVME_LIB /home/dalton/git/lib
setenv LD_LIBRARY_PATH ${LINUXVME_LIB}:${LD_LIBRARY_PATH}

#export LINUXVME_INC /home/dalton/git/include
#export LINUXVME_LIB /home/dalton/git/lib
#export LD_LIBRARY_PATH ${LINUXVME_LIB}:${LD_LIBRARY_PATH}
#echo $HOSTNAME
#echo $HOST
#setenvHOST=`hostname`
echo $HOST

cd /gluonfs1/gluex/CALIB/ALL/fadc250/default/autocal_bcal_fcal/
/gluonfs1/home/dalton/svn/daq_dev_vers/daq/vme/src/vmefa/test/faTweakPedestals 100
cd /gluonfs1/gluex/CALIB/ALL/fadc250/default/

