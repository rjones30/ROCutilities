/*
 * File:
 *    vmeDSCLibReadoutTest.c
 *
 * Description:
 *    Test Readout with the vmeDSC Library
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

  /* Setup Address and data modes for DMA transfers
   *   
   *  vmeDmaConfig(addrType, dataType, sstMode);
   *
   *  addrType = 0 (A16)    1 (A24)    2 (A32)
   *  dataType = 0 (D16)    1 (D32)    2 (BLK32) 3 (MBLK) 4 (2eVME) 5 (2eSST)
   *  sstMode  = 0 (SST160) 1 (SST267) 2 (SST320)
   */
  vmeDmaConfig(2,5,1);

  /* INIT dmaPList */

  dmaPFreeAll();
  vmeIN  = dmaPCreate("vmeIN",10244,500,0);
  vmeOUT = dmaPCreate("vmeOUT",0,0,0);
    
  dmaPStatsAll();

  dmaPReInitAll();


  extern int Ndsc;
  int idsc=0, ichan=0, DSC_SLOT=0;
  
  stat = vmeDSCInit((3<<19),(1<<19),20,0);

  /* Configure modules */
  for(idsc=0; idsc<Ndsc; idsc++)
    {
      DSC_SLOT = vmeDSCSlot(idsc);
      vmeDSCClear(DSC_SLOT);
      vmeDSCSetGateSource(DSC_SLOT, 1, DSC_GATESRC_CONSTANT);
      vmeDSCSetGateSource(DSC_SLOT, 2, DSC_GATESRC_CONSTANT);

      vmeDSCReadoutConfig(DSC_SLOT, 
			  DSC_READOUT_TRG_GRP1 | DSC_READOUT_REF_GRP1 | DSC_READOUT_LATCH_GRP1, 
			  DSC_READOUT_TRIGSRC_SOFT);
    }
  vmeDSCSetThresholdAll(10, 10);
  vmeDSCGStatus(0);



  /* Acquire the data */
  GETEVENT(vmeIN,0);

  for(idsc = 0; idsc<Ndsc; idsc++)
    {
      DSC_SLOT = vmeDSCSlot(idsc);
      vmeDSCTestPulse(DSC_SLOT, 0x102);
    }

  int dCnt=0, iwait=0, ready=0, i=0;
  unsigned int rflag = 0;

  for(idsc = 0; idsc<Ndsc; idsc++)
    {
      /* Software trigger */
      vmeDSCSoftTrigger(vmeDSCSlot(idsc));
    }

  for(idsc = 0; idsc<Ndsc; idsc++)
    {
      DSC_SLOT = vmeDSCSlot(idsc);

      /* Check for data ready */
      for(i=0; i<100; i++)
	{
	  ready = vmeDSCDReady(DSC_SLOT);
	  if(ready)
	    break;
	}

      if(ready)
	{
	  printf("%02d: Ready = %d\n",DSC_SLOT,ready);
	  dCnt = vmeDSCReadBlock(DSC_SLOT, dma_dabufp, 100, 1);
	  if(dCnt>0)
	    {
	      printf("%2d: dCnt = %d\n",DSC_SLOT,dCnt);
	      dma_dabufp += dCnt;
	    }
	  else
	    {
	      printf("ERROR: No data from DSC ID %02d\n",DSC_SLOT);
	    }
	}
      else
	{
	  printf("ERROR: Data not ready from DSC ID %02d\n",DSC_SLOT);
	}
    }


  PUTEVENT(vmeOUT);

  
  /* Look at the acquired data */
  DMANODE *outEvent = dmaPGetItem(vmeOUT);
  int idata;

  printf("outevent length = %d\n",outEvent->length);
  for(idata=0;idata<outEvent->length;idata++)
    {
      if((idata%4)==0) printf("\n\t");
      printf("  0x%08x ",(unsigned int)LSWAP(outEvent->data[idata]));
    }
  printf("\n\n");

  dmaPFreeItem(outEvent);

 CLOSE:

  dmaPFreeAll();
  vmeCloseDefaultWindows();

  exit(0);
}

