
COMMAND="/gluonfs1/home/dalton/svn/daq_dev_vers/daq/vme/src/vmefa/test/tweakonroc.csh"
#COMMAND="echo $HOST"
SLEEPTIME="3600"
echo $COMMAND
echo $SLEEPTIME

xterm -geometry 130x24 -e ssh hdops@rocbcal1  "$COMMAND ; sleep $SLEEPTIME" &
