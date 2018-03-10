/*
 * File:
 *    faSetDAC.c
 *  
 *
 * Description:
 *    Find all of the fADC250s in the crate, and set all of their channel
 *    DAC to a specified value from the command line.
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
	int common_DAC=0;

	printf("\nJLAB fADC250 Set DAC\n");
	printf("----------------------------\n");

	progName = argv[0];

	/* Parse command line arguments */
	switch(argc)
		{
		case 2: 
			{
				common_DAC = strtoll(argv[1],NULL,10);
				break;
			}
      
		default:
			printf(" Incorrect number of arguments..\n");
			Usage();
			goto CLOSE;
		}

	/* Check user input */

  
	if((common_DAC<0) | (common_DAC>0xfff))
		{
			printf(" Invalid DAC value %d (0x%x)\n",common_DAC,common_DAC);
			Usage();
			goto CLOSE;
		}

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

	printf(" Will set all DAC values to %d\n",common_DAC);
	for(ifa=0; ifa<nfadc; ifa++)
		faSetDAC(faSlot(ifa), common_DAC, 0xffff);


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
