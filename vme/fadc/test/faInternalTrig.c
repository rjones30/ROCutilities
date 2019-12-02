/*
 * File:
 *    faInternalTrig.c
 *
 * Description:
 *    Test self triggering in the fADC250-V2
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "jvme.h"
#include "flexioLib.h"
#include "fadcLib.h"

/* Event Buffer definitions */
#define MAX_EVENT_POOL     100
#define MAX_EVENT_LENGTH   1024*2000      /* Size in Bytes */

DMA_MEM_ID vmeIN,vmeOUT;
extern DMANODE *the_event;
extern unsigned int *dma_dabufp;

int faMode=1;
#define FADC_WINDOW_LAT       50  /* Trigger Window Latency */
#define FADC_WINDOW_WIDTH     46  /* Trigger Window Width */
#define FADC_DAC_LEVEL      3250  /* Internal DAC Level */
#define FADC_THRESHOLD        50  /* Threshold for data readout */
int FA_SLOT=0;
extern   int fadcA32Base;           /* This will need to be reset from it's default
                                     * so that it does not overlap with the TID */
extern   int nfadc;                 /* Number of FADC250s verified with the library */
extern   int fadcID[FA_MAX_BOARDS]; /* Array of slot numbers, discovered by the library */
unsigned int MAXFADCWORDS = 0;
unsigned int fadcSlotMask   = 0;    /* bit=slot (starting from 0) */
int NFADC=0;

int faStopped=1;
int intCount=0;

/* polling thread pthread and pthread_attr */
pthread_attr_t fapollthread_attr;
pthread_t      fapollthread;

#define BLOCKLEVEL 1

#define DO_READOUT

void myISR(int arg);

void
faPoll(void)
{
  unsigned int faReady=0;
  int stat;

  printf("%s: I'm in\n",__FUNCTION__);

  while(1)
    {
      pthread_testcancel();
      
      /* If still need Ack, don't test the Trigger Status */
      if(faStopped>0) 
	{
	  continue;
	}

      faReady = faGBready();
      stat = (faReady == fadcSlotMask);

      if(stat>0)
	{
	  vmeBusLock(); 
	  intCount++;

	  myISR(intCount);

	  vmeBusUnlock();
	}

    }

  printf("%s: Read ERROR: Exiting Thread\n",__FUNCTION__);
  pthread_exit(0);

}
/* Interrupt Service routine */
void
myISR(int arg)
{
  volatile unsigned short reg;
  int dCnt, len=0,idata;
  DMANODE *outEvent;
  int tibready=0, timeout=0;
  int printout = 1;
  int iev=0;

  unsigned int tiIntCount = arg;

  GETEVENT(vmeIN,tiIntCount);

    /* Readout FADC */
  if(NFADC!=0)
    {
      FA_SLOT = fadcID[0];
      int itime=0, stat=0, roflag=1, islot=0;
      unsigned int gbready=0;
/*       for(itime=0;itime<10000;itime++)  */
/* 	{ */
/* 	  gbready = faGBready(); */
/* 	  stat = (gbready == fadcSlotMask); */
/* 	  if (stat>0)  */
/* 	    { */
/* 	      break; */
/* 	    } */
/* 	} */
/*       if(stat>0)  */
	{
	  if(NFADC>1) roflag=2; /* Use token passing scheme to readout all modules */
	  dCnt = faReadBlock(FA_SLOT,dma_dabufp,MAXFADCWORDS,roflag);
	  if(dCnt<=0)
	    {
	      printf("FADC%d: No data or error.  dCnt = %d\n",FA_SLOT,dCnt);
	    }
	  else
	    {
	      if(dCnt>MAXFADCWORDS)
		{
		  printf("%s: WARNING.. faReadBlock returned dCnt >= MAXFADCWORDS (%d >= %d)\n",
			 __FUNCTION__,dCnt, MAXFADCWORDS);
		}
	      else 
		dma_dabufp += dCnt;
	    }
	} 
/*       else  */
/* 	{ */
/* 	  printf ("FADC%d: no events   stat=%d  intcount = %d   gbready = 0x%08x  fadcSlotMask = 0x%08x\n", */
/* 		  FA_SLOT,stat,tiIntCount,gbready,fadcSlotMask); */
/* 	} */

      /* Reset the Token */
      if(roflag==2)
	{
	  for(islot=0; islot<NFADC; islot++)
	    {
	      FA_SLOT = fadcID[islot];
	      faResetToken(FA_SLOT);
	    }
	}
    }

  PUTEVENT(vmeOUT);

  outEvent = dmaPGetItem(vmeOUT);

#define READOUT
#ifdef READOUT
  if(tiIntCount%printout==0)
    {
      printf("Received %d triggers...\n",
	     tiIntCount);

      len = outEvent->length;
      
      for(idata=0;idata<len;idata++)
	{
	  faDataDecode(LSWAP(outEvent->data[idata]));
	}
      printf("\n\n");
    }
#endif
  dmaPFreeItem(outEvent);

  if(tiIntCount%printout==0)
    printf("intCount = %d\n",tiIntCount );

  /* Pulse flex io for next event */
  flexioWriteCsr(0, 0x101);


}


