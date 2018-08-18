/*----------------------------------------------------------------------------*/
/**
 * @mainpage
 * <pre>
 *  fadcLib_extnesions.c  -  stuff that should have been in fadcLib.c
 *                           and isn't.
 *
 *  Author: Richard Jones, based on code in fadcLib.c by
 *          David Abbott & Bryan Moffit
 *          Jefferson Lab Data Acquisition Group
 *
 * </pre>
 *----------------------------------------------------------------------------*/

#ifdef VXWORKS
#include <vxWorks.h>
#include "vxCompat.h"
#else
#include <stddef.h>
#include <pthread.h>
#include "jvme.h"
#endif
#include <stdio.h>
#include <string.h>
#ifdef VXWORKS
#include <logLib.h>
#include <taskLib.h>
#include <intLib.h>
#include <iv.h>
#include <semLib.h>
#include <vxLib.h>
#else
#include <unistd.h>
#endif

/* Include ADC definitions */
#include "fadcLib.h"

#ifdef VXWORKS
#define FALOCK
#define FAUNLOCK
#define FASDCLOCK
#define FASDCUNLOCK
#else
/* Mutex to guard flexio read/writes */
pthread_mutex_t   faMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t   fasdcMutex = PTHREAD_MUTEX_INITIALIZER;
#define FALOCK      if(pthread_mutex_lock(&faMutex)<0) perror("pthread_mutex_lock");
#define FAUNLOCK    if(pthread_mutex_unlock(&faMutex)<0) perror("pthread_mutex_unlock");
#define FASDCLOCK   if(pthread_mutex_lock(&fasdcMutex)<0) perror("pthread_mutex_lock");
#define FASDCUNLOCK if(pthread_mutex_unlock(&fasdcMutex)<0) perror("pthread_mutex_unlock");
#endif

/* Define external Functions */
#ifdef VXWORKS
IMPORT  STATUS sysBusToLocalAdrs(int, char *, char **);
IMPORT  STATUS intDisconnect(int);
IMPORT  STATUS sysIntEnable(int);
IMPORT  STATUS sysIntDisable(int);
IMPORT  STATUS sysVmeDmaDone(int, int);
IMPORT  STATUS sysVmeDmaSend(UINT32, UINT32, int, BOOL);

#define EIEIO    __asm__ volatile ("eieio")
#define SYNC     __asm__ volatile ("sync")
#endif

/* Define Interrupts variables */
BOOL              fadcIntRunning  = FALSE;                    /* running flag */
int               fadcIntID       = -1;                       /* id number of ADC generating interrupts */
LOCAL VOIDFUNCPTR fadcIntRoutine  = NULL;                     /* user interrupt service routine */
LOCAL int         fadcIntArg      = 0;                        /* arg to user routine */
LOCAL UINT32      fadcIntLevel    = FA_VME_INT_LEVEL;         /* default VME interrupt level */
LOCAL UINT32      fadcIntVec      = FA_VME_INT_VEC;           /* default interrupt Vector */

/* Define global variables */
int nfadc = 0;                                       /* Number of FADCs in Crate */
unsigned int  fadcA32Base   = 0x08000000;            /* Minimum VME A32 Address for use by FADCs */
unsigned long fadcA32Offset = 0x08000000;            /* Difference in CPU A32 Base - VME A32 Base */
unsigned long fadcA24Offset = 0x0;                   /* Difference in CPU A24 Base - VME A24 Base */
unsigned long fadcA16Offset = 0x0;                   /* Difference in CPU A16 Base - VME A16 Base */
volatile struct fadc_struct *FAp[(FA_MAX_BOARDS+1)]; /* pointers to FADC memory map */
volatile struct fadc_sdc_struct *FASDCp;             /* pointer to FADC Signal distribution card */
volatile unsigned int *FApd[(FA_MAX_BOARDS+1)];      /* pointers to FADC FIFO memory */
volatile unsigned int *FApmb;                        /* pointer to Multblock window */
int fadcID[FA_MAX_BOARDS];                           /* array of slot numbers for FADCs */
unsigned int fadcAddrList[FA_MAX_BOARDS];            /* array of a24 addresses for FADCs */
int fadcRev[(FA_MAX_BOARDS+1)];                      /* Board Revision Info for each module */
int fadcProcRev[(FA_MAX_BOARDS+1)];                  /* Processing FPGA Revision Info for each module */
unsigned short fadcChanDisable[(FA_MAX_BOARDS+1)];   /* Disabled Channel Mask for each Module*/
int fadcInited=0;                                    /* >0 if Library has been Initialized before */
int fadcMaxSlot=0;                                   /* Highest Slot hold an FADC */
int fadcMinSlot=0;                                   /* Lowest Slot holding an FADC */
int fadcSource=0;                                    /* Signal source for FADC system control*/
int fadcBlockLevel=0;                                /* Block Level for ADCs */
int fadcIntCount = 0;                                /* Count of interrupts from FADC */
int fadcUseSDC=0;                                    /* If > 0 then Use Signal Distribution board */
struct fadc_data_struct fadc_data;
int fadcBlockError=FA_BLOCKERROR_NO_ERROR; /* Whether (>0) or not (0) Block Transfer had an error */
int fadcAlignmentDebug=0;                            /* Flag to send alignment sequence to CTP */

/* Internal triggering tools */
#include "faItrig.c"

/* Include Firmware Tools */
#include "fadcFirmwareTools.c"

/**
 *  @ingroup Config
 *  @brief Get the threshold used to determine what samples are sent through the
 *     trigger path
 *  @param id Slot number
 *  @return threshold Trigger Path Threshold
 */
unsigned int
faGetTriggerPathThreshold(int id)
{
  if(id==0) id=fadcID[0];
  if((id<=0) || (id>21) || (FAp[id] == NULL)) {
    printf("%s: ERROR : FADC in slot %d is not initialized \n",__FUNCTION__,id);
    return ERROR;
  }

  if(fadcProcRev[id]<0x90B)
    {
      printf("%s: ERROR: Processing Firmware does not support this function\n",
	     __FUNCTION__);
      printf("      Requires 0x90B and above\n");
      return ERROR;
    }

  return vmeRead32(&FAp[id]->config3)&FA_ADC_TPT_MASK;
}
