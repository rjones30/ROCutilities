/*
 * File:
 *    faPrintThresholds.c
 *  
 *
 * Description:
 *    Print thresholds from all of the fADC250s in the crate
 *
 *  Bryan Moffit - November 2014
 */


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

	int stat;

	printf("\nJLAB fADC250 Print Thresholds\n");
	printf("--------------------------------\n");

	vmeSetQuietFlag(1);
	if(vmeOpenDefaultWindows()!=OK)
		{
			printf(" Failed to access VME bridge\n");
			goto CLOSE;
		}


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

	for(ifa=0; ifa<nfadc; ifa++)
		faPrintThreshold(faSlot(ifa));


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
	printf("%s\n",progName);
	printf("      - Print thresholds on all fadc modules\n\n");
	printf("\n\n");
}
