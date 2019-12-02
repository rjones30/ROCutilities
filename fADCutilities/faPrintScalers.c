/*
 * File:
 *    faPrintScalers.c
 *  
 *
 * Description:
 *    Print scalers from all of the fADC250s in the crate.
 *
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
	//int DEBUG=4;

	unsigned int data1[FA_MAX_BOARDS][17];
	//unsigned int data2[FA_MAX_BOARDS][17];

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
			printf("Mod %2i   ", faSlot(ifa));
			for (chan=0; chan<=16; chan++) {
				if (chan==8) printf("\n         ");
				printf("%10u ",data1[ifa][chan]);
			}
			printf("\n");
		}
		sleep(1);
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
