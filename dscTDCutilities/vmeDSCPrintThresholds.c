//
// vmeDSCPrintThresholds.c
//  
// author: richard.t.jones at uconn.edu
// version: january 27, 2018
//
// Based on vmeDSCGetThresholds.c by Bryan Moffit - October 2014
//

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "vmeDSClib.h"

char *progName;
void Usage();

int main(int argc, char *argv[]) 
{
  int stat;
  int thres_type = 1;

  printf("\nJLAB vmeDSC Thresholds\n");
  printf("----------------------------\n");

  progName = argv[0];

  vmeSetQuietFlag(1);
  if (vmeOpenDefaultWindows() != OK) {
    printf(" Failed to access VME bridge\n");
    vmeCloseDefaultWindows();
    exit(0);
  }

  extern int Ndsc;
  int idsc=0;
  int ich=0;
  int dsc_slot=0;
  int iFlag=(1<<16); /* Do not attempt to initialize the module */

  printf(" Locating DSC in the crate...\n");
  stat = vmeDSCInit((2<<19), (1<<19), 20, iFlag);
  if (stat != ERROR) {
    printf(" TDC thresholds:\n");
    thres_type = 1;
    for (idsc=0; idsc<Ndsc; idsc++) {
      dsc_slot = vmeDSCSlot(idsc);
      printf("slot %2i ",dsc_slot);
      for (ich=0; ich<16; ich++) {
        int threshold = vmeDSCGetBipolarThreshold(dsc_slot, ich, thres_type);
        printf(" %5d ",threshold);
      }
      printf("\n");
    }
    printf(" TRG thresholds:\n");
    thres_type = 2;
    for(idsc=0; idsc<Ndsc; idsc++) {
      dsc_slot = vmeDSCSlot(idsc);
      printf("slot %2i ",dsc_slot);
      for (ich=0; ich<16; ich++) {
        int threshold = vmeDSCGetBipolarThreshold(dsc_slot, ich, thres_type);
        printf(" %5d ",threshold);
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
  printf("%s\n",progName);
  printf("\n\n");
}
