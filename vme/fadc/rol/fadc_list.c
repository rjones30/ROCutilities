/*************************************************************************
 *
 *  fadc_list.c - Library of routines for the user to write for
 *                readout and buffering of events from JLab FADC using
 *                a JLab TIR module and Linux VME controller.
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
#define TIR_MODE 2

#include "linuxvme_list.c"  /* source required for CODA */
#include "fadcLib.h"        /* library of FADC250 routines */

/* FADC Library Variables */
extern int fadcA32Base;
int FA_SLOT;
extern int fadcID[20];
#define FADC_ADDR 0xfd0000

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
  vmeDmaConfig(2,5,1); 

  printf("rocDownload: User Download Executed\n");

}

void
rocPrestart()
{
  unsigned short iflag;
  int stat;

  /* Program/Init FADC Modules Here */
  iflag = 0xea00; /* SDC Board address */
  iflag |= 1<<0;  /* Front panel sync-reset */
  iflag |= 1<<1;  /* Front Panel Input trigger source */
  iflag |= 0<<4;  /* Internal 250MHz Clock source */
  /*   iflag |= 1<<4;  /\* Front Panel 250MHz Clock source *\/ */
  printf("iflag = 0x%x\n",iflag);

/*   fadcA32Base = 0x08000000; */

  faInit(FADC_ADDR,0x0,1,iflag);
  FA_SLOT = fadcID[0];

  /*      Setup FADC Programming */
  faSetBlockLevel(FA_SLOT,1);
  /*  for Block Reads */
  faEnableBusError(FA_SLOT);
  /*  for Single Cycle Reads */
  /*       faDisableBusError(FA_SLOT); */
  
  /*  Set All channel thresholds to 0 */
  faSetThreshold(FA_SLOT,0,0xffff);
  
    
  /*  Setup option 1 processing - RAW Window Data     <-- */
  /*        option 2            - RAW Pulse Data */
  /*        option 3            - Integral Pulse Data */
  /*  Setup 200 nsec latency (PL  = 50)  */
  /*  Setup  80 nsec Window  (PTW = 20) */
  /*  Setup Pulse widths of 36ns (NSB(3)+NSA(6) = 9)  */
  /*  Setup up to 1 pulse processed */
  /*  Setup for both ADC banks(0 - all channels 0-15) */
  faSetProcMode(FA_SLOT,1,50,20,3,6,1,0);
  
  faClear(FA_SLOT);

  faStatus(FA_SLOT,0);

  faSDC_Config(1,0);

  faSDC_Status(0);

  printf("rocPrestart: User Prestart Executed\n");

}

void
rocGo()
{
  /*  Enable FADC */
  faEnable(FA_SLOT,0,0);

  taskDelay(1);
  
  /*  Send Sync Reset to FADC */
  /*     faSync(FA_SLOT); */
  faSDC_Sync();

  /* Interrupts/Polling enabled after conclusion of rocGo() */
}

void
rocEnd()
{

  /* FADC Disable */
  faDisable(FA_SLOT,0);

  /* FADC Event status - Is all data read out */
  faStatus(FA_SLOT,0);

  faReset(FA_SLOT,0);

  printf("rocEnd: Ended after %d events\n",tirGetIntCount());
  
}

void
rocTrigger(int arg)
{
  int ii, nwords;
  unsigned int datascan;

  /* Insert trigger count  - Make sure bytes are ordered little-endian (LSWAP)*/
  *dma_dabufp++ = LSWAP(tirGetIntCount());

  /* Check for valid data here */
  for(ii=0;ii<100;ii++) 
    {
      datascan = faBready(FA_SLOT);
      if (datascan>0) 
	{
	  break;
	}
    }

  if(datascan>0) 
    {
      nwords = faReadBlock(FA_SLOT,dma_dabufp,500,1);
    
      if(nwords < 0) 
	{
	  printf("ERROR: in transfer (event = %d), nwords = 0x%x\n", tirGetIntCount(),nwords);
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

  *dma_dabufp++ = LSWAP(0xda0000ff); /* Event EOB */

}
