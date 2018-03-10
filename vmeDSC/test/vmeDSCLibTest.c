/*
 * File:
 *    tiLibTest.c
 *
 * Description:
 *    Test Vme TI interrupts with GEFANUC Linux Driver
 *    and TI library
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


int 
main(int argc, char *argv[]) 
{

  int stat;

  printf("\nJLAB vmeDSC Library Tests\n");
  printf("----------------------------\n");


  vmeOpenDefaultWindows();


  extern int Ndsc;
  int idsc=0, ichan=0, sn=0, DSC_SLOT=0;
  
  stat = vmeDSCInit((3<<19),(1<<19),20,0);

  vmeDSCGStatus(0);

  for(idsc=0; idsc<Ndsc; idsc++)
    {
      DSC_SLOT = vmeDSCSlot(idsc);
      sn = vmeDSCGetSerialNumber(DSC_SLOT,NULL,0);
      if(sn==0)
	{
	  printf("%2d: No serial number\n",DSC_SLOT);
	}
      else
	{
	  vmeDSCFlashPrintSerialInfo(DSC_SLOT);
	  vmeDSCGetSerialNumber(DSC_SLOT,NULL,1);
	}
    }

  exit(0);


 CLOSE:

  vmeCloseDefaultWindows();

  exit(0);
}