int 
main(int argc, char *argv[]) 
{

  int stat;

  printf("\nJLAB fADC250-V2 Tests\n");
  printf("----------------------------\n");

  /*   remexSetCmsgServer("dafarm28"); */
  /*   remexInit(NULL,1); */

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
  vmeIN  = dmaPCreate("vmeIN",MAX_EVENT_LENGTH,MAX_EVENT_POOL,0);
  vmeOUT = dmaPCreate("vmeOUT",0,0,0);
    
  dmaPStatsAll();

  dmaPReInitAll();

  /*     gefVmeSetDebugFlags(vmeHdl,0x0); */

  vmeSetQuietFlag(1); /* skip the errors associated with BUS Errors */

  /* Setup FlexIO */
  flexioInit(0xee0, FLEXIO_MODE_POLL);

  flexioWriteCsr(0, 0x8000); // reset
  flexioWriteCsr(0, 0x1);    // output in vme pulse mode

  /* Setup output data pattern */
  flexioWriteData(0, 0xeded);
/*   flexioWriteData(0, 0xeded); */

  /* Setup fADC250 */
  NFADC = 16+2;   /* 16 slots + 2 (for the switch slots) */
  fadcA32Base=0x09000000;

  /* Setup the iFlag.. flags for FADC initialization */
  int iFlag=0;
  /* Sync Source */
  iFlag |= (0<<0);    /* VME */
  /* Trigger Source */
  iFlag |= (4<<1);    /* Internal */
  /* Clock Source */
  iFlag |= FA_INIT_INT_CLKSRC;    /* Self */

  iFlag |= FA_INIT_SKIP_FIRMWARE_CHECK;

/*   faInit((unsigned int)(3<<19),(1<<19),NFADC,iFlag); */
  faInit(0xed0000,0,1,iFlag);
  NFADC=nfadc;        /* Redefine our NFADC with what was found from the driver */
  vmeSetQuietFlag(0); /* Turn the error statements back on */
  faItrigDisable(0, 1);
  
  MAXFADCWORDS = NFADC * BLOCKLEVEL * (1+2+FADC_WINDOW_WIDTH*16) + 3;
  
  printf("**************************************************\n");
  printf("* Calculated MAX FADC words per block = %d\n",MAXFADCWORDS);
  printf("**************************************************\n");

  
  if(NFADC>1)
    faEnableMultiBlock(1);

  if(NFADC==0)
    goto CLOSE;

  /* Additional Configuration for each module */
  fadcSlotMask=0;
  int islot=0;
  for(islot=0;islot<NFADC;islot++) 
    {
      FA_SLOT = fadcID[islot];      /* Grab the current module's slot number */
      fadcSlotMask |= (1<<FA_SLOT); /* Add it to the mask */

      /*       faDataInsertAdcParameters(FA_SLOT,1); */
      /*       faDataSuppressTriggerTime(FA_SLOT,2); */
      /* Set the internal DAC level */
      faSetDAC(FA_SLOT,FADC_DAC_LEVEL,0);
      /* Set the threshold for data readout */
      faSetThreshold(FA_SLOT,FADC_THRESHOLD,0);
	
      /*  Setup option 1 processing - RAW Window Data     <-- */
      /*        option 2            - RAW Pulse Data */
      /*        option 3            - Integral Pulse Data */
      /*  Setup 200 nsec latency (PL  = 50)  */
      /*  Setup  80 nsec Window  (PTW = 20) */
      /*  Setup Pulse widths of 36ns (NSB(3)+NSA(6) = 9)  */
      /*  Setup up to 1 pulse processed */
      /*  Setup for both ADC banks(0 - all channels 0-15) */
      /* Integral Pulse Data */
      faSetProcMode(FA_SLOT,faMode,FADC_WINDOW_LAT,FADC_WINDOW_WIDTH,3,6,3,0);
	
      /* Bus errors to terminate block transfers (preferred) */
      faEnableBusError(FA_SLOT);
      /* Set the Block level */
      faSetBlockLevel(FA_SLOT,BLOCKLEVEL);

      /* Set the individual channel pedestals for the data that is sent
       * to the CTP
       */
      int ichan;
      for(ichan=0; ichan<16; ichan++)
	{
	  faSetChannelPedestal(FA_SLOT,ichan,0);
	}

      faSetTriggerPathThreshold(FA_SLOT, 500);
    }

  /************************************************************
   *
   *  Setup Internal Triggering
   *   
   *   Four Modes of Operation (tmode)
   *     0) Table Mode
   *     1) Coincidence Mode
   *     2) Window Mode
   *     3) INVALID
   *     4) Sum Mode
   *
   *   wMask     = Mask of 16 channels to be enabled for Window Mode
   *   wWidth    = Width of trigger window before latching (in clocks)
   *   cMask     = Mask of 16 channels to be enabled for Coincidence Mode
   *   sumThresh = 10-12 bit threshold for Sum trigger to be latched 
   *   tTable    = pointer to trigger table (65536 values) to be loaded
   */
  int tmode=0;
  unsigned int wWidth=5, wMask=0x0, cMask=0xeded, sumThresh=0, tTable[65536];

  memset((char *)tTable,0,65536*sizeof(unsigned int));

  tTable[0xeded] = 1;

  faItrigSetMode(FA_SLOT, tmode, wWidth, wMask,
		 cMask, sumThresh, (uint32_t *)tTable);
    
  faItrigSetHBwidth(0, 2, 0xffff);
  faGStatus(0);

  /* FADC Perform some resets, status */
  for(islot=0;islot<NFADC;islot++) 
    {
      FA_SLOT = fadcID[islot];
      /*       faSetClockSource(FA_SLOT,2); */
      faClear(FA_SLOT);
      faResetToken(FA_SLOT);
      faResetTriggerCount(FA_SLOT);
    }

  faGStatus(0);
  faItrigStatus(0,0);
  faItrigPrintHBinfo(0);

  /*  Enable FADC */
  for(islot=0;islot<NFADC;islot++) 
    {
      FA_SLOT = fadcID[islot];
      faEnableSyncSrc(FA_SLOT);
    }

  faSync(0);

  taskDelay(1);

/*   faGStatus(0); */

  int pti_status;
  void *res;

  pti_status = 
    pthread_create(&fapollthread,
		   NULL,
		   (void*(*)(void *)) faPoll,
		   (void *)NULL);
  if(pti_status!=0) 
    {						
      printf("%s: ERROR: FA Polling Thread could not be started.\n",
	     __FUNCTION__);	
      printf("\t pthread_create returned: %d\n",pti_status);
    }

  printf("Hit enter to start triggers\n");
  getchar();

  faGEnable(1,0);
  faItrigEnable(0, 1);
  faItrigStatus(0,0);
  faStopped=0;
  flexioWriteCsr(0, 0x101); // send first pulse


  printf("Hit any key to Disable TID and exit.\n");
  getchar();

  vmeBusLock();
  /* FADC Disable */
  faGDisable(1);

  faStopped=1;
  vmeBusUnlock();
  if(fapollthread) 
    {
      if(pthread_cancel(fapollthread)<0) 
	perror("pthread_cancel");
      if(pthread_join(fapollthread,&res)<0)
	perror("pthread_join");
      if (res == PTHREAD_CANCELED)
	printf("%s: Polling thread canceled\n",__FUNCTION__);
      else
	printf("%s: ERROR: Polling thread NOT canceled\n",__FUNCTION__);
    }




  faGStatus(0);
  faPrintAuxScal(0);

 CLOSE:

  dmaPFreeAll();
  vmeCloseDefaultWindows();

  exit(0);
}

