/*
 * File:
 *    fadcReadoutTest.c
 *
 * Description:
 *    Test readingout out the fADC250 v2 with a pipeline TI as the
 *     trigger source.
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "tiLib.h"
#include "sdLib.h"
#include "fadcLib.h"

/* Event Buffer definitions */
#define MAX_EVENT_POOL     100
#define MAX_EVENT_LENGTH   1024*2000      /* Size in Bytes */

DMA_MEM_ID vmeIN,vmeOUT;
extern DMANODE *the_event;
extern unsigned int *dma_dabufp;

extern int tiA32Base;

int faMode=1;
#define FADC_WINDOW_LAT      345  /* Trigger Window Latency */
#define FADC_WINDOW_WIDTH    24  /* Trigger Window Width */
#define FADC_DAC_LEVEL      3250  /* Internal DAC Level */
#define FADC_THRESHOLD      300  /* Threshold for data readout */
int FA_SLOT=0;
extern   int fadcA32Base;           /* This will need to be reset from it's default
                                     * so that it does not overlap with the TID */
extern   int nfadc;                 /* Number of FADC250s verified with the library */
extern   int fadcID[FA_MAX_BOARDS]; /* Array of slot numbers, discovered by the library */
unsigned int MAXFADCWORDS = 0;
unsigned int fadcSlotMask   = 0;    /* bit=slot (starting from 0) */
int NFADC=0;

#define BLOCKLEVEL 1

#define DO_READOUT

/* Interrupt Service routine */
void
mytiISR(int arg)
{
  volatile unsigned short reg;
  int dCnt, len=0,idata;
  DMANODE *outEvent;
  int tibready=0, timeout=0;
  int printout = 1/BLOCKLEVEL;

  unsigned int tiIntCount = tiGetIntCount();

#ifdef DO_READOUT
  GETEVENT(vmeIN,tiIntCount);

#ifdef DOINT
  tibready = tiBReady();
  if(tibready==ERROR)
    {
      printf("%s: ERROR: tiIntPoll returned ERROR.\n",__FUNCTION__);
      return;
    }

  if(tibready==0 && timeout<100)
    {
      printf("NOT READY!\n");
      tibready=tiBReady();
      timeout++;
    }

  if(timeout>=100)
    {
      printf("TIMEOUT!\n");
      return;
    }
#endif

  dCnt = tiReadBlock(dma_dabufp,3*BLOCKLEVEL+10,1);
  if(dCnt<=0)
    {
      printf("No data or error.  dCnt = %d\n",dCnt);
    }
  else
    {
/*       dma_dabufp += dCnt; */
      /*       printf("dCnt = %d\n",dCnt); */
    
    }


    /* Readout FADC */
  if(NFADC!=0)
    {
      FA_SLOT = fadcID[0];
      int itime=0, stat=0, roflag=1, islot=0;
      unsigned int gbready=0;
      for(itime=0;itime<10000;itime++) 
	{
	  gbready = faGBready();
	  stat = (gbready == fadcSlotMask);
	  if (stat>0) 
	    {
	      break;
	    }
	}
      if(stat>0) 
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
      else 
	{
	  printf ("FADC%d: no events   stat=%d  intcount = %d   gbready = 0x%08x  fadcSlotMask = 0x%08x\n",
		  FA_SLOT,stat,tiGetIntCount(),gbready,fadcSlotMask);
	}

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
#ifdef DATADUMP
	  if((idata%5)==0) printf("\n\t");
	  printf("  0x%08x ",(unsigned int)LSWAP(outEvent->data[idata]));
#else
	  faDataDecode(LSWAP(outEvent->data[idata]));
#endif
	}
      printf("\n\n");
    }
#endif
  dmaPFreeItem(outEvent);
#else /* DO_READOUT */
  /*   tiResetBlockReadout(); */

#endif /* DO_READOUT */
  if(tiIntCount%printout==0)
    printf("intCount = %d\n",tiIntCount );
