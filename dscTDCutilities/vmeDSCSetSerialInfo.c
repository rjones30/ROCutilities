/*
 * File:
 *    vmeDSCSetSerialInfo.c
 *
 * Description:
 *    Set the serial info for the vmeDSC
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "vmeDSClib.h"

DMA_MEM_ID vmeIN,vmeOUT;
extern DMANODE *the_event;
extern unsigned int *dma_dabufp;

char rev_slot[22] =
  {
    0, 0, 0, 0,
    'D', //4
    'D', 
    'D', //6
    'D', 
    'D', //8
    'D', 
    'D', //10
    'D', 
    'D', //12
    'D', 
    'D', //14
    'D', 
    'D', //16
    'D', 
    'D', //18
    'D', 
    'D', //20
    'D' 
  };

char mfg_slot[22][4] =
  {
    "", "", "", "",
    "ACDI", //4
    "ACDI", 
    "ACDI", //6
    "ACDI", 
    "ACDI", //8
    "ACDI", 
    "ACDI", //10
    "ACDI", 
    "ACDI", //12
    "ACDI", 
    "ACDI", //14
    "ACDI", 
    "ACDI", //16
    "ACDI", 
    "ACDI", //18
    "ACDI", 
    "ACDI", //20
    "ACDI"
  };

int sn_slot[22] =
  {
    0, 0, 0, 0,
    0, //4
    0, 
    0, //6
    0, 
    70, //8
    64, 
    47, //10
    348, 
    65, //12
    29, 
    48, //14
    59, 
    71, //16
    34, 
    30, //18
    25, 
    0, //20
    0
  };

char testDate_slot[22][100] =
  {
    "", "", "", "",
    " 2012", //4
    " 2012", 
    " 2012", //6
    " 2012", 
    "Fri Aug 24 15:26:16 2012", //8
    "Mon Jul 29 15:05:12 2013", 
    "Thu Aug 23 09:32:02 2012", //10
    " 2012", 
    "Thu Aug 23 08:43:24 2012", //12
    "Wed Aug 22 15:52:22 2012", 
    "Mon Aug 27 10:08:18 2012", //14
    "Thu Aug 23 16:34:34 2012", 
    "Thu Aug 23 10:26:49 2012", //16
    "Fri Aug 24 13:42:44 2012", 
    "Fri Aug 24 16:14:16 2012", //18
    "Tue Aug 28 11:48:39 2012", 
    " 2012", //20
    " 2012"
  };
  

int 
main(int argc, char *argv[]) 
{

  int stat;

  printf("\nJLAB vmeDSC Library Tests\n");
  printf("----------------------------\n");


  vmeOpenDefaultWindows();


  extern int Ndsc;
  int idsc=0, ichan=0, sn=0, mfg=0;
  int DSC_SLOT=0;
  
  stat = vmeDSCInit((3<<19),(1<<19),20,0);

  vmeDSCGStatus(0);

  for(idsc=0; idsc<Ndsc; idsc++)
    {
      DSC_SLOT = vmeDSCSlot(idsc);
      sn = vmeDSCGetSerialNumber(DSC_SLOT,NULL,0);
      if(sn==0)
	{
	  vmeDSCCalibrate(DSC_SLOT);
	  sleep(2);

	  vmeDSCFlashSetSerialInfo(DSC_SLOT, rev_slot[DSC_SLOT],
				   sn_slot[DSC_SLOT], mfg_slot[DSC_SLOT],
				   testDate_slot[DSC_SLOT]);
	}
      vmeDSCFlashPrintSerialInfo(DSC_SLOT);
      vmeDSCGetSerialNumber(DSC_SLOT,NULL,1);
    }


 CLOSE:

  vmeCloseDefaultWindows();

  exit(0);
}

