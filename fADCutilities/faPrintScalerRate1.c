/*
 * File:
 *    faPrintScalerRate1.c
 *  
 *
 * Description:
 *    Print scaler rates from all of the fADC250s in the crate
 *
 *  Bryan Moffit - November 2014
 *
 * Update:
 *    Modified to watch the scalers for just one second,
 *    print them out and wait for the user to press enter
 *    (or q) before continuing around the loop.
 *
 *  Richard Jones - January 13, 2018
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

	unsigned int data1[FA_MAX_BOARDS][17];
	unsigned int data2[FA_MAX_BOARDS][17];

	int stat;

	printf("\nJLAB fADC250 Print Scalers\n");
	printf("----------------------------\n");

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

	while (1) {
		for(ifa=0; ifa<nfadc; ifa++) {
			faReadScalers(faSlot(ifa), data1[ifa], 0xffff, iFlag);
			if (DEBUG>2) { // print the first read
				printf("Mod %2i   ", faSlot(ifa));
				for (chan=0; chan<=16; chan++) {
					if (chan==8) printf("\n         ");
					printf("%10u ",data1[ifa][chan]);
				}
				printf("\n");
			}
		}
		// sleep to accumulate scaler data
		sleep(1);
		// loop over modules to read scalers 
		for(ifa=0; ifa<nfadc; ifa++) {
			faReadScalers(faSlot(ifa), data2[ifa], 0xffff, iFlag);
			if (DEBUG>3) { // printf the second read
				printf("Mod %2i   ", faSlot(ifa));
				for (chan=0; chan<=16; chan++) {
					if (chan==8) printf("\n         ");
					printf("%10u ",data2[ifa][chan]);
				}
				printf("\n");
			}
		}
		//if (DEBUG>1) { // printf the rates
		printf("Scaler rates in Hz\n");
		for(ifa=0; ifa<nfadc; ifa++) {
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
		//}
		{
			char ans[99];
			printf("press enter to continue, q to quit:");
			fgets(ans, 90, stdin);
			if (ans[0] == 'q')
				break;
		} 
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
