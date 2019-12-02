/*
 * File:
 *    faDoThresholdScan.c
 *  
 *
 * Description:
 *    Do a threshold scan on a subset of the fADC250s in the crate
 *
 *  Richard Jones - January 2018
 *
 * Based on original code in faPrintScalerRates.c written by
 *  Bryan Moffit - November 2014
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "fadcLib.h"

char *progName;
void Usage();

extern int nfadc;

int 
main(int argc, char *argv[]) 
{
	int DEBUG=0;

	// The following simple scan parameters have been replaced
 	// with a graded-scale stepping scheme described by threshold_seq
	//short int threshold_start = 100;
	//short int threshold_end = 500;
	//short int threshold_step = 10;
	short int thresh;

	int ithr;
	int threshold_seq_length = 67;
	int threshold_seq[67] =
	{100, 102, 104, 106, 108,
	 110, 112, 114, 116, 118, 120,
	 124, 128, 132, 136, 140,
	 150, 160, 170, 180, 190, 200, 210, 220, 230, 240, 250, 260, 271, 280, 290, 300,
	 320, 340, 360, 380, 400, 420, 440, 460, 480, 500, 520, 540, 560, 580, 600,
	 650, 700, 750, 800, 850, 900, 950, 1000, 1050, 1100, 1150, 1200,
	 1300, 1400, 1500, 1600, 1700, 1800, 1900, 2000};

	unsigned int channel_mask[FA_MAX_BOARDS] = {0};
	int channels_selected;

	unsigned int data1[FA_MAX_BOARDS][17];
	unsigned int data2[FA_MAX_BOARDS][17];

	int stat;

	printf("\nJLAB fADC250 Threshold Scan\n");
	printf("-----------------------------\n");

	vmeSetQuietFlag(1);
	if(vmeOpenDefaultWindows()!=OK)
		{
			printf(" Failed to access VME bridge\n");
			goto CLOSE;
		}

	int chan=0;
	int ifa=0;
	int iFlag=0;

	iFlag = FA_INIT_SKIP; /* Do not attempt to initialize the module(s) */
	iFlag |= FA_INIT_SKIP_FIRMWARE_CHECK;
	printf(" Locating fADC250s in the crate...\n");

	vmeBusLock();

	stat = faInit((3<<19),(1<<19),20,iFlag);
	if(nfadc==0)
		{
			vmeBusUnlock();
			goto CLOSE;
		}

	iFlag = (1<<0);

	channels_selected = 0;
	while (!channels_selected) {
		int islot, channel;
#ifdef ASK_USER_TO_SELECT_COLUMNS
		printf("enter slot, channel (channel=-1 for all, 0 when done\n");
		if (scanf("%d, %d", &islot, &channel) == 2) {
			int slot = 0;
			if (islot == 0 || channel == 0)
				break;
			for (ifa=0; ifa<nfadc; ifa++) {
				if (islot == faSlot(ifa)) {
					slot = faSlot(ifa);
					break;
				}
				else if (islot == -1) {
					slot = faSlot(ifa);
					channel_mask[slot-1] |= 1<<(channel-1);
					slot = -1;
				}
			}
			if (slot > 0 && (channel > 0 || channel < 17))
				channel_mask[slot-1] |= 1<<(channel-1);
			else if (slot > 0 && channel == -1)
				channel_mask[slot-1] = 0xffff;
			else if (slot != -1)
				printf("invalid input, please enter valid slot, channel\n");
		}
		else {
			printf("invalid input, please enter slot, channel\n");
		}
#else
		for (ifa=0; ifa<nfadc; ifa++) {
			int slot = faSlot(ifa);
			channel_mask[slot-1] |= 0xffff;
		}
		break;
#endif
	}

	for (ithr = 0; ithr < threshold_seq_length; ++ithr) {
		thresh = threshold_seq[ithr];
		printf("threshold set to %d\n", thresh);
		for	(ifa=0; ifa<nfadc; ifa++) {
			int slot = faSlot(ifa);
			if (channel_mask[slot-1]) {
				//faSetThreshold(slot, thresh, channel_mask[slot-1]);
				faSetTriggerPathThreshold(slot, thresh);
				faReadScalers(slot, data1[ifa], 0xffff, iFlag);
				if (DEBUG>2) { // print the first read
					printf("Mod %2i   ", faSlot(ifa));
					for (chan=0; chan<=16; chan++) {
						if (chan==8) printf("\n         ");
						printf("%10u ",data1[ifa][chan]);
					}
					printf("\n");
				}
			}
		}

		// sleep to accumulate scaler data
		sleep(1);

		// loop over modules to read scalers 
		for (ifa=0; ifa<nfadc; ifa++) {
			int slot = faSlot(ifa);
			if (channel_mask[slot-1]) {
				faReadScalers(slot, data2[ifa], 0xffff, iFlag);
				if (DEBUG>3) { // printf the second read
					printf("Mod %2i   ", faSlot(ifa));
					for (chan=0; chan<=16; chan++) {
						if (chan==8) printf("\n         ");
						printf("%10u ",data2[ifa][chan]);
					}
					printf("\n");
				}
			}
		}

		//if (DEBUG>1) { // printf the rates
			printf("Scaler rates in Hz\n");
			for (ifa=0; ifa<nfadc; ifa++) {
				int slot = faSlot(ifa);
				if (channel_mask[slot-1]) {
					double time=(data2[ifa][16]-data1[ifa][16]); // in units of 2048 ns
					time *= 2048e-9; // in units of seconds
					printf("Mod %2i   ", faSlot(ifa));
					for (chan=0; chan<=16; chan++) {
						double rate = (data2[ifa][chan]-data1[ifa][chan])/time;
						if (chan==8) printf("\n         ");
						printf("%12.1f ",rate);
					}
					printf("\n");
				}
			}
		//}
	}


	vmeBusUnlock();


 CLOSE:

	printf("Done\n Thank you for using %s.\n\n",progName);
	vmeCloseDefaultWindows();

	exit(0);
}


void
Usage()
{
	printf("\nUSAGE:\n\n");
	printf("%s DAC\n",progName);
	printf("      - Set all DAC values to the common DAC\n\n");
	printf("\n\n");
}
