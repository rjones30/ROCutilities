/*************************************************************************
 *
 *  fadc_sd_ctp_list.c - Library of routines for readout and buffering of 
 *                events using a JLAB Trigger Interface (TI) with 
 *                a Linux VME controller.
 *
 *      This readout list for use with a crate of fADC250-V2s, Switch Slot
 *      modules: CTP, SD.
 *
 */

/* Event Buffer definitions */
#define MAX_EVENT_POOL     400
#define MAX_EVENT_LENGTH   (66000<<2)      /* Size in Bytes */

#define TI_SLAVE
#define TI_READOUT TI_READOUT_TS_POLL  /* Poll for available data, TS/TImaster triggers */
#define TI_ADDR    (21<<19)          /* GEO slot 21 */

#define FIBER_LATENCY_OFFSET 0x30

#include <linux/prctl.h>
#include "dmaBankTools.h"
#include "tiprimary_list.c" /* source required for CODA */
#include "fadcLib.h"
#include "sdLib.h"
#include "ctpLib.h"
#include "remexLib.h"

#define BLOCKLEVEL 1
int BUFFERLEVEL=1;

/* TI Globals */
unsigned int MAXTIWORDS=0;
int NPULSES=4;
extern unsigned int tiTriggerSource;

/* CTP Defaults/Globals */
#define CTP_THRESHOLD    0xbb

/* FADC Defaults/Globals */
#define FADC_ADDR (9<<19)
#define FADC_THRESHOLD     20
#define FADC_WINDOW_LAT   375
#define FADC_WINDOW_WIDTH 24
#define FADC_DAC_LEVEL 3250
/* Raw Window Data */
#define FADC_MODE 1
extern int fadcA32Base;
extern volatile struct fadc_struct *FAp[(FA_MAX_BOARDS+1)];
extern int fadcA24Offset;
extern int nfadc;
int FA_SLOT=0;

int NFADC=1; /* May change, depending on crate */
unsigned int fadcSlotMask=0;
extern int fadcBlockError;

/* for the calculation of maximum data words in the block transfer */
unsigned int MAXFADCWORDS=0;

unsigned int ctp_threshold=CTP_THRESHOLD, fadc_threshold=FADC_THRESHOLD;
unsigned int fadc_window_lat=FADC_WINDOW_LAT, fadc_window_width=FADC_WINDOW_WIDTH;
unsigned int blocklevel=BLOCKLEVEL;

/* function prototype */
void rocTrigger(int arg);

/****************************************
 *  DOWNLOAD
 ****************************************/
