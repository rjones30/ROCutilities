/*
 * File:
 *    faMapRates.c
 *
 * Description:
 *    Move the trigger path threshold around desired pedestal value.
 *    Output measured rates over range to file.
 *
 */


/*
setenv LINUXVME_INC ~/git/include
setenv LINUXVME_LIB ~/git/lib
setenv LD_LIBRARY_PATH ${LINUXVME_LIB}:${LD_LIBRARY_PATH}
cd /gluonfs1/home/dalton/svn/daq_dev_vers/daq/vme/src/epics
faMapRates 100

ssh rocbcal1
cd /gluonfs1/home/dalton/svn/daq_dev_vers/daq/vme/src/vmefa/test
 */

// crontab
//  */5  * * * *  $HOME/svn/daq_dev_vers/daq/vme/src/vmefa/test/maprates_onroc.csh > /dev/null


#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "fadcLib.h"

#define FADC_ADDR 0xed0000

extern int fadcA32Base;
extern int fadcID[FA_MAX_BOARDS];
extern int nfadc;

char *progName;
void Usage();
void PrintDataArray(int *array[]);

int 
main(int argc, char *argv[]) 
{
	int threshstart=0, threshstop=0, threshstep=1;

	/* Parse command line arguments */
	progName = argv[0];
	switch(argc) {
	case 3:
		{
			threshstart = strtoll(argv[1],NULL,10);
			threshstop = strtoll(argv[2],NULL,10);
			break;
		}
	case 4:
		{
			threshstart = strtoll(argv[1],NULL,10);
			threshstop = strtoll(argv[2],NULL,10);
			threshstep = strtoll(argv[3],NULL,10);
			break;
		}
	default:
		printf(" Incorrect number of arguments.\n");
		Usage();
		goto CLOSE;
    }

	if (threshstop<threshstart) {
		printf("Threshold_min < Threshold_max\n");
		return -1;
	}

	int DEBUG=0;


	int sleepval=1000000;
	GEF_STATUS status;
	int i,j;
	int ifa, chan;
	unsigned int scalerdata1[FA_MAX_BOARDS][17];
	unsigned int scalerdata2[FA_MAX_BOARDS][17];
	unsigned int OrigThresh[FA_MAX_BOARDS];

	FILE *outfile[FA_MAX_BOARDS][FA_MAX_ADC_CHANNELS];      // have a potential file handle for each channel
	int fileisopen[FA_MAX_BOARDS][FA_MAX_ADC_CHANNELS];     // record if file handle is open for each channel
	for (i=0; i<FA_MAX_BOARDS; i++) {
		for (j=0; j<FA_MAX_ADC_CHANNELS; j++) {
			fileisopen[i][j]=0;
		}
	}
	char filename[255];



	char *host;
	//host = getenv("HOSTNAME");
	host = getenv("HOST");
	printf("Running on host %s\n",host);

	// Make a date time label
	time_t result = time(NULL); 
	const char *timestring = ctime(&result);
	char datetimelabel[255];
	sprintf(datetimelabel,"");
	int count=0;
	printf("%s",ctime(&result));
	char *pch;
	pch = strtok (timestring," :");
	while (pch != NULL) {
		if (count==3) sprintf(datetimelabel,"%s_",datetimelabel);
		if (count==4) sprintf(datetimelabel,"%sh",datetimelabel);
		if (count<5) sprintf(datetimelabel,"%s%s",datetimelabel,pch);
		count++;
		pch = strtok (NULL, " :");
	}
	printf("datetimelabel '%s'\n",datetimelabel);



	printf("\nJLAB fadc calibrate pedestals\n");
	printf("-------------------------------\n");
  
	status = vmeOpenDefaultWindows();

	fadcA32Base=0x09000000;
	/* Set the FADC structure pointer */

	int InitFlag;
	//	InitFlag = FA_INIT_SOFT_SYNCRESET | FA_INIT_SOFT_TRIG | FA_INIT_INT_CLKSRC;
	InitFlag = FA_INIT_SKIP; /* Do not attempt to initialize the module(s) */
	InitFlag |= FA_INIT_SKIP_FIRMWARE_CHECK;
	printf(" Locating fADC250s in the crate...\n");

	int stat;
	stat = faInit((3<<19),(1<<19),20,InitFlag);
	printf("Init status bit: %i\n",stat);

	faCheckAddresses(0);
	faGStatus(0);
	faEnableScalers(0);
	vmeBusLock();

	/* printf("Reporting initial values\n"); */
	/* for(ifa=0; ifa<nfadc; ifa++) */
	/* 	faPrintDAC(faSlot(ifa)); */
	/* for(ifa=0; ifa<nfadc; ifa++)  */
	/* 	faPrintThreshold(faSlot(ifa)); */
	
	// Save initial threshold values
	for(ifa=0; ifa<nfadc; ifa++) 
		OrigThresh[ifa] = faGetTriggerPathThreshold(faSlot(ifa));
	for(ifa=0; ifa<nfadc; ifa++) 
		printf("Mod %2i  Existing trigger path threshold: %3i\n",faSlot(ifa),OrigThresh[ifa]);

	int ScalerReadFlag;
	// ScalerReadFlag = (1<<0) && (1<<1);   // latch and clear
	ScalerReadFlag = (1<<0);             // latch

	// Loop over threshold values
	int threshval;
	for (threshval = threshstart; threshval<=threshstop; threshval+=threshstep) {
		if (DEBUG>0) printf("Setting threshold value to: %i\n",threshval);
		else {
			printf("Setting threshold value to: %i\n",threshval);
			fflush(stdout);
		}
		// loop over modules to set trigger path threshold
		for(ifa=0; ifa<nfadc; ifa++) 
			faSetTriggerPathThreshold(faSlot(ifa),threshval);
		// loop over modules to read scalers 
		for(ifa=0; ifa<nfadc; ifa++) {
			faReadScalers(faSlot(ifa), &scalerdata1[ifa], 0xffff, ScalerReadFlag);
			if (DEBUG>2) { // print the first read
				printf("Mod %2i   ", faSlot(ifa));
				for (chan=0; chan<FA_MAX_ADC_CHANNELS; chan++) {
					if (chan==8) printf("\n         ");
					printf("%10u ",scalerdata1[ifa][chan]);
				}
				printf("\n");
			}
		}
		// sleep to accumulate scaler scalerdata
		usleep(sleepval);
		// loop over modules to read scalers 
		for(ifa=0; ifa<nfadc; ifa++) {
			faReadScalers(faSlot(ifa), &scalerdata2[ifa], 0xffff, ScalerReadFlag);
			if (DEBUG>3) { // printf the second read
				printf("Mod %2i   ", faSlot(ifa));
				for (chan=0; chan<FA_MAX_ADC_CHANNELS; chan++) {
					if (chan==8) printf("\n         ");
					printf("%10u ",scalerdata2[ifa][chan]);
				}
				printf("\n");
			}
			// Write data to file
			double clockcycles = scalerdata2[ifa][16]-scalerdata1[ifa][16];
			double time = clockcycles/250000.;
			for (chan=0; chan<FA_MAX_ADC_CHANNELS; chan++) {
				double rate = (scalerdata2[ifa][chan]-scalerdata1[ifa][chan])/time;
				if (fileisopen[ifa][chan]==0) {
					sprintf(filename,"ADC_ratevthresh_%s_%s_s%02ic%02i.txt",host,datetimelabel,faSlot(ifa),chan);
					//printf("opening file: %s\n",filename);
					outfile[ifa][chan] = fopen(filename, "a");
					fileisopen[ifa][chan] = 1;
				}
				fprintf(outfile[ifa][chan],"%4i %13.3f\n",threshval,rate);
			}
		}
	}

	printf("Closing the text files\n");
	for (i=0; i<FA_MAX_BOARDS; i++) {
		for (j=0; j<FA_MAX_ADC_CHANNELS; j++) {
			if (fileisopen[i][j]==1) {
				fclose(outfile[i][j]);
			}
		}
	}

	// set the original threshold values
	printf("Restoring trigger path thresholds.\n");
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



void
Usage()
{
	printf("\nUSAGE:\n\n");
	printf("%s Threshold_min Threshold_max Thresold_step\n",progName);
	printf("\t - Find DAC value to put pedestal at PedestalValue\n\n");
	printf("\n\n");
}


void PrintDataArray(int *array[]) {
	int ifa,chan;
	for(ifa=0; ifa<nfadc; ifa++) {
		printf("Mod %2i   \n", faSlot(ifa));
		for (chan=0; chan<FA_MAX_ADC_CHANNELS; chan++) {
			printf("%i  %i\n",ifa,chan);
			printf("%10u \n",array[ifa][chan]);
		}
		printf("\n");
	}
}
