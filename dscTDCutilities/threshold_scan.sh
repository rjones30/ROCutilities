#!/bin/bash
#
# threshold_scan.sh - script to do a series of ratevsthreshold_allchan runs
#                     with an increasing threshold step from zero to 1000mV.
#
# author: richard.t.jones at uconn.edu
# version: october 1, 2018

cd threshold_scans
ls *.txt >/dev/null 2>&1
if [[ $? = 0 ]]; then
    echo "Leftover scan files from a previous run found in threshold_scan,"
    echo "please clean up old results before starting a new scan."
    exit 1
fi

# do an initial scan to get rid of hysteresis
../ratevsthreshold_allchan 10 20 10

t=0
dt=1
while [[ $t -lt 1000 ]]; do
    tend=`expr $t + \( $dt \* 9 \)`
    ../ratevsthreshold_allchan $t $tend $dt
    for fn in `ls *_[0-9][0-9]h[0-9][0-9]_s[0-9][0-9]c[0-9][0-9].txt`; do
        suffix=`echo $fn | awk -F_ '{print $4}'`
        newname=`echo $fn | sed s/$suffix/tt$t/`
        mv $fn $newname
    done
    t=`expr $dt \* 10 + $t`
    dt=`expr $dt \* 2`
done

python ../ratevsthreshold_collect.py `ls *_s04c00.txt | awk -F_ '{print $4}'` | tee threshold_scan.log
