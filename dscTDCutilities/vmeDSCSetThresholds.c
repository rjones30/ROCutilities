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

int channel_threshold[21][16] = {{0}};

void read_threshold_cnf(char *filename);

int main(int argc, char *argv[]) 
{

  int stat;
  int thres_type = 0;
  int common_threshold=0;
  char *endp;

  printf("\nJLAB vmeDSC Set Threshold\n");
  printf("----------------------------\n");

  progName = argv[0];

  /* Parse command line arguments */
  switch(argc)
    {
    case 2: /* One threshold to rule them all */
      {
	thres_type = 0;
	common_threshold = strtoll(argv[1],&endp,10);
	if (*endp != 0)
	  {
	    read_threshold_cnf(argv[1]);
	    common_threshold = 0x10000;
	  }
	break;
      }
      
    case 3:
      {
	thres_type = atoi(argv[1]);
	common_threshold = strtoll(argv[2],&endp,10);
	if (*endp != 0)
	  {
	    read_threshold_cnf(argv[2]);
	    common_threshold = 0x10000;
	  }
	break;
      }

    default:
      printf(" Incorrect number of arguments..\n");
      Usage();
      goto CLOSE;
    }

  /* Check user input */
  if ((thres_type < 0) | (thres_type > 2) )
    {
      printf(" Incorrect threshold type..\n");
      Usage();
      goto CLOSE;
    }
  
  vmeSetQuietFlag(1);
  if (vmeOpenDefaultWindows() != OK)
    {
      printf(" Failed to access VME bridge\n");
      goto CLOSE;
    }

  extern int Ndsc;
  int idsc=0;
  int chan=0;
  int slot=0;
  int iFlag=0;
  char *type_name[3] = { "ALL", "TDC", "TRG" };

  iFlag = (1<<16); /* Do not attempt to initialize the module */

  printf(" Locating DSC in the crate...\n");
  stat = vmeDSCInit((2<<19),(1<<19),20,iFlag);
  if (stat == ERROR)
    {
      goto CLOSE;
    }

  switch(thres_type)
    {
      case 0: /* All of 'em */
	if (*endp == 0)
	  {
	    printf(" Will set all (TDC and TRG) Thresholds to %d\n",common_threshold);
	    vmeDSCSetBipolarThresholdAll((UINT16) common_threshold, (UINT16) common_threshold);
	  }
	else
	  {
	    printf(" Will set all (TDC and TRG) Thresholds to individual levels\n");
	    for (idsc=0; idsc<Ndsc; idsc++)
	      {
		slot = vmeDSCSlot(idsc);
		printf("slot %d:", slot);
		for (chan=0; chan < 16; chan++)
		  {
		    vmeDSCSetBipolarThreshold(slot, chan, channel_threshold[slot][chan], thres_type);
		    printf("%6d", channel_threshold[slot][chan]);
		  }
		printf("\n");
	      }
	  }
	break;

      case 1: /* TDC */
      case 2: /* TRG */
      default: 
	if (*endp == 0)
	  {
	    printf(" Will set %s Thresholds to %d\n",type_name[thres_type],common_threshold);
	    for(idsc=0; idsc<Ndsc; idsc++)
	      {
		slot = vmeDSCSlot(idsc);
		for (chan=0; chan < 16; chan++)
		  {
		    vmeDSCSetBipolarThreshold(slot, chan, common_threshold, thres_type);
		  }
	      }
	  }
	else
	  {
	    printf(" Will set %s Thresholds to individual levels\n", type_name[thres_type]);
	    for(idsc=0; idsc<Ndsc; idsc++)
	      {
		slot = vmeDSCSlot(idsc);
		printf("slot %d:", slot);
		for (chan=0; chan < 16; chan++)
		  {
		    vmeDSCSetBipolarThreshold(slot, chan, channel_threshold[slot][chan], thres_type);
		    printf("%6d", channel_threshold[slot][chan]);
		  }
		printf("\n");
	      }
	  }
      }

 CLOSE:

  vmeCloseDefaultWindows();

  exit(0);
}


void Usage()
{
  printf("\nUSAGE:\n\n");
  printf("%s THRESHOLD\n",progName);
  printf("      - Set all thresholds (TDC & TRG) to the common THRESHOLD\n\n");
  printf("%s TYPE THRESHOLD\n",progName);
  printf("      - Set all thresholds of TYPE to the common THRESHOLD\n\n");
  printf("              Where TYPE =  1  for TDC\n");
  printf("                            2  for TRG\n");
  printf("%s <filename.cnf>\n",progName);
  printf("      - Set all thresholds (TDC & TRG) to thresholds read from a file\n\n");
  printf("%s TYPE <filename.cnf>\n",progName);
  printf("      - Set all thresholds of TYPE to thresholds read from a file\n\n");
  printf("              Where TYPE =  1  for TDC\n");
  printf("                            2  for TRG\n");
  printf("\n\n");
}

void read_threshold_cnf(char *filename)
{
  char line[500];
  FILE *cnf = fopen(filename, "r");
  if (cnf == 0)
    {
      Usage();
      exit(1);
    }
  while (fgets(line, 500, cnf))
    {
      char crate[80];
      int thres[16];
      int slot;
      int chan;
      if (sscanf(line, "CRATE %s", crate))
	{
	  if (strcmp(crate, "roctagm2"))
	    {
	      printf("invalid input file %s\n", filename);
	      exit(1);
	    }
	}
      else if (sscanf(line, "DSC2_SLOTS %d", &slot))
	  continue;
      else if (sscanf(line, "DSC2_ALLCH_THR %d %d %d %d %d %d %d %d"
                                          " %d %d %d %d %d %d %d %d",
                            thres, thres+1, thres+2, thres+3,
                            thres+4, thres+5, thres+6, thres+7,
                            thres+8, thres+9, thres+10, thres+11,
                            thres+12, thres+13, thres+14, thres+15))
	{
	  for (chan=0; chan < 16; ++chan)
	    {
	      channel_threshold[slot][chan] = thres[chan];
	    }
	}
    }
}
