#!/bin/bash
#
# seq.sh - script to cycle through a row-by-row light pulse scan
#          of the TAGM rates using the tdc discriminators.
#
# Author: richard.t.jones at uconn.edu
# Version: december 7, 2019
#
# Usage: [on roctagm2 in dscTDCutilities] ./seq.sh
#        [on gluon48 in TAGMutilties] ./seq.sh
#
# These two seq.sh scripts are made to work together. The user switches
# between the two windows, pressing ENTER at the prompt for each to
# advance through the steps of the scan.

for row in 1 2 3 4 5; do
	for gval in 25 35 45; do
		echo -n "ready for scan row${row}g${gval},"
		echo -n "press enter to start, s to skip, q to quit: "
		read ans
		if [[ "$ans" = "q" ]]; then
			exit 0
		elif [[ "$ans" = "s" ]]; then
			continue
		fi
                ans=
		while [[ "$ans" != "a" ]]; do
			rm threshold_scans/*.txt
			./threshold_scan.sh
			echo -n "press enter to repeat, a to accept: "
			read ans
		done
		mv threshold_scans/threshold_scan.log threshold_scan_r${row}_g${gval}.log
		rm threshold_scans/*.txt
	done
done
