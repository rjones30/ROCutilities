/*************************************************************************
 *
 *  vme_list.c  - Library of routines for the user to write for
 *                readout and buffering of events using
 *                a Linux VME controller.
 *
 */

/* Event Buffer definitions */
#define MAX_EVENT_POOL     400
#define MAX_EVENT_LENGTH   1024*10      /* Size in Bytes */

/* Define Interrupt source and address */
#define TIR_SOURCE
#define TIR_ADDR 0x0ed0
/* TIR_MODE:  0 : interrupt on trigger,
              1 : interrupt from Trigger Supervisor signal
              2 : polling for trigger
              3 : polling for Trigger Supervisor signal  */
#define TIR_MODE 0

#include "linuxvme_list.c" /* source required for CODA */
#include "vmeDSClib.h"     /* library of routines for the vmeDSC */
#include "c1190Lib.h"

/* CAEN 1190/1290 specific definitions */
#define NUM_V1190 1

extern unsigned long long int dma_timer[10];

/* function prototype */
void rocTrigger(int arg);

void
rocDownload()
{

  /* Setup Address and data modes for DMA transfers
   *   
   *  vmeDmaConfig(addrType, dataType, sstMode);
   *
   *  addrType = 0 (A16)    1 (A24)    2 (A32)
   *  dataType = 0 (D16)    1 (D32)    2 (BLK32) 3 (MBLK) 4 (2eVME) 5 (2eSST)
   *  sstMode  = 0 (SST160) 1 (SST267) 2 (SST320)
   */
  vmeDmaConfig(2,5,2); 

  printf("rocDownload: User Download Executed\n");

}

void
rocPrestart()
{
  unsigned short iflag;
  int stat, itdc;
  unsigned long long time0, time1, time2;

  /* PRogram/Init VME Modules Here */
  /* Program/Init VME Modules Here */
  /* INIT C1190/C1290 - Must be A32 for 2eSST */
  UINT32 list[NUM_V1190] = {0x08270000};
  
  tdc1190InitList(list,NUM_V1190,1);
  /* Another way to do the same thing */
  /*   tdc1190Init(list[0],0x10000,NUM_V1190,1); */
  for(itdc=0; itdc<NUM_V1190; itdc++) 
    {
      tdc1190SetTriggerMatchingMode(itdc);
      tdc1190SetEdgeResolution(itdc,100);
      tdc1190EventFifo(itdc,0);
      tdc1190BusError(itdc,1);
      tdc1190Align64(itdc,1);
    }
  /* Disable some noisy channels */
/*   tdc1190DisableChannel(1,12); */
/*   tdc1190DisableChannel(1,13); */

  for(itdc=0; itdc<NUM_V1190; itdc++) 
    tdc1190Status(itdc);



  dscInit(0xbe0000,0,1);
  dscStatus(0,0);

  dscSetThreshold(0,0,550,TDCTRG);
  dscSetThreshold(0,14,750,TDCTRG);
  dscSetThreshold(0,15,750,TDCTRG);
  dscSetChannelMask(0,0xffff,TRG);
  dscSetChannelMask(0,0xffff,TDC);
  dscSetChannelORMask(0,0xffff,TDCTRG);

  time0 = rdtsc();
  sleep(1);
  time1 = rdtsc();

  printf("usleep(1) ticks = %lld\n",time1-time0);


  printf("rocPrestart: User Prestart Executed\n");

}

void
rocGo()
{
  /*  Enable Modules here */
  dscLatchScalers(0,2); /* equivalent to a clear */

}

void
rocEnd()
{
  int itdc;

  /* Disable Modules here */
  dscStatus(0,0);

  /* 1190/1290 Status  - FIXME: Reset too? */
  for(itdc=0; itdc<NUM_V1190; itdc++) 
    tdc1190Status(itdc);

  printf("rocEnd: Ended after %d events\n",tirGetIntCount());
  
}

void
rocTrigger(int arg)
{
  int ii, status, dma, count;
  int nwords;
  unsigned int datascan, tirval, vme_addr;
  int length,size;
  unsigned long long time0, time1, time2, time3;

  tirIntOutput(1); /* Stop the gate to vmeDSC */

  tirval = tirIntType();

  *dma_dabufp++ = LSWAP(0xffdaf000); /* TI data header */
  *dma_dabufp++ = LSWAP(tirval);

  /* Check for valid data here */
  for(ii=0;ii<100;ii++) 
    {
      datascan = tdc1190Dready(0);
      if (datascan>0) 
	{
	  break;
	}
    }

  if(datascan>0) 
    {
      /* Get the TDC data from all modules... rflag=2 for Linked List DMA 
	 "64" is ignored in Linux */
      time0 = rdtsc();
      nwords = tdc1190ReadBlock(0,dma_dabufp,64,1);
      time1 = rdtsc();
    
      if(nwords < 0) 
	{
	  printf("ERROR: in transfer (event = %d), status = 0x%x\n", tirGetIntCount(),nwords);
	  *dma_dabufp++ = LSWAP(0xda000bad);
	} 
      else 
	{
	  dma_dabufp += nwords;
	}
    } 
  else 
    {
      printf("ERROR: Data not ready in event %d\n",tirGetIntCount());
      *dma_dabufp++ = LSWAP(0xda000bad);
    }

  for(ii=0; ii<1000; ii++)
    {
      tirval=tirIntType();
    }

  time2 = rdtsc();

  for(ii=0; ii<1000; ii++)
    {
      tirIntOutput(1); /* Stop the gate to vmeDSC */
    }

  time3 = rdtsc();

  /* Insert some data here */
  dscLatchScalers(0,2); /* latch both scalers */
  nwords = dscReadScalers(0,dma_dabufp,20,DSC_READOUT_TRGVME|DSC_READOUT_REF|DSC_READOUT_REFVME);
  dma_dabufp+=nwords;

  *dma_dabufp++ = LSWAP(0xcebaf009);
  *dma_dabufp++ = LSWAP((unsigned int)(time1-time0));
  *dma_dabufp++ = LSWAP((unsigned int)(time2-time1));
  *dma_dabufp++ = LSWAP((unsigned int)(time3-time2));
  *dma_dabufp++ = LSWAP((unsigned int)(dma_timer[0]-time0));
  *dma_dabufp++ = LSWAP((unsigned int)(dma_timer[1]-time0));
  *dma_dabufp++ = LSWAP((unsigned int)(dma_timer[2]-time0));
  *dma_dabufp++ = LSWAP((unsigned int)(dma_timer[3]-time0));
  *dma_dabufp++ = LSWAP((unsigned int)(dma_timer[4]-time0));
  *dma_dabufp++ = LSWAP((unsigned int)(dma_timer[5]-time0));

  *dma_dabufp++ = LSWAP(0xda0000ff);

/*   for(ii=0;ii<2000;ii++) */
/*     { */
/*       *dma_dabufp++ = LSWAP(0xda000000+ii); */
/*     } */

  tirIntOutput(0); 
}
