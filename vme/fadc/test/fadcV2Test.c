/*
 * File:
 *    fadcV2Test.c
 *
 * Description:
 *    Check of FADC V2 with SDC and v851
 *
 *  Single hit and acquire mode with v851 generating hit (via VME) to
 *  FADC channel and trigger to SDC->FP
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "fadcLib.h"
#include "v851Lib.h"

#define FADC_ADDR (0xccc000)
#define V851_ADDR  0xDA00	  /*  base address of v851 Digital Delay Generator (A16) */
#define DIST_ADDR  0xEA00	  /*  base address of FADC signal distribution board  (A16)  */

DMA_MEM_ID vmeIN, vmeIN2, vmeOUT;
#define MAX_NUM_EVENTS    400
#define MAX_SIZE_EVENTS   1024*10      /* Size in Bytes */

extern int fadcA32Base;
extern volatile struct fadc_struct *FAp[(FA_MAX_BOARDS+1)];
extern int fadcA24Offset;
extern int fadcID[FA_MAX_BOARDS];
int FA_SLOT;

int 
main(int argc, char *argv[]) {

    GEF_STATUS status;

    printf("\nJLAB fadc Lib Tests\n");
    printf("----------------------------\n");

    status = vmeOpenDefaultWindows();
    if (status != GEF_SUCCESS)
    {
      printf("vmeOpenDefaultWindows failed: code 0x%08x\n",status);
      return -1;
    }

    fadcA32Base=0x09000000;
    /* Set the FADC structure pointer */
    int iFlag = (DIST_ADDR)<<10;
    /* Trigger Source */
    iFlag |= (1<<1); // Front Panel Input Trigger 
/*     iFlag |= (1<<2); // VXS Input Trigger  */
    /* Clock Source */
/*     iFlag |= (1<<5); // VXS Clock Source  */
    iFlag |= (0<<5); // Internal Clock Source 
    printf("FADC_ADDR 0x%08x\n",FADC_ADDR);
    faInit((unsigned int)(FADC_ADDR),(1<<19),1,iFlag);

    faGStatus(0);
    FA_SLOT = fadcID[0];

    goto CLOSE;
    /* Extra setups */
    faEnableBusError(FA_SLOT);
    faSetCalib(FA_SLOT, 23, 8);

    faChanDisable(FA_SLOT,0x0);
    faSetDAC(FA_SLOT,3300,0);
    faSetThreshold(FA_SLOT,1600,0);

    faSetNormalMode(FA_SLOT,0); /* DO THIS BEFORE faSetProcMode() !!! */
  /*  Setup option 1 processing - RAW Window Data     <-- */
  /*        option 2            - RAW Pulse Data */
  /*        option 3            - Integral Pulse Data */
  /*  Setup 200 nsec latency (PL  = 50)  */
  /*  Setup  80 nsec Window  (PTW = 20) */
  /*  Setup Pulse widths of 36ns (NSB(3)+NSA(6) = 9)  */
  /*  Setup up to 1 pulse processed */
  /*  Setup for both ADC banks(0 - all channels 0-15) */
    faSetProcMode(FA_SLOT,1,500,64,3,6,1,0);

/*     faPrintDAC(FA_SLOT); */
/*     faPrintThreshold(FA_SLOT); */

    v851Init(V851_ADDR,0,0);	/* initialize V851 */

/*     v851ProgPulser(30,0); /\* Setup 30Hz pulser *\/ */
    v851SetMode(3,0,0); /* Setup VME triggers */
    
    /* Set Waveforms to One shot mode */
    v851SetOneShot(0);

    /* Program Delays for each channel */
    int i;
    unsigned int delay[6] = { 0, 100, 2050, 1900, 2100, 2200 };  	/* default V851 DDG delays */
    for(i=1; i<6; i++)
      {
	v851SetDelay(i,delay[i],0,0);
      }
      /* Intiate module programming */
    v851UpdateDelay(0);

    /* Rearm */
    v851Enable(0);

    /*  Enable FADC */
    faSync(FA_SLOT);
    taskDelay(10);
    faEnable(FA_SLOT,0,0);
    faStatus(FA_SLOT,0);

    printf("Press enter to generate a trigger\n");
    getchar();
    v851Trig(0);

    /* FADC Disable */
    v851Disable(0);

    taskDelay(1);
    while(faDready(FA_SLOT,0)!=0)
      faPrintBlock(FA_SLOT,1);

    faDisable(FA_SLOT,0);

    faStatus(FA_SLOT,0);

    printf("\n\n");

 CLOSE:


    status = vmeCloseDefaultWindows();
    if (status != GEF_SUCCESS)
    {
      printf("vmeCloseDefaultWindows failed: code 0x%08x\n",status);
      return -1;
    }

    exit(0);
}

