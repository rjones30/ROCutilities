/*
 * File:
 *    vmeDSCUpdateFirmware.c
 *
 * Description:
 *    Program to update the firmware on the vmeDSC
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "vmeDSClib.h"

void Usage();
char *progName;

int 
main(int argc, char *argv[]) 
{

  int stat;
  int crateUpdate=0;
  char *dat_filename;
  unsigned int dsc_address=0xffffffff;
  int ndsc=1;

  printf("\nvmeDSC Firmware Updater\n");
  printf("----------------------------\n");

  vmeOpenDefaultWindows();

  /* Check commandline arguments */
  if(argc<3)
    {
      printf(" ERROR: Must specify two arguments\n");
      Usage();
      exit(-1);
    }
  else
    {
      dat_filename = argv[1];
      dsc_address = (unsigned int) strtoll(argv[2],NULL,16)&0xffffffff;
    }

  /* Check VME address */
  if(dsc_address == 0)
    {
      crateUpdate=1;
      dsc_address = (2<<19);  /* Slot 2 address: 0x100000 */
      ndsc=20;                /* Update up to 20 DSCs */
    }
  else if (dsc_address == 0xFFFFFF)
    {
      printf("  Invalid VME address (0x%x)\n",dsc_address);
      Usage();
      exit(-1);
    }
    
  /* Initialize the library */
  int iFlag=0;
  iFlag |= (1<<18);   /* Allow for non-supported firmware in library */
  iFlag |= (1<<19);   /* Module indices in library are over order initialized
		         Instead of over Slot Number */

  vmeDSCInit(dsc_address, (1<<19), ndsc, iFlag);

  extern int Ndsc; /* Library count of initialized DSCs */
  int idsc;

  /* Show the status of all DSCs found */
  vmeDSCGStatus(0);

  /* Update the firmware */
  stat= vmeDSCUpdateFirmwareAll(dat_filename);
  if(stat != OK)
    {
      printf(" **** Firmware Update Failed ****\n");
      goto CLOSE;
    }

  /* Show serial number */
  for(idsc=0; idsc<Ndsc; idsc++)
    {
      vmeDSCFlashPrintSerialInfo(idsc);
    }


 CLOSE:

  vmeCloseDefaultWindows();

  exit(0);
}


void
Usage()
{
  printf("\n");
  printf("%s <firmware .dat file> <DSC VME ADDRESS>\n",progName);
  printf("\n");
  printf("   <DSC VME ADDRESS> :  - 6 digit (hex) A24 address of a single module\n");
  printf("                        - 0 to update all DSC in crate (all modules\n");
  printf("                          must use slot<<19 as their A24 address).\n");
  printf("\n");

}
