/*
 * File:
 *    faSetThresholds.c
 *  
 *
 * Description:
 *    Find all of the fADC250s in the crate, and set all of their channel
 *    thresholds to a specified value from the command line.
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
  int thres_type = 0;
  int common_threshold=0;

  printf("\nJLAB fADC250 Set Threshold\n");
  printf("----------------------------\n");

  progName = argv[0];

  /* Parse command line arguments */
  switch(argc)
    {
    case 2: /* One threshold to rule them all */
      {
	thres_type = 0;
	common_threshold = strtoll(argv[1],NULL,10);
	break;
      }
      
    case 3:
      {
	thres_type = atoi(argv[1]);
	common_threshold = strtoll(argv[2],NULL,10);
	break;
      }

    default:
      printf(" Incorrect number of arguments..\n");
      Usage();
      goto CLOSE;
    }

  /* Check user input */
  if((thres_type < 0) | (thres_type>2) )
    {
      printf(" Incorrect threshold type..\n");
      Usage();
      goto CLOSE;
    }
  
  if((common_threshold<0) | (common_threshold>0xffff))
    {
      printf(" Invalid threshold value %d (0x%x)\n",common_threshold,common_threshold);
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
  char *type_name[3] = { "ALL", "Readout", "Trigger" };

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

  switch(thres_type)
    {
    case 0: /* All of 'em */
      printf(" Will set all (Readout and Trigger) Thresholds to %d\n",common_threshold);
      faGSetTriggerPathThreshold(common_threshold);
      for(ifa=0; ifa<nfadc; ifa++)
	faSetThreshold(faSlot(ifa), common_threshold, 0xffff);
      break;

    case 1: /* Readout */
      printf(" Will set %s Thresholds to %d\n",type_name[thres_type],common_threshold);
      for(ifa=0; ifa<nfadc; ifa++)
	faSetThreshold(faSlot(ifa), common_threshold, 0xffff);
      break;

    case 2: /* Trigger */
    default: 
      printf(" Will set %s Thresholds to %d\n",type_name[thres_type],common_threshold);
      faGSetTriggerPathThreshold(common_threshold);
      
      break;
      
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
  printf("%s THRESHOLD\n",progName);
  printf("      - Set all thresholds (Readout & Trigger) to the common THRESHOLD\n\n");
  printf("%s TYPE THRESHOLD\n",progName);
  printf("      - Set all thresholds of TYPE to the common THRESHOLD\n\n");
  printf("              Where TYPE =  1  for Readout\n");
  printf("                            2  for Trigger\n");
  printf("\n\n");
}
