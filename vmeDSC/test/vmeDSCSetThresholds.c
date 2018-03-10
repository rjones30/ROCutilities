/*
 * File:
 *    vmeDSCSetThresholds.c
 *  
 *
 * Description:
 *    Find all of the DSC in the crate, and set all of their channel
 *    thresholds to a specified value from the command line.
 *
 *  Bryan Moffit - October 2014
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "vmeDSClib.h"

char *progName;
void Usage();

int 
main(int argc, char *argv[]) 
{

  int stat;
  int thres_type = 0;
  int common_threshold=0;

  printf("\nJLAB vmeDSC Set Threshold\n");
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
  
  //if((common_threshold<0) | (common_threshold>0xffff))
  if((common_threshold>0xffff))
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


  extern int Ndsc;
  int idsc=0, ich=0, DSC_SLOT=0;
  int iFlag=0;
  char *type_name[3] = { "ALL", "TDC", "TRG" };

  iFlag = (1<<16); /* Do not attempt to initialize the module */

  printf(" Locating DSC in the crate...\n");
  stat = vmeDSCInit((2<<19),(1<<19),20,iFlag);
  if(stat==ERROR)
    {
      goto CLOSE;
    }

  switch(thres_type)
    {
    case 0: /* All of 'em */
      printf(" Will set all (TDC and TRG) Thresholds to %d\n",common_threshold);
      //vmeDSCSetThresholdAll((UINT16) common_threshold, (UINT16) common_threshold);
	  vmeDSCSetBipolarThresholdAll((UINT16) common_threshold, (UINT16) common_threshold);
      break;

    case 1: /* TDC */
    case 2: /* TRG */
    default: 
      printf(" Will set %s Thresholds to %d\n",type_name[thres_type],common_threshold);
      for(idsc=0; idsc<Ndsc; idsc++)
	{
	  DSC_SLOT = vmeDSCSlot(idsc);
	  for(ich=0; ich<16; ich++)
	    {
			//vmeDSCSetThreshold(DSC_SLOT, ich, common_threshold, thres_type);
			vmeDSCSetBipolarThreshold(DSC_SLOT, ich, common_threshold, thres_type);
	    }
	}
      
    }

 CLOSE:

  vmeCloseDefaultWindows();

  exit(0);
}


void
Usage()
{
  printf("\nUSAGE:\n\n");
  printf("%s THRESHOLD\n",progName);
  printf("      - Set all thresholds (TDC & TRG) to the common THRESHOLD\n\n");
  printf("%s TYPE THRESHOLD\n",progName);
  printf("      - Set all thresholds of TYPE to the common THRESHOLD\n\n");
  printf("              Where TYPE =  1  for TDC\n");
  printf("                            2  for TRG\n");
  printf("\n\n");
}
