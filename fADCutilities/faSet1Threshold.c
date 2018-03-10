/*
 * File:
 *    faSet1Threshold.c
 *  
 *
 * Description:
 *    Set a single threshold value in one slot and one channel
 *    and one threshold type to a specified value from the command line.
 *
 *  Richard Jones - January 2018
 *
 * based on faSetThresholds.c by Bryan Moffit - November 2014
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
  int slot, channel;

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

  slot = 0;
  while (slot == 0) {
    int islot;
    printf("enter slot, channel\n");
    if (scanf("%d, %d", &islot, &channel) == 2) {
      for (ifa=0; ifa<nfadc; ifa++) {
        if (islot == faSlot(ifa)) {
          slot = faSlot(ifa);
          break;
        }
      }
      if (channel < 1 || channel > 16)
        slot = 0;
      if (slot == 0)
        printf("invalid input, please enter valid slot, channel\n");
    }
    else {
      printf("invalid input, please enter slot, channel\n");
    }
  }


  switch(thres_type)
    {
    case 0:
      faSetTriggerPathThreshold(slot, common_threshold);
      faSetThreshold(slot, common_threshold, 1<<(channel-1));
      break;

    case 1: /* Readout */
      faSetThreshold(slot, common_threshold, 1<<(channel-1));
      break;

    case 2: /* Trigger */
    default: 
      faSetTriggerPathThreshold(slot, common_threshold);
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
  printf("      - Set both thresholds (Readout & Trigger) to the common THRESHOLD\n\n");
  printf("%s TYPE THRESHOLD\n",progName);
  printf("      - Set threshold of TYPE to the common THRESHOLD\n\n");
  printf("              Where TYPE =  1  for Readout\n");
  printf("                            2  for Trigger\n");
  printf("\n\n");
}