void
rocDownload()
{
  remexSetCmsgServer("megrez"); // Set this to the platform's host
  remexSetRedirect(1);
  remexInit(NULL,1);

  /* Setup Address and data modes for DMA transfers
   *   
   *  vmeDmaConfig(addrType, dataType, sstMode);
   *
   *  addrType = 0 (A16)    1 (A24)    2 (A32)
   *  dataType = 0 (D16)    1 (D32)    2 (BLK32) 3 (MBLK) 4 (2eVME) 5 (2eSST)
   *  sstMode  = 0 (SST160) 1 (SST267) 2 (SST320)
   */
  vmeDmaConfig(2,5,1); 

  /*****************
   *   TI SETUP
   *****************/
  tiSetBlockBufferLevel(BUFFERLEVEL);

  /* Set the sync delay width to 0x40*32 = 2.048us */
  tiSetSyncDelayWidth(0x54, 0x40, 1);

  tiStatus(1);

  fadcA32Base=0x09000000;
  /* Set the FADC structure pointer */
  int iFlag = 0;
  iFlag |= FA_INIT_EXT_SYNCRESET; /* P2 SyncReset*/
  iFlag |= FA_INIT_VXS_TRIG;      /* VXS Input Trigger */
  iFlag |= FA_INIT_INT_CLKSRC;    /* Internal Clock Source (Will switch later) */

  vmeSetQuietFlag(1);
  faInit((unsigned int)(3<<19),(1<<19),NFADC+2,iFlag);
  NFADC=nfadc;
  vmeSetQuietFlag(0);

  /* Calculate the maximum number of words per block transfer */
  if(FADC_MODE==1) /* Raw Window */
    MAXFADCWORDS = NFADC * blocklevel * (1+2+FADC_WINDOW_WIDTH*16) + 3;
  else if (FADC_MODE==3) /* Pulse Integral */
    MAXFADCWORDS = NFADC * blocklevel * (2+1+2+NPULSES*2*16) + 3;
  
  printf("****************************************\n");
  printf("* Calculated MAX FADC words = %d\n",MAXFADCWORDS);
  printf("****************************************\n");
  
  if(NFADC>1)
    faEnableMultiBlock(1);

  /* Extra setups */
  int ifa;
  fadcSlotMask=faScanMask();;
  printf("******* fadcSlotMask    = 0x%08x\n",fadcSlotMask);
  for(ifa=0;ifa<NFADC;ifa++) 
    {
      /* Set the internal DAC level */
      faSetDAC(FA_SLOT,FADC_DAC_LEVEL,0);
      faPrintDAC(FA_SLOT);
      /* Set the threshold for data readout */
      faSetThreshold(FA_SLOT,fadc_threshold,0);
	
      int ichan;
      for(ichan=0; ichan<16; ichan++)
	{
	  faSetChannelPedestal(FA_SLOT,ichan,0);
	}


      /*********************************************************************************
       * faSetProcMode(int id, int pmode, unsigned int PL, unsigned int PTW, 
       *    unsigned int NSB, unsigned int NSA, unsigned int NP, int bank)
       *
       *  id    : fADC250 Slot number
       *  pmode : Processing Mode
       *          1 - Raw Window
       *          2 - Pulse Raw Window
       *          3 - Pulse Integral
       *          4 - High-resolution time
       *          7 - Mode 3 + Mode 4 
       *          8 - Mode 1 + Mode 4
       *    PL : Window Latency
       *   PTW : Window Width
       *   NSB : Number of samples before pulse over threshold
       *   NSA : Number of samples after pulse over threshold
       *    NP : Number of pulses processed per window
       *  bank : Ignored
       *
       */
      faSetProcMode(FA_SLOT,FADC_MODE,fadc_window_lat,fadc_window_width,3,6,3,0);

      faEnableBusError(FA_SLOT);
      faSetBlockLevel(FA_SLOT,blocklevel);

      faStatus(FA_SLOT,1);
    }	


  /*****************
   *   SD SETUP
   *****************/
  sdInit(0);
  sdSetActiveVmeSlots(fadcSlotMask);
  sdStatus(1);

  /*****************
   *   CTP SETUP
   *****************/
  ctpInit(0);

  ctpSetVmeSlotEnableMask(fadcSlotMask);
  ctpSetFinalSumThreshold(ctp_threshold, 0);
  ctpStatus(1);

  int iwait=0;
  int allchanup=0;
  while(allchanup  != (0x7) )
    {
      iwait++;
      allchanup = ctpGetAllChanUp(0);
      if(iwait>1000)
	{
	  printf("iwait timeout   allchup - 0x%x\n",allchanup);
	  break;
	}
    }

  printf("rocDownload: User Download Executed\n");

}

/****************************************
 *  PRESTART
 ****************************************/
void
rocPrestart()
{
  unsigned short iflag;
  int stat;
  int ifa;

  /* Program/Init VME Modules Here */
  for(ifa=0;ifa<NFADC;ifa++) 
    {
      FA_SLOT = faSlot(ifa);
      faSetClockSource(FA_SLOT,FA_REF_CLK_P0);
      faSoftReset(FA_SLOT,0);
      faResetToken(FA_SLOT);
      faResetTriggerCount(FA_SLOT);
    }

  tiStatus(1);

  ctpAlignAtSyncReset(1);

  for(ifa=0;ifa<NFADC;ifa++) 
    {
      FA_SLOT = faSlot(ifa);
      /*  Enable FADC */
      faChanDisable(FA_SLOT,0xffff);
      faSetMGTTestMode(FA_SLOT,0);
      faEnable(FA_SLOT,0,0);
    }

  ctpStatus(1);
  ctpResetScalers();
  printf("CTP sync counter = %d\n",ctpGetSyncScaler());

  printf("rocPrestart: User Prestart Executed\n");
}

