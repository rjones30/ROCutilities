
COMMAND="/gluonfs1/home/dalton/svn/daq_dev_vers/daq/vme/src/vmefa/test/tweakonroc.csh"
SLEEPTIME="3600"
echo $COMMAND
echo $SLEEPTIME

xterm -geometry 130x24 -e ssh hdops@rocbcal1  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocbcal2  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocbcal4  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocbcal5  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocbcal7  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocbcal8  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocbcal10  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocbcal11  "$COMMAND ; sleep $SLEEPTIME" &

xterm -geometry 130x24 -e ssh hdops@rocfcal1  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocfcal2  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocfcal3  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocfcal4  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocfcal5  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocfcal6  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocfcal7  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocfcal8  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocfcal9  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocfcal10  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocfcal11  "$COMMAND ; sleep $SLEEPTIME" &
xterm -geometry 130x24 -e ssh hdops@rocfcal12  "$COMMAND ; sleep $SLEEPTIME" &
