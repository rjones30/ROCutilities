
COMMAND="/gluonfs1/home/dalton/svn/daq_dev_vers/daq/vme/src/vmefa/test/maprates_onroc.csh"
SLEEPTIME="600"
echo $COMMAND
echo $SLEEPTIME

xterm -geometry 130x24 -e ssh rocbcal1  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh rocbcal2  "$COMMAND ; sleep $SLEEPTIME" &
#xterm -geometry 130x24 -e ssh rocbcal4  "$COMMAND ; sleep $SLEEPTIME" &
#xterm -geometry 130x24 -e ssh rocbcal5  "$COMMAND ; sleep $SLEEPTIME" &
#xterm -geometry 130x24 -e ssh rocbcal7  "$COMMAND ; sleep $SLEEPTIME" &
#xterm -geometry 130x24 -e ssh rocbcal8  "$COMMAND ; sleep $SLEEPTIME" &
#xterm -geometry 130x24 -e ssh rocbcal10  "$COMMAND ; sleep $SLEEPTIME" &
#xterm -geometry 130x24 -e ssh rocbcal11  "$COMMAND ; sleep $SLEEPTIME" &

