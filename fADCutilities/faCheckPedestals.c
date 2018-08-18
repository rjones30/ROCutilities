/*
 * File:
 *    faCheckPedestals.c
 *
 * Description:
 *    Move the trigger path threshold between 2 limits to find a 
 *    maximum rate of increase in the scalers.
 *
 */


#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "fadcLib.h"
#include "fadcLib_extensions.h"

#define FADC_ADDR 0xed0000

extern int fadcA32Base;
extern int fadcID[FA_MAX_BOARDS];
extern int nfadc;

//  int  faSetThreshold(int id, unsigned short tvalue, unsigned short chmask);
//  int  faReadScalers(int id, volatile unsigned int *data, unsigned int chmask, int rflag);
//  int  faSetDAC(int id, unsigned short dvalue, unsigned short chmask);
//  STATUS faInit (UINT32 addr, UINT32 addr_inc, int nadc, int iFlag);

int 
main(int argc, char *argv[]) 
{
	int DEBUG=4;
	if (argc>1) {
		DEBUG = atoi(argv[1]);
		printf("Set DEBUG to %i\n",DEBUG);
	}

	int sleepval=100000;
	GEF_STATUS status;
	int i,j, iflag;
	unsigned int scalerdata1[FA_MAX_BOARDS][17];
	unsigned int scalerdata2[FA_MAX_BOARDS][17];
	//unsigned int OrigDAC[FA_MAX_BOARDS][FA_MAX_ADC_CHANNELS];
	unsigned int OrigThresh[FA_MAX_BOARDS];
	float highestrate[FA_MAX_BOARDS][FA_MAX_ADC_CHANNELS];
	int highestthreshval[FA_MAX_BOARDS][FA_MAX_ADC_CHANNELS];
	int threshstart=90, threshstop=110;
	int lowsinglethresh, highsinglethresh;
	highsinglethresh=threshstart;
	lowsinglethresh=threshstop;

	for (i=0; i<FA_MAX_BOARDS; i++) {
		for (j=0; j<FA_MAX_ADC_CHANNELS; j++) {
			highestrate[i][j]=0;
			highestthreshval[i][j]=0;
		}
	}


	printf("\nJLAB fadc calibrate pedestals\n");
	printf("-------------------------------\n");
  
	status = vmeOpenDefaultWindows();

	iflag = FA_INIT_SOFT_SYNCRESET | FA_INIT_SOFT_TRIG | FA_INIT_INT_CLKSRC;

	fadcA32Base=0x09000000;
	/* Set the FADC structure pointer */
	//faInit((unsigned int)(FADC_ADDR),(1<<19),1,iflag);

	int iFlag;
	iFlag = FA_INIT_SKIP; /* Do not attempt to initialize the module(s) */
	iFlag |= FA_INIT_SKIP_FIRMWARE_CHECK;
	printf(" Locating fADC250s in the crate...\n");

	int stat;
	stat = faInit((3<<19),(1<<19),20,iFlag);
	printf("Init status bit: %i\n",stat);

	faCheckAddresses(0);
	faGStatus(0);
	faEnableScalers(0);
	vmeBusLock();

	int ifa, chan;

	printf("Reporting initial values\n");
	for(ifa=0; ifa<nfadc; ifa++)
		faPrintDAC(faSlot(ifa));

	// save initial threshold values
	for(ifa=0; ifa<nfadc; ifa++) 
		OrigThresh[ifa] = faGetTriggerPathThreshold(faSlot(ifa));

	int threshval;

	int ScalerReadFlag;
	//ScalerReadFlag = (1<<0) && (1<<1);   // latch and clear
	ScalerReadFlag = (1<<0);             // latch

	//loop over Threshold values
	for (threshval = threshstart; threshval<=threshstop; threshval++) {
		if (DEBUG>0) printf("Setting Threshold value to: %i\n",threshval);
		else {
			printf("Setting Threshold value to: %i\r",threshval);
			fflush(stdout);
		}
		faGSetTriggerPathThreshold(threshval);
		// loop over modules to set Threshold and read scalers 
		//for(ifa=0; ifa<nfadc; ifa++) {
		//	faSetThreshold(faSlot(ifa), threshval, 0xffff);
		//}
		// loop over modules to read scalers 
		for(ifa=0; ifa<nfadc; ifa++) {
			faReadScalers(faSlot(ifa), scalerdata1[ifa], 0xffff, ScalerReadFlag);
			if (DEBUG>2) { // print the first read
				printf("Mod %2i   ", faSlot(ifa));
				for (chan=0; chan<=16; chan++) {
					if (chan==8) printf("\n         ");
					printf("%10u ",scalerdata1[ifa][chan]);
				}
				printf("\n");
			}
		}
		// sleep to accumulate scaler data
		usleep(sleepval);
		// loop over modules to read scalers 
		for(ifa=0; ifa<nfadc; ifa++) {
			faReadScalers(faSlot(ifa), scalerdata2[ifa], 0xffff, ScalerReadFlag);
			if (DEBUG>3) { // printf the second read
				printf("Mod %2i   ", faSlot(ifa));
				for (chan=0; chan<=16; chan++) {
					if (chan==8) printf("\n         ");
					printf("%10u ",scalerdata2[ifa][chan]);
				}
				printf("\n");
			}
			// save Threshold value if rate is highest
			double time=scalerdata2[ifa][16]-scalerdata1[ifa][16];
			for (chan=0; chan<=15; chan++) {
				double rate = (scalerdata2[ifa][chan]-scalerdata1[ifa][chan])/time;
				if (rate>highestrate[ifa][chan]) {
					highestrate[ifa][chan]=rate;
					highestthreshval[ifa][chan]=threshval;
				}
			}
		}
		if (DEBUG>1) { // printf the rates
			for(ifa=0; ifa<nfadc; ifa++) {
				double time=scalerdata2[ifa][16]-scalerdata1[ifa][16];
				printf("Mod %2i   ", faSlot(ifa));
				for (chan=0; chan<=16; chan++) {
					double rate = (scalerdata2[ifa][chan]-scalerdata1[ifa][chan])/time;
					if (chan==8) printf("\n         ");
					printf("%10.5f ",rate);
				}
				printf("\n");
			}
		}
	}
	// print results
	printf("\nRESULTS\n\n");
	if (1) { //if (DEBUG>0) {
		for(ifa=0; ifa<nfadc; ifa++) {
			printf("Mod %2i   ", faSlot(ifa));
			for (chan=0; chan<=15; chan++) {
				printf("%6.3f ",highestrate[ifa][chan]);
			}
			printf("\n");
		}
	}
	for(ifa=0; ifa<nfadc; ifa++) {
		printf("Mod %2i   ", faSlot(ifa));
		for (chan=0; chan<=15; chan++) {
			if (highestthreshval[ifa][chan]>highsinglethresh) highsinglethresh=highestthreshval[ifa][chan];
			if (highestthreshval[ifa][chan]<lowsinglethresh) lowsinglethresh=highestthreshval[ifa][chan];
			printf("%6i ",highestthreshval[ifa][chan]);
		}
		printf("\n");
	}

	// set the original threshold values
	printf("Restoring trigger path thresholds");
	for(ifa=0; ifa<nfadc; ifa++) 
		faSetTriggerPathThreshold(faSlot(ifa),OrigThresh[ifa]);

	// close down
	vmeBusUnlock();
	faDisableScalers(0);


	goto CLOSE;

 CLOSE:
	status = vmeCloseDefaultWindows();
	if (status != GEF_SUCCESS)
		{
			printf("vmeCloseDefaultWindows failed: code 0x%08x\n",status);
			return -1;
		}
	exit(0);
}

