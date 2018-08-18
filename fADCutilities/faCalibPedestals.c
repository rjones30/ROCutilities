/*
 * File:
 *    faCalibPedestals.c
 *
 * Description:
 *    Sets trigger path threshold to the desired pedestal value.
 *    Move the DAC value between 2 limits to find the threshold 
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

char *progName;
void Usage();
void PrintDataArray(int *array[]);

int 
main(int argc, char *argv[]) 
{
	int thresh=0;

	/* Parse command line arguments */
	switch(argc) {
	case 2: /* One threshold to rule them all */
		{
			thresh = strtoll(argv[1],NULL,10);
			break;
		}
	default:
		printf(" Incorrect number of arguments..\n");
		Usage();
		goto CLOSE;
    }


	int DEBUG=0;
	if (argc>1) {
		DEBUG = atoi(argv[1]);
		printf("Set DEBUG to %i\n",DEBUG);
	}

	int shortsleepval=10000;
	//int longsleepval=1000000;
	GEF_STATUS status;
	int i,j;
	int ifa, chan;
	unsigned int scalerdata1[FA_MAX_BOARDS][17];
	unsigned int scalerdata2[FA_MAX_BOARDS][17];
	unsigned short OrigDAC[FA_MAX_BOARDS][FA_MAX_ADC_CHANNELS];
	unsigned int OrigThresh[FA_MAX_BOARDS];
	float highestrate[FA_MAX_BOARDS][FA_MAX_ADC_CHANNELS];
	int highestdacval[FA_MAX_BOARDS][FA_MAX_ADC_CHANNELS];
	int dacstart=3250; // 3265 bcal7
	int dacstop=3499;  // 3448 bcal11
	//int dacstart=3000, dacstop=3700;
	int lowsingledac, highsingledac;
	highsingledac=dacstart;
	lowsingledac=dacstop;

	// initialise array values
	for (i=0; i<FA_MAX_BOARDS; i++) {
		for (j=0; j<FA_MAX_ADC_CHANNELS; j++) {
			highestrate[i][j]=0;
			highestdacval[i][j]=0;
		}
	}

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

	printf("Reporting initial values\n");
	for(ifa=0; ifa<nfadc; ifa++)
		faPrintDAC(faSlot(ifa));
	for(ifa=0; ifa<nfadc; ifa++) 
		faPrintThreshold(faSlot(ifa));
	
	// save initial DAC and threshold values
	for(ifa=0; ifa<nfadc; ifa++) 
		faGetDAC(faSlot(ifa), OrigDAC[ifa]);
	for(ifa=0; ifa<nfadc; ifa++) 
		OrigThresh[ifa] = faGetTriggerPathThreshold(faSlot(ifa));

	for(ifa=0; ifa<nfadc; ifa++) 
		printf("Mod %2i Trigger path threshold: %3i\n",faSlot(ifa),OrigThresh[ifa]);
	//PrintDataArray(OrigDAC);

	printf("Setting trigger path threshold to %i\n",thresh);
	faGSetTriggerPathThreshold(thresh);


	int ScalerReadFlag;
	//ScalerReadFlag = (1<<0) && (1<<1);   // latch and clear
	ScalerReadFlag = (1<<0);             // latch
	//loop over DAC values
	int dacval;
	for (dacval = dacstart; dacval<=dacstop; dacval++) {
		if (DEBUG>0) printf("Setting DAC value to: %i\n",dacval);
		else {
			printf("Setting DAC value to: %i\r",dacval);
			fflush(stdout);
		}
		// loop over modules to set DAC
		for(ifa=0; ifa<nfadc; ifa++) {
			faSetDAC(faSlot(ifa), dacval, 0xffff);
		}
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
		// sleep to accumulate scaler scalerdata
		usleep(shortsleepval);
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
			// save DAC value if rate is highest
			double time=scalerdata2[ifa][16]-scalerdata1[ifa][16];
			for (chan=0; chan<=15; chan++) {
				double rate = (scalerdata2[ifa][chan]-scalerdata1[ifa][chan])/time;
				if (rate>highestrate[ifa][chan]) {
					highestrate[ifa][chan]=rate;
					highestdacval[ifa][chan]=dacval;
				}
			}
		}
	}
	// print results
	printf("\nRESULTS\n\n");
	if (DEBUG>0) {
		for(ifa=0; ifa<nfadc; ifa++) {
			printf("Mod %2i   ", faSlot(ifa));
			for (chan=0; chan<=15; chan++) {
				printf("%5.2f ",highestrate[ifa][chan]);
			}
			printf("\n");
		}
	}
	// Find the lowest and highest values
	for(ifa=0; ifa<nfadc; ifa++) {
		printf("Mod %2i   ", faSlot(ifa));
		for (chan=0; chan<=15; chan++) {
			if (highestdacval[ifa][chan]>highsingledac) highsingledac=highestdacval[ifa][chan];
			if (highestdacval[ifa][chan]<lowsingledac) lowsingledac=highestdacval[ifa][chan];
			printf("%i  ",highestdacval[ifa][chan]);
		}
		printf("\n");
	}


	// Make a date time label
	time_t result = time(NULL); 
	const char *timestring = ctime(&result);
	char datetimelabel[255] = {0};
	printf("%s",ctime(&result));
	char *pch;
	pch = strtok ((char*)timestring," :");
	int count=0;
	while (pch != NULL) {
		if (count==3) sprintf(datetimelabel,"%s_",datetimelabel);
		if (count==4) sprintf(datetimelabel,"%sh",datetimelabel);
		if (count<5) sprintf(datetimelabel,"%s%s",datetimelabel,pch);
		count++;
		pch = strtok (NULL, " :");
	}
	printf("datetimelabel '%s'\n",datetimelabel);
	// Write a new configuration file
	char *host;
	host = getenv("HOSTNAME");
	printf("Running on host %s\n",host);
	printf("DAC values from %i to %i\n",lowsingledac,highsingledac);
	char filename[255];
	sprintf(filename,"%s_fadc250_default.cnf.%s",host,datetimelabel);
	FILE *outfile;
	outfile = fopen(filename, "w");
	printf("Writing to %s\n\n",filename);
	fprintf(outfile,"# %s",ctime(&result));
	fprintf(outfile,"# pedestals determined using faCalibPedestals (not with DAQ)\n");
	fprintf(outfile,"# contact Mark Dalton\n\n");
	fprintf(outfile,"# DAC values range from %i to %i\n",lowsingledac,highsingledac);
	fprintf(outfile,"CRATE  %s \n\n",host);
	fprintf(outfile,"#   slots:   1   2  3  4  ... \n\n\n");
	for(ifa=0; ifa<nfadc; ifa++) {
		fprintf(outfile,"############################\n");
		fprintf(outfile,"FADC250_SLOTS   %i  \n", faSlot(ifa));
		fprintf(outfile,"#########################\n");
		fprintf(outfile,"FADC250_ALLCH_DAC   ");
		for (chan=0; chan<=15; chan++) {
			fprintf(outfile,"%i  ",highestdacval[ifa][chan]);
		}
		fprintf(outfile,"\n");
		fprintf(outfile,"FADC250_ALLCH_THR   ");
		for (chan=0; chan<=15; chan++) {
			fprintf(outfile,"%4i  ",5);
		}
		fprintf(outfile,"\n\n");
	}
	fclose(outfile);

	// Set the DAC values to the optimum
	for(ifa=0; ifa<nfadc; ifa++) {
		for (chan=0; chan<=15; chan++) {
			faSetDAC(faSlot(ifa), highestdacval[ifa][chan], (1<<chan));
		}
	}

	// set the original threshold values
	printf("Restoring trigger path thresholds");
	for(ifa=0; ifa<nfadc; ifa++) 
		faSetTriggerPathThreshold(faSlot(ifa),OrigThresh[ifa]);

	// determine the rate at the new settings
	if (0) {
		printf("measuring pedestal rate\n");
		for(ifa=0; ifa<nfadc; ifa++) {
			faSetDAC(faSlot(ifa), dacval, 0xffff);
			faReadScalers(faSlot(ifa), scalerdata1[ifa], 0xffff, ScalerReadFlag);
		}
		usleep(10000000);
		for(ifa=0; ifa<nfadc; ifa++) {
			printf("Mod %2i   ", faSlot(ifa));
			faReadScalers(faSlot(ifa), scalerdata2[ifa], 0xffff, ScalerReadFlag);
			double time=scalerdata2[ifa][16]-scalerdata1[ifa][16];
			for (chan=0; chan<=15; chan++) {
				if (chan==8) printf("\n         ");
				double rate = (scalerdata2[ifa][chan]-scalerdata1[ifa][chan])/time;
				printf("%11.8f ",rate);
			}
			printf("%10u %10u\n",scalerdata1[ifa][16],scalerdata2[ifa][16]);
		}
	}

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
	printf("%s PedestalValue\n",progName);
	printf("\t - Find DAC value to put pedestal at PedestalValue\n\n");
	printf("\n\n");
}


void PrintDataArray(int *array[]) {
	int ifa,chan;
	for(ifa=0; ifa<nfadc; ifa++) {
		printf("Mod %2i   ", faSlot(ifa));
		for (chan=0; chan<=16; chan++) {
			printf("%10u ",array[ifa][chan]);
		}
		printf("\n");
	}
}