/****************************************
 *  GO
 ****************************************/
void
rocGo()
{
  int ifa;

  /* Enable modules, if needed, here */
  ctpAlignAtSyncReset(0);
  ctpGetAlignmentStatus(1,10);

  for(ifa=0;ifa<NFADC;ifa++)
    {
      FA_SLOT = faSlot(ifa);
      faChanDisable(FA_SLOT,0x0);
      faSetMGTTestMode(FA_SLOT,1);
    }

  /* Interrupts/Polling enabled after conclusion of rocGo() */

}

/****************************************
 *  END
 ****************************************/
void
rocEnd()
{
  int   err, debug=1, msgSize=0, counter=0;
  int ifa;

  for(ifa=0;ifa<NFADC;ifa++) 
    {
      FA_SLOT = faSlot(ifa);
      faDisable(FA_SLOT,0);
      faStatus(FA_SLOT,0);
    }

  tiStatus(1);
  sdStatus(1);
  ctpStatus(1);

  printf("rocEnd: Ended after %d blocks\n",tiGetIntCount());
}

/****************************************
 *  TRIGGER
 ****************************************/
void
rocTrigger(int arg)
{
  int ii, ifa;
  int stat, dCnt, len=0, idata;
  unsigned int gready=0;
  unsigned int intCount=0;

  intCount = tiGetIntCount();

  vmeDmaConfig(2,5,1); 
  /* Readout the trigger block from the TI 
     Trigger Block MUST be reaodut first */
  dCnt = tiReadTriggerBlock(dma_dabufp);
  if(dCnt<=0) 
    {
      printf("No data or error.  dCnt = %d\n",dCnt);
    }
  else
    { /* TI Data is already in a bank structure.  Bump the pointer */
      dma_dabufp += dCnt;
    }

  BANKOPEN(3,BT_UI4,0);
  /* Readout FADC */
  /* Configure Block Type... temp fix for 2eSST trouble with token passing */
  if(NFADC!=0)
    {
      int itime, roflag=1, nReadout=0;
      if(NFADC>1) 
	{
	  roflag=2; nReadout=1;
	}
      else
	{
	  roflag=1; nReadout=NFADC;
	}

      for(ifa=0; ifa<nReadout; ifa++)
	{
	  /* Check for Block Ready */
	  FA_SLOT = faSlot(ifa);
	  for(itime=0;itime<100;itime++) 
	    {
	      gready = faGBready();
	      stat = (gready == fadcSlotMask);
	      if (stat>0) 
		{
		  break;
		}
	    }

	  if(stat>0) 
	    {
	      /* Readout Block */
	      dCnt = faReadBlock(FA_SLOT,dma_dabufp,MAXFADCWORDS,roflag);
	      if(dCnt<=0)
		{
		  printf("FADC%d: No data or error.  dCnt = %d\n",FA_SLOT,dCnt);
		}
	      else
		{
		  dma_dabufp += dCnt;
		}
	    } 
	  else 
	    {
	      /* Block was not ready */
	      printf ("FADC%d: no events   stat=%d  intcount = %d   gready = 0x%08x  fadcSlotMask = 0x%08x\n",
		      FA_SLOT,stat,intCount,gready,fadcSlotMask);
	    }
	  if(fadcBlockError==1)
	    {
	      printf("   Block Read Error in event %d\n",intCount);
	      printf("   Token Status Mask 0x%08x\n",faGTokenStatus());
	    }
	  
	}

      if(roflag==2)
	{
	  faResetToken(faSlot(0));
	}
    }

  BANKCLOSE;

}

void
rocCleanup()
{
  int ifa=0;
  int   err, debug=1, msgSize=0, counter=0;

  remexClose();

  printf("%s: Reset all FADCs\n",__FUNCTION__);
  for(ifa=0; ifa<NFADC; ifa++)
    {
      FA_SLOT = faSlot(ifa);
      faReset(FA_SLOT,1); /* Reset, and DO NOT restore A32 settings */
    }
  
}