/*     sleep(1); */

}


int 
main(int argc, char *argv[]) {

  int stat;

  printf("\nJLAB TI Tests\n");
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
  /* Set the TI structure pointer */
  /*     tiInit((2<<19),TI_READOUT_EXT_POLL,0); */
  tiA32Base=0x08000000;
  tiInit(0,TI_READOUT_EXT_POLL,0);
  tiCheckAddresses();

  char mySN[20];
  printf("0x%08x\n",tiGetSerialNumber((char **)&mySN));
  printf("mySN = %s\n",mySN);

#ifndef DO_READOUT
  tiDisableDataReadout();
  tiDisableA32();
#endif

  tiLoadTriggerTable(0);
    
  tiSetTriggerHoldoff(1,4,0);
  tiSetTriggerHoldoff(2,4,0);

  tiSetPrescale(0);
  tiSetBlockLevel(BLOCKLEVEL);

  stat = tiIntConnect(TI_INT_VEC, mytiISR, 0);
  if (stat != OK) 
    {
      printf("ERROR: tiIntConnect failed \n");
      goto CLOSE;
    } 
  else 
    {
      printf("INFO: Attached TI Interrupt\n");
    }

  /*     tiSetTriggerSource(TI_TRIGGER_TSINPUTS); */
  tiSetTriggerSource(TI_TRIGGER_PULSER);
  tiEnableTSInput(0x1);

  /*     tiSetFPInput(0x0); */
  /*     tiSetGenInput(0xffff); */
  /*     tiSetGTPInput(0x0); */

  tiSetBusySource(TI_BUSY_LOOPBACK|TI_BUSY_SWB,1);

  tiSetBlockBufferLevel(1);

  tiSetFiberDelay(1,2);
  tiSetSyncDelayWidth(1,0x3f,1);


    /***************************************
     * FADC Setup 
     ***************************************/
  unsigned short adc_playback_allchannels[512];
  unsigned short adc_playback[32] =
    {
      0,0,0x00bb,0x00ff, 0x00ff,0x00bb,0,0, 
      0,0,0,0, 0,0,0,0, 
      0,0,0,0, 0,0,0,0, 
      0,0,0,0, 0,0,0,0
    };

  /* Here, we assume that the addresses of each board were set according to their
   * geographical address (slot number):
   * Slot  3:  (3<<19) = 0x180000
   * Slot  4:  (4<<19) = 0x200000
   * ...
   * Slot 20: (20<<19) = 0xA00000
   */

  NFADC = 16+2;   /* 16 slots + 2 (for the switch slots) */
  fadcA32Base=0x09000000;

  /* Setup the iFlag.. flags for FADC initialization */
  int iFlag=0;
  /* Sync Source */
  iFlag |= (1<<0);    /* VXS */
  /* Trigger Source */
  iFlag |= (1<<2);    /* VXS */
  /* Clock Source */
  iFlag |= (0<<5);    /* Self */

  vmeSetQuietFlag(1); /* skip the errors associated with BUS Errors */
  faInit((unsigned int)(3<<19),(1<<19),NFADC,iFlag);
  NFADC=nfadc;        /* Redefine our NFADC with what was found from the driver */
  vmeSetQuietFlag(0); /* Turn the error statements back on */
  
  /* Calculate the maximum number of words per block transfer (assuming Pulse mode)
   *   MAX = NFADC * BLOCKLEVEL * (EvHeader + TrigTime*2 + Pulse*2*chan) 
   *         + 2*32 (words for byte alignment) 
   */
/*   if(faMode == 1) /\* Raw window Mode *\/ */
    MAXFADCWORDS = NFADC * BLOCKLEVEL * (1+2+FADC_WINDOW_WIDTH*16) + 3;
/*   else /\* Pulse mode *\/ */
/*     MAXFADCWORDS = NFADC * BLOCKLEVEL * (1+2+32) + 2*32; */
  /* Maximum TID words is easier to calculate, but we can be conservative, since
   * it's first in the readout
   */
/*   MAXTIDWORDS = 8+(3*BLOCKLEVEL); */
  
  printf("**************************************************\n");
  printf("* Calculated MAX FADC words per block = %d\n",MAXFADCWORDS);
/*   printf("* Calculated MAX TID  words per block = %d\n",MAXTIDWORDS); */
  printf("**************************************************\n");
  /* Check these numbers, compared to our buffer size.. */
/*   if( (MAXFADCWORDS+MAXTIDWORDS)*4 > MAX_EVENT_LENGTH ) */
/*     { */
/*       printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"); */
/*       printf(" WARNING.  Event buffer size is smaller than the expected data size\n"); */
/*       printf("     Increase the size of MAX_EVENT_LENGTH and recompile!\n"); */
/*       printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"); */
/*     } */

  
  if(NFADC>1)
    faEnableMultiBlock(1);

  /* Additional Configuration for each module */
  fadcSlotMask=0;
  int islot=0;
  for(islot=0;islot<NFADC;islot++) 
    {
      FA_SLOT = fadcID[islot];      /* Grab the current module's slot number */
      fadcSlotMask |= (1<<FA_SLOT); /* Add it to the mask */

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

    }


  /***************************************
   *   SD SETUP
   ***************************************/
  sdInit(0);   /* Initialize the SD library */
  sdSetActiveVmeSlots(fadcSlotMask); /* Use the fadcSlotMask to configure the SD */
  sdStatus();

    
  printf("Hit enter to reset stuff\n");
  getchar();

  tiClockReset();
  taskDelay(1);
  tiTrigLinkReset();
  taskDelay(1);
  tiEnableVXSSignals();
  taskDelay(1);

  faGStatus(0);

  /* FADC Perform some resets, status */
  for(islot=0;islot<NFADC;islot++) 
    {
      FA_SLOT = fadcID[islot];
      faSetClockSource(FA_SLOT,2);
      faClear(FA_SLOT);
      faResetToken(FA_SLOT);
      faResetTriggerCount(FA_SLOT);
    }

  /* TI Status */
  tiStatus(0);
  faGStatus(0);

  /*  Enable FADC */
  for(islot=0;islot<NFADC;islot++) 
    {
      FA_SLOT = fadcID[islot];
      faEnableSyncSrc(FA_SLOT);
    }


  tiSyncReset(1);

  taskDelay(1);

  faGStatus(0);
  tiStatus(0);

  printf("Hit enter to start triggers\n");
  getchar();

  for(islot=0;islot<NFADC;islot++)
    {
      faEnable(FA_SLOT,0,0);
/*       FA_SLOT = fadcID[islot]; */
/*       faChanDisable(FA_SLOT,0x0); */
/*       faSetMGTTestMode(FA_SLOT,1); */
    }


  tiIntEnable(0);
  tiStatus(0);
#define SOFTTRIG
#ifdef SOFTTRIG
  tiSetRandomTrigger(1,0xf);
/*   taskDelay(10); */
/*   tiSoftTrig(1,0x1,0x700,0); */
#endif

  printf("Hit any key to Disable TID and exit.\n");
  getchar();
  tiStatus(0);

#ifdef SOFTTRIG
  /* No more soft triggers */
  /*     tidSoftTrig(0x0,0x8888,0); */
  tiSoftTrig(1,0,0x700,0);
  tiDisableRandomTrigger();
#endif



  tiIntDisable();

  tiIntDisconnect();

  /* FADC Disable */
  for(islot=0;islot<NFADC;islot++) 
    {
      FA_SLOT = fadcID[islot];
      faDisable(FA_SLOT,0);
    }

  faGStatus(0);


 CLOSE:

  dmaPFreeAll();
  vmeCloseDefaultWindows();

  exit(0);
}

