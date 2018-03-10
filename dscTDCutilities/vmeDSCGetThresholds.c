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
  int thres_type = 1;

  printf("\nJLAB vmeDSC Set Threshold\n");
  printf("----------------------------\n");

  progName = argv[0];

  vmeSetQuietFlag(1);
  if(vmeOpenDefaultWindows()!=OK)
    {
      printf(" Failed to access VME bridge\n");
      //goto CLOSE;
	  vmeCloseDefaultWindows();
	  exit(0);
    }


  extern int Ndsc;
  int idsc=0, ich=0, DSC_SLOT=0;
  int iFlag=0;

  iFlag = (1<<16); /* Do not attempt to initialize the module */

  printf(" Locating DSC in the crate...\n");
  stat = vmeDSCInit((2<<19),(1<<19),20,iFlag);
  if(stat!=ERROR) {
      for(idsc=0; idsc<Ndsc; idsc++) {
		  DSC_SLOT = vmeDSCSlot(idsc);
		  printf("slot %2i ",idsc);
		  for(ich=0; ich<16; ich++)
			  {
				  int bipthreshold = vmeDSCGetBipolarThreshold(DSC_SLOT, ich, thres_type);
				  //int threshold = vmeDSCGetThreshold(DSC_SLOT, ich, thres_type);
				  //printf(" %d %d ",threshold,bipthreshold);
				  printf(" %5d ",bipthreshold);
			  }
		  printf("\n");
	  }
  }
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
