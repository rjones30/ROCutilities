/*----------------------------------------------------------------------------*/
/**
 * @mainpage
 * <pre>
 *  fadcLib.c  -  Driver library for JLAB config and readout of JLAB 250MHz FLASH
 *                  ADC using a VxWorks >=5.4 or Linux >=2.6.18 based Single 
 *                  Board computer. 
 *
 *  Author: David Abbott & Bryan Moffit
 *          Jefferson Lab Data Acquisition Group
 *          June 2007
 *
 *  Revision  2.0 - Initial Revision for FADC V2
 *                    - Supports up to 20 FADC boards in a Crate
 *                    - Programmed I/O and Block reads
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
 * @defgroup Config Initialization/Configuration
 * @defgroup SDCConfig SDC Initialization/Configuration
 *   @ingroup Config
 * @defgroup Status Status
 * @defgroup SDCStatus SDC Status
 *   @ingroup Status
 * @defgroup Readout Data Readout
 * @defgroup IntPoll Interrupt/Polling
 * @defgroup Deprec Deprecated - To be removed
 */

/**
 *  @ingroup Config
 *  @brief Initialize JLAB FADC Library. 
 *
 * @param addr
 *  - A24 VME Address of the fADC250
 * @param addr_inc
 *  - Amount to increment addr to find the next fADC250
 * @param nadc
 *  - Number of times to increment
 *
 *  @param iFlag 18 bit integer
 * <pre>
 *       Low 6 bits - Specifies the default Signal distribution (clock,trigger) 
 *                    sources for the board (Internal, FrontPanel, VXS, VME(Soft))
 *       bit    0:  defines Sync Reset source
 *                     0  VME (Software Sync-Reset)
 *                     1  Front Panel/VXS/P2 (Depends on Clk/Trig source selection)
 *       bits 3-1:  defines Trigger source
 *               0 0 0  VME (Software Triggers)
 *               0 0 1  Front Panel Input
 *               0 1 0  VXS (P0) 
 *               1 0 0  Internal Trigger Logic (HITSUM FPGA)
 *               (all others Undefined - default to VME/Software)
 *       bits 5-4:  defines Clock Source
 *           0 0  Internal 250MHz Clock
 *           0 1  Front Panel 
 *           1 0  VXS (P0)
 *           1 1  P2 Connector (Backplane)
 * </pre>
 *
 * <pre>
 *       Common Modes of Operation:
 *           Value = 0  CLK (Int)  TRIG (Soft)   SYNC (Soft)    (Debug/Test Mode)
 *                   2  CLK (Int)  TRIG (FP)     SYNC (Soft)    (Single Board
 *                   3  CLK (Int)  TRIG (FP)     SYNC (FP)         Modes)
 *                0x10  CLK (FP)   TRIG (Soft)   SYNC (Soft)
 *                0x13  CLK (FP)   TRIG (FP)     SYNC (FP)      (VME SDC Mode)
 *                0x20  CLK (VXS)  TRIG (Soft)   SYNC (Soft)
 *                0x25  CLK (VXS)  TRIG (VXS)    SYNC (VXS)     (VXS SD Mode)
 *
 *
 *      High 10bits - A16 Base address of FADC Signal Distribution Module
 *                    This board can control up to 7 FADC Boards.
 *                    Clock Source must be set to Front Panel (bit4 = 1)
 *
 *      bit 16:  Exit before board initialization
 *             0 Initialize FADC (default behavior)
 *             1 Skip initialization (just setup register map pointers)
 *
 *      bit 17:  Use fadcAddrList instead of addr and addr_inc
 *               for VME addresses.
 *             0 Initialize with addr and addr_inc
 *             1 Use fadcAddrList 
 *
 *      bit 18:  Skip firmware check.  Useful for firmware updating.
 *             0 Perform firmware check
 *             1 Skip firmware check
 * </pre>
 *      
 *
 * @return OK, or ERROR if the address is invalid or a board is not present.
 */
STATUS 
faInit(UINT32 addr, UINT32 addr_inc, int nadc, int iFlag)
{
  int ii, res, errFlag = 0;
  int boardID = 0;
  int maxSlot = 1;
  int minSlot = 21;
  int trigSrc=0, clkSrc=0, srSrc=0;
  unsigned int rdata, a32addr, a16addr=0;
  unsigned long laddr=0, laddr_inc=0;
  volatile struct fadc_struct *fa;
  unsigned short sdata;
  int noBoardInit=0;
  int useList=0;
  int noFirmwareCheck=0;
  unsigned short supported_proc[FA_SUPPORTED_PROC_FIRMWARE_NUMBER]
    = {FA_SUPPORTED_PROC_FIRMWARE};
  unsigned short proc_version=0;
  int icheck=0, proc_supported=0;


  // Alex
  /* Check if we have already Initialized boards before */
  if((fadcInited>0) && (fadcID[0] != 0)) 
    {

      FALOCK;

      /* Hard Reset of all FADC boards in the Crate */
      for(ii=0;ii<nfadc;ii++) 
	{
	  vmeWrite32(&(FAp[fadcID[ii]]->csr),FA_CSR_HARD_RESET);
          printf(" Hard Reset Executed on FADC %d \n",FAp[fadcID[ii]]);
	}
      taskDelay(5);

      FAUNLOCK;

    }

  
  /* Check if we are to exit when pointers are setup */
  noBoardInit=(iFlag&FA_INIT_SKIP)>>16;

  /* Check if we're initializing using a list */
  useList=(iFlag&FA_INIT_USE_ADDRLIST)>>17;

  /* Are we skipping the firmware check? */
  noFirmwareCheck=(iFlag&FA_INIT_SKIP_FIRMWARE_CHECK)>>18;

  /* Check for valid address */
  if(addr==0) 
    {
      printf("faInit: ERROR: Must specify a Bus (VME-based A24) address for FADC 0\n");
      return(ERROR);
    }
  else if(addr > 0x00ffffff) 
    { /* A24 Addressing */
      printf("faInit: ERROR: A32 Addressing not allowed for FADC configuration space\n");
      return(ERROR);
    }
  else
    { /* A24 Addressing */
      if( ((addr_inc==0)||(nadc==0)) && (useList==0) )
	nadc = 1; /* assume only one FADC to initialize */

      /* get the FADC address */
#ifdef VXWORKS
      res = sysBusToLocalAdrs(0x39,(char *)addr,(char **)&laddr);
#else
      res = vmeBusToLocalAdrs(0x39,(char *)(unsigned long)addr,(char **)&laddr);
#endif
      if (res != 0) 
	{
#ifdef VXWORKS
	  printf("faInit: ERROR in sysBusToLocalAdrs(0x39,0x%x,&laddr) \n",addr);
#else
	  printf("faInit: ERROR in vmeBusToLocalAdrs(0x39,0x%x,&laddr) \n",addr);
#endif
	  return(ERROR);
	}
      fadcA24Offset = laddr - addr;
    }

  /* Init Some Global variables */
  fadcSource = iFlag&FA_SOURCE_MASK;
  fadcInited = nfadc = 0;
  fadcUseSDC = 0;
  bzero((char *)fadcChanDisable,sizeof(fadcChanDisable));
  bzero((char *)fadcID,sizeof(fadcID));

  for (ii=0;ii<nadc;ii++) 
    {
      if(useList==1)
	{
	  laddr_inc = fadcAddrList[ii] + fadcA24Offset;
	}
      else
	{
	  laddr_inc = laddr +ii*addr_inc;
	}
      fa = (struct fadc_struct *)laddr_inc;
      /* Check if Board exists at that address */
#ifdef VXWORKS
      res = vxMemProbe((char *) &(fa->version),VX_READ,4,(char *)&rdata);
#else
      res = vmeMemProbe((char *) &(fa->version),4,(char *)&rdata);
#endif
      if(res < 0) 
	{
#ifdef VXWORKS
	  printf("faInit: WARN: No addressable board at addr=0x%x\n",(UINT32) fa);
#else
	  printf("faInit: WARN: No addressable board at VME (Local) addr=0x%x (0x%lx)\n",
		 (UINT32)(laddr_inc-fadcA24Offset), (unsigned long) fa);
#endif
	  errFlag = 1;
	  continue;
	}
      else 
	{
	  /* Check that it is an FA board */
	  if((rdata&FA_BOARD_MASK) != FA_BOARD_ID) 
	    {
	      printf("%s: WARN: For board at 0x%x, Invalid Board ID: 0x%x\n",
		     __FUNCTION__,
		     (UINT32)(laddr_inc-fadcA24Offset), rdata);
	      continue;
	    }
	  else 
	    {
	      /* Check if this is board has a valid slot number */
	      boardID =  ((vmeRead32(&(fa->intr)))&FA_SLOT_ID_MASK)>>16;

	      if((boardID <= 0)||(boardID >21)) 
		{
		  printf("%s: ERROR: For Board at 0x%x,  Slot number is not in range: %d\n",
			 __FUNCTION__,(UINT32)(laddr_inc-fadcA24Offset), boardID);
		  continue;
		}

	      if(!noFirmwareCheck)
		{
		  /* Check Control FPGA firmware version */
		  if( (rdata&FA_VERSION_MASK) < FA_SUPPORTED_CTRL_FIRMWARE )
		    {
		      printf("%s: ERROR: Slot %2d: Control FPGA Firmware (0x%02x) not supported by this driver.\n",
			     __FUNCTION__,boardID, rdata & FA_VERSION_MASK);
		      printf("\tUpdate to 0x%02x to use this driver.\n",FA_SUPPORTED_CTRL_FIRMWARE);
		      continue;
		    }

		  /* Check Processing FPGA firmware version */
		  proc_version = 
		    (unsigned short)(vmeRead32(&fa->adc_status[0]) & FA_ADC_VERSION_MASK);

		  for(icheck=0; icheck<FA_SUPPORTED_PROC_FIRMWARE_NUMBER; icheck++)
		    {
		      if(proc_version == supported_proc[icheck])
			proc_supported=1;
		    }

		  if(proc_supported==0)
		    {
		      printf("%s: ERROR: Slot %2d: Proc FPGA Firmware (0x%02x) not supported by this driver.\n",
			     __FUNCTION__,boardID, proc_version);
		      printf("\tSupported Proc Firmware:  ");
		      for(icheck=0; icheck<FA_SUPPORTED_PROC_FIRMWARE_NUMBER; icheck++)
			{
			  printf("0x%02x ",supported_proc[icheck]);
			}
		      printf("\n");
		      continue;
		    }
		}
	      else
		{
		  /* Check Control FPGA firmware version */
		  if( (rdata&FA_VERSION_MASK) < FA_SUPPORTED_CTRL_FIRMWARE )
		    {
		      printf("%s: WARN: Slot %2d: Control FPGA Firmware (0x%02x) not supported by this driver (ignored).\n",
			     __FUNCTION__,boardID, rdata & FA_VERSION_MASK);
		    }

		  /* Check Processing FPGA firmware version */
		  proc_version = 
		    (unsigned short)(vmeRead32(&fa->adc_status[0]) & FA_ADC_VERSION_MASK);

		  for(icheck=0; icheck<FA_SUPPORTED_PROC_FIRMWARE_NUMBER; icheck++)
		    {
		      if(proc_version == supported_proc[icheck])
			proc_supported=1;
		    }

		  if(proc_supported==0)
		    {
		      printf("%s: WARN: Slot %2d: Proc FPGA Firmware (0x%02x) not supported by this driver (ignored).\n",
			     __FUNCTION__,boardID, proc_version & FA_VERSION_MASK);
		    }
		}

	      FAp[boardID] = (struct fadc_struct *)(laddr_inc);
	      fadcRev[boardID] = rdata&FA_VERSION_MASK;
	      fadcProcRev[boardID] = proc_version;
	      fadcID[nfadc] = boardID;
	      if(boardID >= maxSlot) maxSlot = boardID;
	      if(boardID <= minSlot) minSlot = boardID;
	      
	      printf("Initialized FADC %2d  Slot #%2d at VME (Local) address 0x%06x (0x%lx) \n",
		     nfadc,fadcID[nfadc],
		     (UINT32) (((unsigned long)FAp[(fadcID[nfadc])])-fadcA24Offset),
		     (unsigned long) FAp[(fadcID[nfadc])]);
	    }
	  nfadc++;
	}
    }



  // Alex
  //  for(ii = 3;ii < 11;ii++){
  //    vmeWrite32(&(FAp[ii]->csr),FA_CSR_HARD_RESET);
  //    printf(" Hard Reset Executed on FADC %d \n",FAp[ii]);
  //  }
  
  //  for(ii = 13;ii < 16;ii++){
  //    vmeWrite32(&(FAp[ii]->csr),FA_CSR_HARD_RESET);
  //    printf(" Hard Reset Executed on FADC %d \n",FAp[ii]);
  //  }
  




  /* Check if we are using a JLAB FADC Signal Distribution Card (SDC)
     NOTE the SDC board only supports 7 FADCs - so if there are
     more than 7 FADCs in the crate they can only be controlled by daisychaining 
     multiple SDCs together - or by using a VXS Crate with SD switch card 
  */
  a16addr = iFlag&FA_SDC_ADR_MASK;
  if(a16addr) 
    {
#ifdef VXWORKS
      res = sysBusToLocalAdrs(0x29,(char *)a16addr,(char **)&laddr);
      if (res != 0) 
	{
	  printf("faInit: ERROR in sysBusToLocalAdrs(0x29,0x%x,&laddr) \n",a16addr);
	  return(ERROR);
	}

      res = vxMemProbe((char *) laddr,VX_READ,2,(char *)&sdata);
#else
      res = vmeBusToLocalAdrs(0x29,(char *)(unsigned long)a16addr,(char **)&laddr);
      if (res != 0) 
	{
	  printf("faInit: ERROR in vmeBusToLocalAdrs(0x29,0x%x,&laddr) \n",a16addr);
	  return(ERROR);
	}
      res = vmeMemProbe((char *) laddr,2,(char *)&sdata);
#endif
      if(res < 0) 
	{
	  printf("faInit: ERROR: No addressable SDC board at addr=0x%x\n",(UINT32) laddr);
	} 
      else 
	{
	  fadcA16Offset = laddr-a16addr;
	  FASDCp = (struct fadc_sdc_struct *) laddr;
	  if(!noBoardInit)
	    vmeWrite16(&(FASDCp->ctrl),FASDC_CSR_INIT);   /* Reset the Module */

	  if(nfadc>7) 
	    {
	      printf("WARN: A Single JLAB FADC Signal Distribution Module only supports 7 FADCs\n");
	      printf("WARN: You must use multiple SDCs to support more FADCs - this must be configured in hardware\n");
	    }
#ifdef VXWORKS
	  printf("Using JLAB FADC Signal Distribution Module at address 0x%x\n",
		 (UINT32) FASDCp); 
#else
	  printf("Using JLAB FADC Signal Distribution Module at VME (Local) address 0x%x (0x%lx)\n",
		 (UINT32)a16addr, (unsigned long) FASDCp); 
#endif
	  fadcUseSDC=1;
	}
      if(fadcSource == FA_SOURCE_SDC) 
	{  /* Check if SDC will be used */
	  fadcUseSDC = 1;
	  printf("faInit: JLAB FADC Signal Distribution Card is Assumed in Use\n");
	  printf("faInit: Front Panel Inputs will be enabled. \n");
	}
      else
	{
	  fadcUseSDC = 0;
	  printf("faInit: JLAB FADC Signal Distribution Card will not be Used\n");
	}
    }

  /* Hard Reset of all FADC boards in the Crate */
  if(!noBoardInit)
    {
      for(ii=0;ii<nfadc;ii++) 
	{
	  vmeWrite32(&(FAp[fadcID[ii]]->reset),FA_RESET_ALL);
	}
      taskDelay(60); 
    }

  /* Initialize Interrupt variables */
  fadcIntID = -1;
  fadcIntRunning = FALSE;
  fadcIntLevel = FA_VME_INT_LEVEL;
  fadcIntVec = FA_VME_INT_VEC;
  fadcIntRoutine = NULL;
  fadcIntArg = 0;

  /* Calculate the A32 Offset for use in Block Transfers */
#ifdef VXWORKS
  res = sysBusToLocalAdrs(0x09,(char *)fadcA32Base,(char **)&laddr);
  if (res != 0) 
    {
      printf("faInit: ERROR in sysBusToLocalAdrs(0x09,0x%x,&laddr) \n",fadcA32Base);
      return(ERROR);
    } 
  else 
    {
      fadcA32Offset = laddr - fadcA32Base;
    }
#else
  res = vmeBusToLocalAdrs(0x09,(char *)(unsigned long)fadcA32Base,(char **)&laddr);
  if (res != 0) 
    {
      printf("faInit: ERROR in vmeBusToLocalAdrs(0x09,0x%x,&laddr) \n",fadcA32Base);
      return(ERROR);
    } 
  else 
    {
      fadcA32Offset = laddr - fadcA32Base;
    }
#endif

  if(!noBoardInit)
    {
      /* what are the Trigger Sync Reset and Clock sources */
      if (fadcSource == FA_SOURCE_VXS)
	{
	  printf("faInit: Enabling FADC for VXS Clock ");
	  clkSrc  = FA_REF_CLK_P0;
	  switch (iFlag&0xf) 
	    {
	    case 0: case 1:
	      printf("and Software Triggers (Soft Sync Reset)\n");
	      trigSrc = FA_TRIG_VME | FA_ENABLE_SOFT_TRIG;
	      srSrc   = FA_SRESET_VME | FA_ENABLE_SOFT_SRESET;
	      break;
	    case 2:
	      printf("and Front Panel Triggers (Soft Sync Reset)\n");
	      trigSrc = FA_TRIG_FP_ISYNC;
	      srSrc   = FA_SRESET_VME | FA_ENABLE_SOFT_SRESET;
	      break;
	    case 3:
	      printf("and Front Panel Triggers (FP Sync Reset)\n");
	      trigSrc = FA_TRIG_FP_ISYNC;
	      srSrc   = FA_SRESET_FP_ISYNC;
	      break;
	    case 4: case 6:
	      printf("and VXS Triggers (Soft Sync Reset)\n");
	      trigSrc = FA_TRIG_P0_ISYNC;
	      srSrc   = FA_SRESET_VME | FA_ENABLE_SOFT_SRESET;
	      break;
	    case 5: case 7:
	      printf("and VXS Triggers (VXS Sync Reset)\n");
	      trigSrc = FA_TRIG_P0_ISYNC;
	      srSrc   = FA_SRESET_P0_ISYNC;
	      break;
	    case 8: case 10: case 12: case 14:
	      printf("and Internal Trigger Logic (Soft Sync Reset)\n");
	      trigSrc = FA_TRIG_INTERNAL;
	      srSrc   = FA_SRESET_VME | FA_ENABLE_SOFT_SRESET;
	      break;
	    case 9: case 11: case 13: case 15:
	      printf("and Internal Trigger Logic (VXS Sync Reset)\n");
	      trigSrc = FA_TRIG_INTERNAL;
	      srSrc   = FA_SRESET_FP_ISYNC;
	      break;
	    }
	}
      else if (fadcSource == FA_SOURCE_SDC) 
	{
	  printf("faInit: Enabling FADC for SDC Clock (Front Panel) ");
	  clkSrc  = FA_REF_CLK_FP;
	  switch (iFlag&0xf) 
	    {
	    case 0: case 1:
	      printf("and Software Triggers (Soft Sync Reset)\n");
	      trigSrc = FA_TRIG_VME | FA_ENABLE_SOFT_TRIG;
	      srSrc   = FA_SRESET_VME | FA_ENABLE_SOFT_SRESET;
	      break;
	    case 2: case 4: case 6:
	      printf("and Front Panel Triggers (Soft Sync Reset)\n");
	      trigSrc = FA_TRIG_FP_ISYNC;
	      srSrc   = FA_SRESET_VME | FA_ENABLE_SOFT_SRESET;
	      break;
	    case 3: case 5: case 7:
	      printf("and Front Panel Triggers (FP Sync Reset)\n");
	      trigSrc = FA_TRIG_FP_ISYNC;
	      srSrc   = FA_SRESET_FP_ISYNC;
	      break;
	    case 8: case 10: case 12: case 14:
	      printf("and Internal Trigger Logic (Soft Sync Reset)\n");
	      trigSrc = FA_TRIG_INTERNAL;
	      srSrc   = FA_SRESET_VME | FA_ENABLE_SOFT_SRESET;
	      break;
	    case 9: case 11: case 13: case 15:
	      printf("and Internal Trigger Logic (Front Panel Sync Reset)\n");
	      trigSrc = FA_TRIG_INTERNAL;
	      srSrc   = FA_SRESET_FP_ISYNC;
	      break;
	    }
	  faSDC_Config(0,0);
	}
      else 
	{  /* Use internal Clk */
	  printf("faInit: Enabling FADC Internal Clock, ");
	  clkSrc = FA_REF_CLK_INTERNAL;
	  switch (iFlag&0xf) 
	    {
	    case 0: case 1:
	      printf("and Software Triggers (Soft Sync Reset)\n");
	      trigSrc = FA_TRIG_VME | FA_ENABLE_SOFT_TRIG;
	      srSrc   = FA_SRESET_VME | FA_ENABLE_SOFT_SRESET ;
	      break;
	    case 2:
	      printf("and Front Panel Triggers (Soft Sync Reset)\n");
	      trigSrc = FA_TRIG_FP_ISYNC;
	      srSrc   = FA_SRESET_VME | FA_ENABLE_SOFT_SRESET;
	      break;
	    case 3:
	      printf("and Front Panel Triggers (FP Sync Reset)\n");
	      trigSrc = FA_TRIG_FP_ISYNC;
	      srSrc   = FA_SRESET_FP_ISYNC;
	      break;
	    case 4: case 6:
	      printf("and VXS Triggers (Soft Sync Reset)\n");
	      trigSrc = FA_TRIG_P0_ISYNC;
	      srSrc   = FA_SRESET_VME | FA_ENABLE_SOFT_SRESET;
	      break;
	    case 5: case 7:
	      printf("and VXS Triggers (VXS Sync Reset)\n");
	      trigSrc = FA_TRIG_P0_ISYNC;
	      srSrc   = FA_SRESET_P0_ISYNC;
	      break;
	    case 8: case 10: case 12: case 14:
	      printf("and Internal Trigger Logic (Soft Sync Reset)\n");
	      trigSrc = FA_TRIG_INTERNAL;
	      srSrc   = FA_SRESET_VME | FA_ENABLE_SOFT_SRESET;
	      break;
	    case 9: case 11: case 13: case 15:
	      printf("and Internal Trigger Logic (Front Panel Sync Reset)\n");
	      trigSrc = FA_TRIG_INTERNAL;
	      srSrc   = FA_SRESET_FP_ISYNC;
	      break;
	    }
	}
    }

  /* Enable Clock source - Internal Clk enabled by default */ 
  if(!noBoardInit)
    {
      for(ii=0;ii<nfadc;ii++) 
	{
	  vmeWrite32(&(FAp[fadcID[ii]]->ctrl1),(clkSrc | FA_ENABLE_INTERNAL_CLK)) ;
	}
      taskDelay(20);


      /* Hard Reset FPGAs and FIFOs */
      for(ii=0;ii<nfadc;ii++) 
	{
	  vmeWrite32(&(FAp[fadcID[ii]]->reset),
		     (FA_RESET_ADC_FPGA1 | FA_RESET_ADC_FIFO1 |
		      FA_RESET_DAC | FA_RESET_EXT_RAM_PT));

	  /* Release reset on MGTs */
	  vmeWrite32(&(FAp[fadcID[ii]]->mgt_ctrl),FA_RELEASE_MGT_RESET);
	  vmeWrite32(&(FAp[fadcID[ii]]->mgt_ctrl),FA_MGT_RESET);
	  vmeWrite32(&(FAp[fadcID[ii]]->mgt_ctrl),FA_RELEASE_MGT_RESET);
	}
      taskDelay(5);
    }

  /* Write configuration registers with default/defined Sources */
  for(ii=0;ii<nfadc;ii++) 
    {
    
      /* Program an A32 access address for this FADC's FIFO */
      a32addr = fadcA32Base + ii*FA_MAX_A32_MEM;
#ifdef VXWORKS
      res = sysBusToLocalAdrs(0x09,(char *)a32addr,(char **)&laddr);
      if (res != 0) 
	{
	  printf("faInit: ERROR in sysBusToLocalAdrs(0x09,0x%x,&laddr) \n",a32addr);
	  return(ERROR);
	}
#else
      res = vmeBusToLocalAdrs(0x09,(char *)(unsigned long)a32addr,(char **)&laddr);
      if (res != 0) 
	{
	  printf("faInit: ERROR in vmeBusToLocalAdrs(0x09,0x%x,&laddr) \n",a32addr);
	  return(ERROR);
	}
#endif
      FApd[fadcID[ii]] = (unsigned int *)(laddr);  /* Set a pointer to the FIFO */
      if(!noBoardInit)
	{
	  vmeWrite32(&(FAp[fadcID[ii]]->adr32),(a32addr>>16) + 1);  /* Write the register and enable */
	
	  /* Set Default Block Level to 1 */
	  vmeWrite32(&(FAp[fadcID[ii]]->blk_level),1);
	}
      fadcBlockLevel=1;

      /* Setup Trigger and Sync Reset sources */
      if(!noBoardInit)
	{
	  vmeWrite32(&(FAp[fadcID[ii]]->ctrl1),
		     vmeRead32(&(FAp[fadcID[ii]]->ctrl1)) | 
		     (srSrc | trigSrc) );
	}
      
      /* Set default stop and busy conditions (modified in faSetProcMode(..)) */
      faSetTriggerStopCondition(fadcID[ii], 9);
      faSetTriggerBusyCondition(fadcID[ii], 9);

    }

  /* If there are more than 1 FADC in the crate then setup the Muliblock Address
     window. This must be the same on each board in the crate */
  if(nfadc > 1) 
    {
      a32addr = fadcA32Base + (nfadc+1)*FA_MAX_A32_MEM; /* set MB base above individual board base */
#ifdef VXWORKS
      res = sysBusToLocalAdrs(0x09,(char *)a32addr,(char **)&laddr);
      if (res != 0) 
	{
	  printf("faInit: ERROR in sysBusToLocalAdrs(0x09,0x%x,&laddr) \n",a32addr);
	  return(ERROR);
	}
#else
      res = vmeBusToLocalAdrs(0x09,(char *)(unsigned long)a32addr,(char **)&laddr);
      if (res != 0) 
	{
	  printf("faInit: ERROR in vmeBusToLocalAdrs(0x09,0x%x,&laddr) \n",a32addr);
	  return(ERROR);
	}
#endif
      FApmb = (unsigned int *)(laddr);  /* Set a pointer to the FIFO */
      if(!noBoardInit)
	{
	  for (ii=0;ii<nfadc;ii++) 
	    {
	      /* Write the register and enable */
	      vmeWrite32(&(FAp[fadcID[ii]]->adr_mb),
			 (a32addr+FA_MAX_A32MB_SIZE) + (a32addr>>16) + FA_A32_ENABLE);
	    }
	}    
      /* Set First Board and Last Board */
      fadcMaxSlot = maxSlot;
      fadcMinSlot = minSlot;
      if(!noBoardInit)
	{
	  vmeWrite32(&(FAp[minSlot]->ctrl1),
		     vmeRead32(&(FAp[minSlot]->ctrl1)) | FA_FIRST_BOARD);
	  vmeWrite32(&(FAp[maxSlot]->ctrl1),
		     vmeRead32(&(FAp[maxSlot]->ctrl1)) | FA_LAST_BOARD);
	}    
    }

  if(!noBoardInit)
    fadcInited = nfadc;

  if(errFlag > 0) 
    {
      printf("faInit: WARN: Unable to initialize all requested FADC Modules (%d)\n",
	     nadc);
      if(nfadc > 0)
	printf("faInit: %d FADC(s) successfully initialized\n",nfadc );
      return(ERROR);
    } 
  else 
    {
      return(OK);
    }
}

int
faCheckAddresses(int id)
{
  unsigned long offset=0, expected=0, base=0;
  
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return ERROR;
    }

  printf("%s:\n\t ---------- Checking fADC250 address space ---------- \n",__FUNCTION__);

  base = (unsigned long) &FAp[id]->version;

  offset = ((unsigned long) &FAp[id]->system_monitor) - base;
  expected = 0xFC;
  if(offset != expected)
    printf("%s: ERROR FAp[id]->system_monitor not at offset = 0x%lx (@ 0x%lx)\n",
	   __FUNCTION__,expected,offset);

  offset = ((unsigned long) &FAp[id]->adc_status[0]) - base;
  expected = 0x100;
  if(offset != expected)
    printf("%s: ERROR FAp[id]->adc_status[0] not at offset = 0x%lx (@ 0x%lx)\n",
	   __FUNCTION__,expected,offset);

  offset = ((unsigned long) &FAp[id]->config7) - base;
  expected = 0x150;
  if(offset != expected)
    printf("%s: ERROR FAp[id]->config7 not at offset = 0x%lx (@ 0x%lx)\n",
	   __FUNCTION__,expected,offset);

  offset = ((unsigned long) &FAp[id]->adc_pedestal[0]) - base;
  expected = 0x158;
  if(offset != expected)
    printf("%s: ERROR FAp[id]->adc_pedestal[0] not at offset = 0x%lx (@ 0x%lx)\n",
	   __FUNCTION__,expected,offset);

  offset = ((unsigned long) &FAp[id]->scaler[0]) - base;
  expected = 0x300;
  if(offset != expected)
    printf("%s: ERROR FAp[id]->scaler[0] not at offset = 0x%lx (@ 0x%lx)\n",
	   __FUNCTION__,expected,offset);
    
  return OK;
}

/**
 *  @ingroup Config
 *  @brief Convert an index into a slot number, where the index is
 *          the element of an array of FADCs in the order in which they were
 *          initialized.
 *
 * @param i Initialization number
 * @return Slot number if Successfull, otherwise ERROR.
 *
 */
int
faSlot(unsigned int i)
{
  if(i>=nfadc)
    {
      printf("%s: ERROR: Index (%d) >= FADCs initialized (%d).\n",
	     __FUNCTION__,i,nfadc);
      return ERROR;
    }

  return fadcID[i];
}

/**
 *  @ingroup Config
 *  @brief Set the clock source
 *
 *   This routine should be used in the case that the source clock
 *   is NOT set in faInit (and defaults to Internal).  Such is the case
 *   when clocks are synchronized in a many crate system.  The clock source
 *   of the FADC should ONLY be set AFTER those clocks have been set and
 *   synchronized.
 *
 *  @param id Slot Number
 *  @param clkSrc 2 bit integer
 * <pre>
 *       bits 1-0:  defines Clock Source
 *           0 0  Internal 250MHz Clock
 *           0 1  Front Panel 
 *           1 0  VXS (P0)
 *           1 1  VXS (P0)
 * </pre>
 *
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetClockSource(int id, int clkSrc)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return ERROR;
    }

  if(clkSrc>0x3)
    {
      printf("%s: ERROR: Invalid Clock Source specified (0x%x)\n",
	     __FUNCTION__,clkSrc);
      return ERROR;
    }

  /* Enable Clock source - Internal Clk enabled by default */ 
  vmeWrite32(&(FAp[id]->ctrl1),
	     (vmeRead32(&FAp[id]->ctrl1) & ~(FA_REF_CLK_MASK)) |
	     (clkSrc | FA_ENABLE_INTERNAL_CLK)) ;
  taskDelay(20);

  switch(clkSrc)
    {
    case FA_REF_CLK_INTERNAL:
      printf("%s: FADC id %d clock source set to INTERNAL\n",
	     __FUNCTION__,id);
      break;

    case FA_REF_CLK_FP:
      printf("%s: FADC id %d clock source set to FRONT PANEL\n",
	     __FUNCTION__,id);
      break;

    case FA_REF_CLK_P0:
      printf("%s: FADC id %d clock source set to VXS (P0)\n",
	     __FUNCTION__,id);
      break;

    case FA_REF_CLK_MASK:
      printf("%s: FADC id %d clock source set to VXS (P0)\n",
	     __FUNCTION__,id);
      break;
    }

  return OK;
}

/**
 *  @ingroup Status
 *  @brief Print Status of fADC250 to standard out
 *  @param id Slot Number
 *  @param sflag Not yet used.
 *
 */
void
faStatus(int id, int sflag)
{ 
  int ii;
  unsigned int a32Base, ambMin, ambMax, vers, bid, brev;
  unsigned int csr, ctrl1, ctrl2, count, bcount, blevel, intr, addr32, addrMB;
  unsigned int adcStat[3], adcConf[3],
    PTW, PL, NSB, NSA, NSA_tp, NP, adcChanDisabled, playbackMode;
  unsigned int nsb_reg, nsa_reg;
  unsigned int adc_enabled, adc_version, adc_option;
  unsigned int trigCnt, trig2Cnt, srCnt, itrigCnt, ramWords;
  unsigned int mgtStatus;
  unsigned int berr_count=0;
  unsigned int scaler_interval=0;
  unsigned int threshold_tp;
  unsigned int trigger_control=0;
  unsigned int lost_trig_scal=0;
  float ctrl_temp=0., proc_temp=0.;
  float core_volt=0., aux_volt=0.;
  unsigned int NPED, MAXPED, NSAT;
  unsigned int MNPED, MMAXPED;

  unsigned int adc_proc = 0;


  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return;
    }

  FALOCK;
  vers   =  vmeRead32(&FAp[id]->version);
  bid    = ((vers)&FA_BOARD_MASK)>>16;
  brev   = (vers)&FA_VERSION_MASK;

  csr    = (vmeRead32(&(FAp[id]->csr)))&FA_CSR_MASK;
  ctrl1  = (vmeRead32(&(FAp[id]->ctrl1)))&FA_CONTROL_MASK;
  ctrl2  = (vmeRead32(&(FAp[id]->ctrl2)))&FA_CONTROL2_MASK;
  count  = (vmeRead32(&(FAp[id]->ev_count)))&FA_EVENT_COUNT_MASK;
  bcount = (vmeRead32(&(FAp[id]->blk_count)))&FA_BLOCK_COUNT_MASK;
  blevel  = (vmeRead32(&(FAp[id]->blk_level)))&FA_BLOCK_LEVEL_MASK;
  ramWords = (vmeRead32(&(FAp[id]->ram_word_count)))&FA_RAM_DATA_MASK;
  itrigCnt = vmeRead32(&FAp[id]->internal_trig_scal);
  trigCnt = vmeRead32(&(FAp[id]->trig_scal));
  trig2Cnt = vmeRead32(&FAp[id]->trig2_scal);
  srCnt = vmeRead32(&FAp[id]->syncreset_scal);
  intr   = vmeRead32(&(FAp[id]->intr));
  addr32 = vmeRead32(&(FAp[id]->adr32));
  a32Base = (addr32&FA_A32_ADDR_MASK)<<16;
  addrMB = vmeRead32(&(FAp[id]->adr_mb));
  ambMin =  (addrMB&FA_AMB_MIN_MASK)<<16;
  ambMax =  (addrMB&FA_AMB_MAX_MASK);
  berr_count = vmeRead32(&(FAp[id]->berr_module_scal));

  for(ii=0;ii<3;ii++) 
    {
      adcStat[ii] = (vmeRead32(&(FAp[id]->adc_status[ii]))&0xFFFF);
      adcConf[ii] = (vmeRead32(&(FAp[id]->adc_config[ii]))&0xFFFF);
    }    

  //printf(" ----------------------   SASCHA 11111  =  0x%X \n", vmeRead32(&FAp[id]->adc_config[0]));



  PTW =  ((vmeRead32(&(FAp[id]->adc_ptw))&0xFFFF) + 1)*FA_ADC_NS_PER_CLK;
  PL  =  (vmeRead32(&(FAp[id]->adc_pl))&0xFFFF)*FA_ADC_NS_PER_CLK;
  nsb_reg =  (vmeRead32(&(FAp[id]->adc_nsb))&0xFFFF);
  nsa_reg =  (vmeRead32(&(FAp[id]->adc_nsa))&0xFFFF);
  NSB =  (nsb_reg&FA_ADC_NSB_READBACK_MASK)*FA_ADC_NS_PER_CLK;
  NSA =  (nsa_reg&FA_ADC_NSA_READBACK_MASK)*FA_ADC_NS_PER_CLK;
  NSA_tp =  ((nsa_reg&FA_ADC_TNSA_MASK)>>9)*FA_ADC_NS_PER_CLK;
  adc_version = adcStat[0]&FA_ADC_VERSION_MASK;

  // Alex
  //  adc_option  = (adcConf[0]&FA_ADC_PROC_MASK) + 1;

  adc_proc = (adcConf[0] & FA_ADC_PROC_MASK)>>8;
  if(adc_proc == 0)  adc_option = 9;
  if(adc_proc == 1)  adc_option = 10;
  if(adc_proc == 3)  adc_option = 11;

  //  adc_option  = (adcConf[0] & FA_ADC_PROC_MASK)>>8 ? 10 :9;
  


  NP          = ( ((adcConf[0]&FA_ADC_PEAK_MASK)>>4) + 1);
  adc_enabled = (adcConf[0]&FA_ADC_PROC_ENABLE);
  playbackMode = (adcConf[0]&FA_ADC_PLAYBACK_MODE)>>7;
  adcChanDisabled = (adcConf[1]&FA_ADC_CHAN_MASK);
  threshold_tp = vmeRead32(&FAp[id]->config3)&FA_ADC_TPT_MASK;
  NPED = (vmeRead32(&FAp[id]->config7) & FA_ADC_CONFIG7_NPED_MASK)>>10;
  MAXPED = vmeRead32(&FAp[id]->config7) & FA_ADC_CONFIG7_MAXPED_MASK;
  NSAT = ( ((adcConf[0] & FA_ADC_CONFIG1_NSAT_MASK)>>10) + 1);
  
  mgtStatus = vmeRead32(&(FAp[id]->mgt_status));
  scaler_interval = vmeRead32(&FAp[id]->scaler_interval) & FA_SCALER_INTERVAL_MASK;
  trigger_control = vmeRead32(&FAp[id]->trigger_control);
  lost_trig_scal  = vmeRead32(&FAp[id]->lost_trig_scal);

  MNPED    =  ( ((vmeRead32(&FAp[id]->config6) & 0x3C00)>>10) + 1);
  MMAXPED  =  (vmeRead32(&FAp[id]->config6) & 0x3FF);
               
  

  FAUNLOCK;
  ctrl_temp = faGetCtrlFPGATemp(id,0);
  proc_temp = faGetProcFPGATemp(id,0);
  core_volt = faGetCtrlFPGAVoltage(id,0,0);
  aux_volt  = faGetCtrlFPGAVoltage(id,1,0);

#ifdef VXWORKS
  printf("\nSTATUS for FADC in slot %d at base address 0x%x \n",
	 id, (UINT32) FAp[id]);
#else
  printf("\nSTATUS for FADC in slot %d at VME (Local) base address 0x%x (0x%lx)\n",
	 id, (UINT32)(unsigned long)(FAp[id] - fadcA24Offset), (unsigned long) FAp[id]);
#endif
  printf("---------------------------------------------------------------------- \n");

  printf(" Board Firmware Rev/ID = 0x%04x : ADC Processing Rev = 0x%04x\n",
	 (vers)&0xffff, adc_version);
  if(addrMB&FA_AMB_ENABLE) 
    {
      printf(" Alternate VME Addressing: Multiblock Enabled\n");
      if(addr32&FA_A32_ENABLE)
	printf("   A32 Enabled at VME (Local) base 0x%08x (0x%lx)\n",a32Base,(unsigned long) FApd[id]);
      else
	printf("   A32 Disabled\n");
    
      printf("   Multiblock VME Address Range 0x%08x - 0x%08x\n",ambMin,ambMax);
    }
  else
    {
      printf(" Alternate VME Addressing: Multiblock Disabled\n");
      if(addr32&FA_A32_ENABLE)
	printf("   A32 Enabled at VME (Local) base 0x%08x (0x%lx)\n",a32Base,(unsigned long) FApd[id]);
      else
	printf("   A32 Disabled\n");
    }

  if(ctrl1&FA_INT_ENABLE_MASK) 
    {
      printf("\n  Interrupts ENABLED: ");
      if(ctrl1&FA_ENABLE_BLKLVL_INT) printf(" on Block Level(%d)",blevel);
    
      printf("\n");
      printf("  Interrupt Reg: 0x%08x\n",intr);
      printf("  VME INT Vector = 0x%x  Level = %d\n",(intr&FA_INT_VEC_MASK),((intr&FA_INT_LEVEL_MASK)>>8));
    }

  printf("\n Signal Sources: \n");

  if((ctrl1&FA_REF_CLK_MASK)==FA_REF_CLK_INTERNAL) 
    {
      printf("   Ref Clock : Internal\n");
    }
  else if((ctrl1&FA_REF_CLK_MASK)==FA_REF_CLK_P0) 
    {
      printf("   Ref Clock : VXS\n");
    }
  else if((ctrl1&FA_REF_CLK_MASK)==FA_REF_CLK_FP) 
    {
      printf("   Ref Clock : Front Panel\n");
    }
  else
    {
      printf("   Ref Clock : %d (Undefined)\n",(ctrl1&FA_REF_CLK_MASK));
    }

  switch(ctrl1&FA_TRIG_MASK) 
    {
    case FA_TRIG_INTERNAL:
      printf("   Trig Src  : Internal\n");
      break;
    case FA_TRIG_VME:
      printf("   Trig Src  : VME (Software)\n");
      break;
    case FA_TRIG_P0_ISYNC:
      printf("   Trig Src  : VXS (Async)\n");
      break;
    case FA_TRIG_P0:
      printf("   Trig Src  : VXS (Sync)\n");
      break;
    case FA_TRIG_FP_ISYNC:
      printf("   Trig Src  : Front Panel (Async)\n");
      break;
    case FA_TRIG_FP:
      printf("   Trig Src  : Front Panel (Sync)\n");
    }  

  switch(ctrl1&FA_SRESET_MASK) 
    {
    case FA_SRESET_VME:
      printf("   Sync Reset: VME (Software)\n");
      break;
    case FA_SRESET_P0_ISYNC:
      printf("   Sync Reset: VXS (Async)\n");
      break;
    case FA_SRESET_P0:
      printf("   Sync Reset: VXS (Sync)\n");
      break;
    case FA_SRESET_FP_ISYNC:
      printf("   Sync Reset: Front Panel (Async)\n");
      break;
    case FA_SRESET_FP:
      printf("   Sync Reset: Front Panel (Sync)\n");
    }  

  if(fadcUseSDC) 
    {
      printf("   SDC       : In Use\n");
    }


  printf("\n Configuration: \n");

  if(ctrl1&FA_ENABLE_INTERNAL_CLK)
    printf("   Internal Clock ON\n");
  else
    printf("   Internal Clock OFF\n");

  if(ctrl1&FA_ENABLE_BERR)
    printf("   Bus Error ENABLED\n");
  else
    printf("   Bus Error DISABLED\n");


  if(ctrl1&FA_ENABLE_MULTIBLOCK) 
    {
      int tP0, tP2;
      tP0 = ctrl1&FA_MB_TOKEN_VIA_P0;
      tP2 = ctrl1&FA_MB_TOKEN_VIA_P2;

      if(tP0) 
	{
	  if(ctrl1&FA_FIRST_BOARD)
	    printf("   MultiBlock transfer ENABLED (First Board - token via VXS)\n");
	  else if(ctrl1&FA_LAST_BOARD)
	    printf("   MultiBlock transfer ENABLED (Last Board  - token via VXS)\n");
	  else
	    printf("   MultiBlock transfer ENABLED (Token via VXS)\n");
	}
      else if(tP2)
	{
	  if(ctrl1&FA_FIRST_BOARD)
	    printf("   MultiBlock transfer ENABLED (First Board - token via P2)\n");
	  else if(ctrl1&FA_LAST_BOARD)
	    printf("   MultiBlock transfer ENABLED (Last Board  - token via P2)\n");
	  else
	    printf("   MultiBlock transfer ENABLED (Token via P2)\n");
	}
      else
	{
	  printf("   MultiBlock transfer ENABLED (**NO Tokens enabled**)\n");
	}
    } 
  else 
    {
      printf("   MultiBlock transfer DISABLED\n");
    }

  if(ctrl1&FA_ENABLE_SOFT_TRIG)
    printf("   Software Triggers   ENABLED\n");
  if(ctrl1&FA_ENABLE_SOFT_SRESET)
    printf("   Software Sync Reset ENABLED\n");


  printf("\n ADC Processing Configuration: \n");
  printf("   Channel Disable Mask = 0x%04x\n",adcChanDisabled);
  printf("   Mode = %d  (%s)  - %s \n",
	 adc_option,
	 fa_mode_names[adc_option],
	 (adc_enabled)?"ENABLED":"Disabled");
  printf("   Lookback          (PL) = %d ns   Time Window     (PTW) = %d ns\n",PL, PTW );
  printf("   Time Before Peak (NSB) = %d ns   Time After Peak (NSA) = %d ns\n",NSB,NSA);
  printf("   Max Peak Count  (MNoP) = %d \n",NP);
  printf("   Samples in Pulse Pedestal Sum            (NPED) = %d\n",
	 NPED + 1);
  printf("   Max Pedestal for readback pedestal sum (MAXPED) = %d\n",
	 MAXPED);
  printf("   Number of Samples over TET required      (NSAT) = %d\n",
	 NSAT);

  printf("\n");
  printf("   Playback Mode          = %s \n",
	 (playbackMode)?"Enabled":"Disabled");

  printf("\n");
  printf("   Trigger Path Parameters:\n");
  printf("   Time After Peak  (TNSA)    = %d ns\n", NSA_tp);
  printf("   Threshold        (TPT)     = %d \n", threshold_tp);
  printf("   Samples over TPT (TNSAT)   = %d \n",
        ((adcConf[0] & FA_ADC_CONFIG1_TNSAT_MASK)>>12) + 1);

  printf("   Monitoring       (MNPED)   = %d \n", MNPED);
  printf("   Monitoring     (MMAXPED)   = %d \n", MMAXPED);

  printf("\n");
  printf(" Unacknowleged Trigger Stop: %s (%d)\n",
	 (trigger_control&FA_TRIGCTL_TRIGSTOP_EN) ? " ENABLED" : "DISABLED",
	 (trigger_control&FA_TRIGCTL_MAX2_MASK)>>16);
  printf(" Unacknowleged Trigger Busy: %s (%d)\n",
	 (trigger_control&FA_TRIGCTL_BUSY_EN) ? " ENABLED" : "DISABLED",
	 trigger_control&FA_TRIGCTL_MAX1_MASK);

  printf("\n");
  if(csr&FA_CSR_ERROR_MASK) 
    {
      printf("  CSR       Register = 0x%08x - **Error Condition**\n",csr);
    }
  else 
    {
      printf("  CSR       Register = 0x%08x\n",csr);
    }

  printf("  Control 1 Register = 0x%08x \n",ctrl1);


  if((ctrl2&FA_CTRL_ENABLE_MASK)==FA_CTRL_ENABLED) 
    {
      printf("  Control 2 Register = 0x%08x - Enabled for triggers\n",ctrl2);
    }
  else
    {
      printf("  Control 2 Register = 0x%08x - Disabled\n",ctrl2);
    }

  printf("  Internal Triggers (Live) = %d\n",itrigCnt);
  printf("  Trigger   Scaler         = %d\n",trigCnt);
  printf("  Trigger 2 Scaler         = %d\n",trig2Cnt);
  printf("  SyncReset Scaler         = %d\n",srCnt);
  if(trigger_control & (FA_TRIGCTL_TRIGSTOP_EN | FA_TRIGCTL_BUSY_EN))
    {
      printf("  Lost Trigger Scaler      = %d\n",lost_trig_scal);
    }

  if(scaler_interval)
    {
      printf("  Block interval for scaler events = %d\n",scaler_interval);
    }

  if(csr&FA_CSR_BLOCK_READY) 
    {
      printf("  Blocks in FIFO           = %d  (Block level = %d) - Block Available\n",bcount,blevel);
      printf("  RAM Level (Bytes)        = %d \n",(ramWords*8)); 
    }
  else if (csr&FA_CSR_EVENT_AVAILABLE) 
    {
      printf("  Events in FIFO           = %d  (Block level = %d) - Data Available\n",count,blevel);
      printf("  RAM Level (Bytes)        = %d \n",(ramWords*8)); 
    }
  else
    {
      printf("  Events in FIFO           = %d  (Block level = %d)\n",count,blevel);
    }

  printf("  MGT Status Register      = 0x%08x ",mgtStatus);
  if(mgtStatus & (FA_MGT_GTX1_HARD_ERROR | FA_MGT_GTX1_SOFT_ERROR |
		  FA_MGT_GTX2_HARD_ERROR | FA_MGT_GTX2_SOFT_ERROR))
    printf(" - **Error Condition**\n");
  else
    printf("\n");
	 
  printf("  BERR count (from module) = %d\n",berr_count);

  printf("  CTRL Temp = %3.2f [C]  PROC Temp = %3.2f [C]\n",ctrl_temp,proc_temp);
  printf("  CTRL Core = %3.2f [V] CTRL AUX  = %3.2f [V]\n",core_volt,aux_volt);

}

/**
 *  @ingroup Status
 *  @brief Print a summary of all initialized fADC250s
 *  @param sflag Not yet used
 */
void 
faGStatus(int sflag)
{
  int ifa, id, ii;
  struct fadc_struct st[FA_MAX_BOARDS+1];
  unsigned int a24addr[FA_MAX_BOARDS+1];
  float temp[FA_MAX_BOARDS+1][2];
  float volt[FA_MAX_BOARDS+1][2];

  unsigned int adc_proc = 0;

  FALOCK;
  for (ifa=0;ifa<nfadc;ifa++) 
    {
      id = faSlot(ifa);
      a24addr[id]    = (unsigned int)((unsigned long)FAp[id] - fadcA24Offset);
      st[id].version = vmeRead32(&FAp[id]->version);
      st[id].adr32   = vmeRead32(&FAp[id]->adr32);
      st[id].adr_mb  = vmeRead32(&FAp[id]->adr_mb);

      st[id].ctrl1   = vmeRead32(&FAp[id]->ctrl1);
      st[id].ctrl2   = vmeRead32(&FAp[id]->ctrl2);

      st[id].csr     = vmeRead32(&FAp[id]->csr);
      
      st[id].config6 =  vmeRead32(&FAp[id]->config6);

      st[id].config7 =  vmeRead32(&FAp[id]->config7);


      for(ii=0;ii<3;ii++) 
	{
	  st[id].adc_status[ii] =  vmeRead32(&FAp[id]->adc_status[ii])&0xFFFF;
	  st[id].adc_config[ii] =  vmeRead32(&FAp[id]->adc_config[ii])&0xFFFF;
	}    
      st[id].adc_ptw = vmeRead32(&FAp[id]->adc_ptw);
      st[id].adc_pl  = vmeRead32(&FAp[id]->adc_pl);
      st[id].adc_nsb = vmeRead32(&FAp[id]->adc_nsb);
      st[id].adc_nsa = vmeRead32(&FAp[id]->adc_nsa);

      st[id].config3 = vmeRead32(&FAp[id]->config3);
      st[id].blk_count = vmeRead32(&FAp[id]->blk_count);
      st[id].blk_level = vmeRead32(&FAp[id]->blk_level);
      st[id].ram_word_count = vmeRead32(&FAp[id]->ram_word_count)&FA_RAM_DATA_MASK;

      st[id].trig_scal      = vmeRead32(&(FAp[id]->trig_scal));
      st[id].trig2_scal     = vmeRead32(&FAp[id]->trig2_scal);
      st[id].syncreset_scal = vmeRead32(&FAp[id]->syncreset_scal);
      st[id].berr_module_scal = vmeRead32(&FAp[id]->berr_module_scal);
      st[id].lost_trig_scal = vmeRead32(&FAp[id]->lost_trig_scal);

      st[id].mgt_status = vmeRead32(&FAp[id]->mgt_status);

      for(ii = 0;ii < FA_MAX_ADC_CHANNELS;ii++){
        st[id].adc_pedestal[ii] = vmeRead32(&FAp[id]->adc_pedestal[ii]) & FA_ADC_PEDESTAL_MASK;
      }

    }
  FAUNLOCK;
  for (ifa=0;ifa<nfadc;ifa++) 
    {
      id = faSlot(ifa);
      temp[id][0] = faGetCtrlFPGATemp(id,0);
      temp[id][1] = faGetProcFPGATemp(id,0);
      volt[id][1] = faGetCtrlFPGAVoltage(id,1,0);
      volt[id][0] = faGetCtrlFPGAVoltage(id,0,0);
    }

  printf("\n");
  
  printf("                      fADC250 Module Configuration Summary\n\n");
  printf("     Firmware Rev   .................Addresses................\n");
  printf("Slot  Ctrl   Proc      A24        A32     A32 Multiblock Range\n");
  printf("--------------------------------------------------------------------------------\n");

  for(ifa=0; ifa<nfadc; ifa++)
    {
      id = faSlot(ifa);
      printf(" %2d  ",id);

      printf("0x%04x 0x%04x  ",st[id].version&0xFFFF, 
	     st[id].adc_status[0]&FA_ADC_VERSION_MASK);

      printf("0x%06x  ",
	     a24addr[id]);

      if(st[id].adr32 &FA_A32_ENABLE)
	{
	  printf("0x%08x  ",
		 (st[id].adr32&FA_A32_ADDR_MASK)<<16);
	}
      else
	{
	  printf("  Disabled  ");
	}

      if(st[id].adr_mb & FA_AMB_ENABLE) 
	{
	  printf("0x%08x-0x%08x",
		 (st[id].adr_mb&FA_AMB_MIN_MASK)<<16,
		 (st[id].adr_mb&FA_AMB_MAX_MASK));
	}
      else
	{
	  printf("Disabled");
	}

      printf("\n");
    }
  printf("--------------------------------------------------------------------------------\n");


  printf("\n");
  printf("      .Signal Sources..                        ..Channel...\n");
  printf("Slot  Clk   Trig   Sync     MBlk  Token  BERR  Enabled Mask\n");
  printf("--------------------------------------------------------------------------------\n");
  for(ifa=0; ifa<nfadc; ifa++)
    {
      id = faSlot(ifa);
      printf(" %2d  ",id);

      printf("%s  ", 
	     (st[id].ctrl1 & FA_REF_CLK_MASK)==FA_REF_CLK_INTERNAL ? " INT " :
	     (st[id].ctrl1 & FA_REF_CLK_MASK)==FA_REF_CLK_P0 ? " VXS " :
	     (st[id].ctrl1 & FA_REF_CLK_MASK)==FA_REF_CLK_FP ? "  FP " :
	     " ??? ");

      printf("%s  ",
	     (st[id].ctrl1 & FA_TRIG_MASK)==FA_TRIG_INTERNAL ? " INT " :
	     (st[id].ctrl1 & FA_TRIG_MASK)==FA_TRIG_VME ? " VME " :
	     (st[id].ctrl1 & FA_TRIG_MASK)==FA_TRIG_P0_ISYNC ? " VXS " :
	     (st[id].ctrl1 & FA_TRIG_MASK)==FA_TRIG_FP_ISYNC ? "  FP " :
	     (st[id].ctrl1 & FA_TRIG_MASK)==FA_TRIG_P0 ? " VXS " :
	     (st[id].ctrl1 & FA_TRIG_MASK)==FA_TRIG_FP ? "  FP " :
	     " ??? ");

      printf("%s    ",
	     (st[id].ctrl1 & FA_SRESET_MASK)==FA_SRESET_VME ? " VME " :
	     (st[id].ctrl1 & FA_SRESET_MASK)==FA_SRESET_P0_ISYNC ? " VXS " :
	     (st[id].ctrl1 & FA_SRESET_MASK)==FA_SRESET_FP_ISYNC ? "  FP " :
	     (st[id].ctrl1 & FA_SRESET_MASK)==FA_SRESET_P0 ? " VXS " :
	     (st[id].ctrl1 & FA_SRESET_MASK)==FA_SRESET_FP ? "  FP " :
	     " ??? ");

      printf("%s   ",
	     (st[id].ctrl1 & FA_ENABLE_MULTIBLOCK) ? "YES":" NO");

      printf("%s",
	     st[id].ctrl1 & (FA_MB_TOKEN_VIA_P0)?" P0":
	     st[id].ctrl1 & (FA_MB_TOKEN_VIA_P2)?" P0":
	     " NO");
      printf("%s  ",
	     st[id].ctrl1 & (FA_FIRST_BOARD) ? "-F":
	     st[id].ctrl1 & (FA_LAST_BOARD) ? "-L":
	     "  ");

      printf("%s     ",
	     st[id].ctrl1 & FA_ENABLE_BERR ? "YES" : " NO");

      printf("0x%04X",
	     ~(st[id].adc_config[1] & FA_ADC_CHAN_MASK) & 0xFFFF);

      printf("\n");
    }
  printf("--------------------------------------------------------------------------------\n");

  printf("\n");
  printf("                        fADC250 Processing Mode Config\n\n");
  printf("      Block          ...[nanoseconds]...       [ns]\n");
  printf("Slot  Level  Mode    PL   PTW   NSB  NSA  NP   NPED  MAXPED  NSAT   Playback   \n");
  printf("--------------------------------------------------------------------------------\n");
  // NEED TO ADD IN NPED, MAXPED, and NSAT
  for(ifa=0; ifa<nfadc; ifa++)
    {
      id = faSlot(ifa);
      printf(" %2d    ",id);

      printf("%3d    ",st[id].blk_level & FA_BLOCK_LEVEL_MASK);

      // Alex
      //      printf("%d    ",(st[id].adc_config[0] & FA_ADC_PROC_MASK) + 1);
      if((st[id].adc_config[0] & FA_ADC_PROC_MASK)>>8 == 0) adc_proc = 9;
      if((st[id].adc_config[0] & FA_ADC_PROC_MASK)>>8 == 1) adc_proc = 10;
      if((st[id].adc_config[0] & FA_ADC_PROC_MASK)>>8 == 3) adc_proc = 11;
      printf("%d    ",adc_proc);

      //      printf("%d    ",(st[id].adc_config[0] & FA_ADC_PROC_MASK)>>8 ? 10 : 9);


      printf("%4d  ", (st[id].adc_pl & 0xFFFF)*FA_ADC_NS_PER_CLK);

      printf("%4d   ", ((st[id].adc_ptw & 0xFFFF) + 1)*FA_ADC_NS_PER_CLK);

      printf("%3d  ", (st[id].adc_nsb & FA_ADC_NSB_READBACK_MASK)*FA_ADC_NS_PER_CLK);

      printf("%3d   ", (st[id].adc_nsa & FA_ADC_NSB_READBACK_MASK)*FA_ADC_NS_PER_CLK);

      printf("%1d     ", ((st[id].adc_config[0] & FA_ADC_PEAK_MASK)>>4) + 1);

      printf("%2d    ", (((st[id].config7 & FA_ADC_CONFIG7_NPED_MASK)>>10) + 1)*FA_ADC_NS_PER_CLK);
      
      printf("%4d     ", st[id].config7 & FA_ADC_CONFIG7_MAXPED_MASK);
      
      printf("%d   ", ((st[id].adc_config[0] & FA_ADC_CONFIG1_NSAT_MASK)>>10) + 1);
      
      printf("%s   ",
	     (st[id].adc_config[0] &FA_ADC_PLAYBACK_MODE)>>7 ?" Enabled":"Disabled");

      printf("\n");
    }
  printf("--------------------------------------------------------------------------------\n");

  printf("\n");
  printf("                     fADC250 Trigger Path Processing Config\n\n");
  printf("         [ns]                   \n");
  printf("Slot     TNSA      TPT     TNSAT     MNPED     MMAXPED \n");
  printf("--------------------------------------------------------------------------------\n");
  for(ifa=0; ifa<nfadc; ifa++)
    {
      id = faSlot(ifa);
      printf(" %2d       ",id);
      
      printf("%3d     ", ((st[id].adc_nsa & FA_ADC_TNSA_MASK)>>9)*FA_ADC_NS_PER_CLK);
      
      printf("%4d         ", st[id].config3 & FA_ADC_TPT_MASK);

      printf("%d    ", (((st[id].adc_config[0] &FA_ADC_CONFIG1_TNSAT_MASK)>>12) + 1));

      printf("%4d        ", (( (st[id].config6 & 0x3C00 )>>10) + 1));
      
      printf("%4d   ", ( st[id].config6 & 0x3FF ));

      printf("\n");
    }
  printf("--------------------------------------------------------------------------------\n");

  printf("\n");
  printf("                        fADC250 Signal Scalers\n\n");
  printf("Slot       Trig1       Trig2   SyncReset        BERR  Lost Triggers\n");
  printf("--------------------------------------------------------------------------------\n");
  for(ifa=0; ifa<nfadc; ifa++)
    {
      id = faSlot(ifa);
      printf(" %2d   ",id);

      printf("%10d  ", st[id].trig_scal);

      printf("%10d  ", st[id].trig2_scal);

      printf("%10d  ", st[id].syncreset_scal);

      printf("%10d     ", st[id].berr_module_scal);

      printf("%10d  ", st[id].lost_trig_scal);

      printf("\n");
    }
  printf("--------------------------------------------------------------------------------\n");

  printf("\n");
  printf("                        fADC250 Data Status\n\n");
  printf("      Trigger   Block                              Error Status\n");
  printf("Slot  Source    Ready  Blocks In Fifo  RAM Level   CSR     MGT\n");
  printf("--------------------------------------------------------------------------------\n");
  for(ifa=0; ifa<nfadc; ifa++)
    {
      id = faSlot(ifa);
      printf(" %2d  ",id);

      printf("%s    ",
	     st[id].ctrl2 & FA_CTRL_ENABLE_MASK ? " Enabled" : "Disabled");

      printf("%s       ",
	     st[id].csr & FA_CSR_BLOCK_READY ? "YES" : " NO");

      printf("%10d ",
	     st[id].blk_count&FA_BLOCK_COUNT_MASK);

      printf("%10d  ",
	     (st[id].ram_word_count&FA_RAM_DATA_MASK)*8);

      printf("%s   ",
	     st[id].csr & FA_CSR_ERROR_MASK ? "ERROR" : "  OK " );

      printf("%s  ",
	     st[id].mgt_status & 
	     (FA_MGT_GTX1_HARD_ERROR | FA_MGT_GTX1_SOFT_ERROR |
	      FA_MGT_GTX2_HARD_ERROR | FA_MGT_GTX2_SOFT_ERROR) ? "ERROR" : "  OK " );

      printf("\n");
    }
  printf("--------------------------------------------------------------------------------\n");

  printf("\n");
  printf("                        fADC250 FPGA Monitoring\n\n");
  printf("      ...........Control...........      Processing      ..Temperature Alarm..\n");
  printf("Slot  Temp [C]   Core [V]   Aux [V]       Temp [C]       Control    Processing\n");
  printf("--------------------------------------------------------------------------------\n");
  for(ifa=0; ifa<nfadc; ifa++)
    {
      id = faSlot(ifa);
      printf(" %2d    ",id);
      printf(" %3.1f       ",temp[id][0]);
      printf("%2.1f        ",volt[id][0]);
      printf("%2.1f         ",volt[id][1]);
      printf(" %3.1f           ",temp[id][1]);
      printf("%s",(st[id].csr & FA_CSR_CTRL_FPGA_HIGH_TEMP)?
	     "*ALARM*    ":
	     "  OK       ");
      printf("%s",(st[id].csr & FA_CSR_PROC_FPGA_HIGH_TEMP)?
	     "*ALARM*":
	     "  OK");
      printf("\n");
    }
  printf("--------------------------------------------------------------------------------\n");

  printf("\n");
  printf("\n");


  if(sflag == 2){
    
    for(ifa = 0; ifa < nfadc; ifa++){
      
      id = faSlot(ifa);
      
      printf(" Trigger Pedestal Settings for FADC in slot %d \n ", id);
      
      for(ii = 0;ii < FA_MAX_ADC_CHANNELS;ii++){
        
        if((ii % 4)==0) printf("\n");
        printf("Chan %2d: %5d   ",(ii+1), st[id].adc_pedestal[ii]  );

      }

      printf("\n");
    }
    
  }


}

/**
 *  @ingroup Status
 *  @brief Get the firmware versions of each FPGA
 *
 *  @param id Slot number
 *  @param  pval
 *    -  0: Print nothing to stdout
 *    - !0: Print firmware versions to stdout
 *
 *  @return (fpga_control.version) | (fpga_processing.version<<16)
 *            or -1 if error
 */
unsigned int
faGetFirmwareVersions(int id, int pflag)
{
  unsigned int cntl=0, proc=0, rval=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : FADC in slot %d is not initialized \n",__FUNCTION__,id);
      return(ERROR);
    }
  
  FALOCK;
  /* Control FPGA firmware version */
  cntl = vmeRead32(&FAp[id]->version) & 0xFFFF;

  /* Processing FPGA firmware version */
  proc = vmeRead32(&(FAp[id]->adc_status[0]))&FA_ADC_VERSION_MASK;
  FAUNLOCK;

  rval = (cntl) | (proc<<16);

  if(pflag)
    {
      printf("%s:  Board Firmware Rev/ID = 0x%04x : ADC Processing Rev = 0x%04x\n",
	     __FUNCTION__,
	     cntl, proc);
    }

  return rval;
}

/**
 *  @ingroup Config
 *  @brief Configure the processing type/mode
 *
 *  @param id Slot number
 *  @param pmode  Processing Mode
 *     -     1 - Raw Window
 *     -     2 - Pulse Raw Window
 *     -     3 - Pulse Integral
 *     -     4 - High-resolution time
 *     -     7 - Mode 3 + Mode 4
 *     -     8 - Mode 1 + Mode 4
 *  @param  PL  Window Latency
 *  @param PTW  Window Width
 *  @param NSB  Number of samples before pulse over threshold
 *  @param NSA  Number of samples after pulse over threshold
 *  @param NP   Number of pulses processed per window
 *  @param bank Ignored
 *
 *    Note:
 *     - PL must be greater than PTW
 *     - NSA+NSB must be an odd number
 *
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetProcMode(int id, int pmode, unsigned int PL, unsigned int PTW, 
	      unsigned int NSB, unsigned int NSA, unsigned int NP, 
	      unsigned int NPED, unsigned int MAXPED, unsigned int NSAT)
{
  
  int err=0;
  int imode=0, supported_modes[FA_SUPPORTED_NMODES] = {FA_SUPPORTED_MODES};
  int mode_supported=0;
  int mode_bit=0;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetProcMode: ERROR : FADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  for(imode=0; imode<FA_SUPPORTED_NMODES; imode++)
    {
      if(pmode == supported_modes[imode])
	mode_supported=1;
    }
  if(!mode_supported)
    {
      printf("%s: ERROR: Processing Mode (%d) not supported\n",
	     __FUNCTION__,pmode);
      return ERROR;
    }
  
  /* Set Min/Max parameters if specified values are out of bounds */
  if((PL < FA_ADC_MIN_PL) || (PL > FA_ADC_MAX_PL))  
    {
      printf("%s: WARN: PL (%d) out of bounds.  ",__FUNCTION__,PL);
      PL  = (PL < FA_ADC_MIN_PL) ? FA_ADC_MIN_PL : FA_ADC_MAX_PL;
      printf("Setting to %d.\n",PL);
    }

  if((PTW < FA_ADC_MIN_PTW) || (PTW > FA_ADC_MAX_PTW)) 
    {
      printf("%s: WARN: PTW (%d) out of bounds.  ",__FUNCTION__,PTW);
      PTW = (PTW < FA_ADC_MIN_PTW) ? FA_ADC_MIN_PTW : FA_ADC_MAX_PTW;
      printf("Setting to %d.\n",PTW);
    }

  if((NSB < FA_ADC_MIN_NSB) || (NSB > FA_ADC_MAX_NSB)) 
    {
      printf("%s: WARN: NSB (%d) out of bounds.  ",__FUNCTION__,NSB);
      NSB = (NSB < FA_ADC_MIN_NSB) ? FA_ADC_MIN_NSB : FA_ADC_MAX_NSB;
      printf("Setting to %d.\n",NSB);
    }


  // Alex
  //  if((NSA < FA_ADC_MIN_NSA) || (NSA > FA_ADC_MAX_NSA)) 
  //    {
  //      printf("%s: WARN: NSA (%d) out of bounds.  ",__FUNCTION__,NSA);
  //      NSA = (NSA < FA_ADC_MIN_NSA) ? FA_ADC_MIN_NSA : FA_ADC_MAX_NSA;
  //      if(((NSB + NSA) % 2)==0) /* Make sure NSA+NSB is an odd number */
  //	NSA = (NSA==FA_ADC_MIN_NSA) ? NSA + 1 : NSA - 1;
  //      printf("Setting to %d.\n",NSA);
  //    }


  //   if( (NSB & 0x8) && (NSA <= NSB) )
  //    {
  //      printf("%s: WARN: ", __FUNCTION__);
  //    }

  
  if((NP < FA_ADC_MIN_NP) || (NP > FA_ADC_MAX_NP))
    {
      printf("%s: WARN: NP (%d) out of bounds.  ",__FUNCTION__,NP);
      NP = (NP < FA_ADC_MIN_NP) ? FA_ADC_MIN_NP : FA_ADC_MAX_NP;
      printf("Setting to %d.\n",NP);
    }

  if((NPED < FA_ADC_MIN_NPED) || (NPED > FA_ADC_MAX_NPED))
    {
      printf("%s: WARN: NPED (%d) out of bounds.  ",__FUNCTION__,NPED);
      NPED = (NPED < FA_ADC_MIN_NPED) ? FA_ADC_MIN_NPED : FA_ADC_MAX_NPED;
      printf("Setting to %d.\n",NPED);
    }

  if(NPED >= PTW)
    {
      printf("%s: WARN: NPED (%d) >= PTW (%d)  ",__FUNCTION__, NPED, PTW);
      NPED = PTW - 1;
      printf("Setting to %d.\n",NPED);
    }
  
  
  if((MAXPED < FA_ADC_MIN_MAXPED) || (MAXPED > FA_ADC_MAX_MAXPED))
    {
      printf("%s: WARN: MAXPED (%d) out of bounds.  ",__FUNCTION__,MAXPED);
      MAXPED = (MAXPED < FA_ADC_MIN_MAXPED) ? FA_ADC_MIN_MAXPED : FA_ADC_MAX_MAXPED;
      printf("Setting to %d.\n",MAXPED);
    }

  if((NSAT < FA_ADC_MIN_NSAT) || (NSAT > FA_ADC_MAX_NSAT))
    {
      printf("%s: WARN: NSAT (%d) out of bounds.  ",__FUNCTION__,NSAT);
      NSAT = (NSAT < FA_ADC_MIN_NSAT) ? FA_ADC_MIN_NSAT : FA_ADC_MAX_NSAT;
      printf("Setting to %d.\n",NSAT);
    }

  /* Consistancy check */
  // Alex
  //  if(((NSB + NSA) % 2)==0) 
  //    {
  //      err++;
  //      printf("%s: ERROR: NSB+NSA must be an odd number\n",__FUNCTION__); 
  //    }




  //  faSetNormalMode(id,0);

  //  faSetNormalMode(id,0);


  FALOCK;
  /* Disable ADC processing while writing window info */
  if(pmode == FA_ADC_PROC_MODE_PULSE_PARAM)
    mode_bit = 0;
  else if(pmode == FA_ADC_PROC_MODE_RAW_PULSE_PARAM)
    mode_bit = 1;
  else if(pmode == FA_ADC_PROC_MODE_RAW)
    mode_bit = 3;
  else
    {
      printf("%s: ERROR: Unsupported mode (%d)\n",
	     __FUNCTION__, pmode);
      return ERROR;
    }


  //printf(" ----------------------   SASCHA =  %d   %d \n", pmode, mode_bit);




  /* Configure the mode (mode_bit), # of pulses (NP), # samples above TET (NSAT)
     keep TNSAT, if it's already been configured */
  vmeWrite32(&FAp[id]->adc_config[0],
	     (vmeRead32(&FAp[id]->adc_config[0]) & FA_ADC_CONFIG1_TNSAT_MASK) |
	     (mode_bit << 8) | ((NP-1) << 4) | ((NSAT-1) << 10) );

  //printf(" ----------------------   SASCHA =  0x%X \n", vmeRead32(&FAp[id]->adc_config[0]));
  

  /* Disable user-requested channels */
  vmeWrite32(&FAp[id]->adc_config[1], fadcChanDisable[id]);

  /* Set window parameters */
  vmeWrite32(&FAp[id]->adc_pl, PL);
  vmeWrite32(&FAp[id]->adc_ptw, PTW - 1);

  /* Set Readback NSB, NSA
     NSA */
  vmeWrite32(&FAp[id]->adc_nsb, NSB);
  vmeWrite32(&FAp[id]->adc_nsa,
	     (vmeRead32(&FAp[id]->adc_nsa) & FA_ADC_TNSA_MASK) |
	     NSA );

  /* Set Pedestal parameters */
  vmeWrite32(&FAp[id]->config7, (NPED-1)<<10 | (MAXPED));


  /* Set default value of trigger path threshold (TPT) */
  vmeWrite32(&FAp[id]->config3, FA_ADC_DEFAULT_TPT);

  /* Enable ADC processing */
  vmeWrite32(&FAp[id]->adc_config[0],
	     vmeRead32(&FAp[id]->adc_config[0]) | FA_ADC_PROC_ENABLE );


  FAUNLOCK;

  //  faSetTriggerStopCondition(id, faCalcMaxUnAckTriggers(pmode,PTW,NSA,NSB,NP));
  //  faSetTriggerBusyCondition(id, faCalcMaxUnAckTriggers(pmode,PTW,NSA,NSB,NP));

  return(OK);
}

/**
 *  @ingroup Config
 *  @brief Configure the processing type/mode for all initialized fADC250s
 *
 *  @param pmode  Processing Mode
 *     -     1 - Raw Window
 *     -     2 - Pulse Raw Window
 *     -     3 - Pulse Integral
 *     -     4 - High-resolution time
 *     -     7 - Mode 3 + Mode 4
 *     -     8 - Mode 1 + Mode 4
 *  @param  PL  Window Latency
 *  @param PTW  Window Width
 *  @param NSB  Number of samples before pulse over threshold
 *  @param NSA  Number of samples after pulse over threshold
 *  @param NP   Number of pulses processed per window
 *  @param bank Ignored
 *
 *  @sa faSetProcMode
 */
void
faGSetProcMode(int pmode, unsigned int PL, unsigned int PTW, 
	       unsigned int NSB, unsigned int NSA, unsigned int NP, 
	       unsigned int NPED, unsigned int MAXPED, unsigned int NSAT)
{
  int ii, res;

  for (ii=0;ii<nfadc;ii++) 
    {
      res = faSetProcMode(fadcID[ii], pmode, PL, PTW, NSB, NSA, NP, NPED, MAXPED, NSAT);
      if(res<0) 
	printf("ERROR: slot %d, in faSetProcMode()\n", fadcID[ii]);
    }
}

/**
 *  @ingroup Config
 *  @brief Return the maximum number of unacknowledged triggers a specific
 *         mode can handle.
 *
 *  @param pmode  Processing Mode
 *  @param ptw  Window Width
 *  @param nsb  Number of samples before pulse over threshold
 *  @param nsa  Number of samples after pulse over threshold
 *  @param np   Number of pulses processed per window
 *
 *  @return The minimum of 9 and the calculated maximum number of triggers
 *    allowed given specified mode and window paramters.
 */

int
faCalcMaxUnAckTriggers(int mode, int ptw, int nsa, int nsb, int np)
{
  int max;
  int imode=0, supported_modes[FA_SUPPORTED_NMODES] = {FA_SUPPORTED_MODES};
  int mode_supported=0;

  for(imode=0; imode<FA_SUPPORTED_NMODES; imode++)
    {
      if(mode == supported_modes[imode])
	mode_supported=1;
    }
  if(!mode_supported)
    {
      printf("%s: ERROR: Processing Mode (%d) not supported\n",
	     __FUNCTION__,mode);
      return ERROR;
    }

  switch(mode)
    {
    case 1:
      max = (int)(2040/(ptw + 7));
      break;

    case 2:
      max = (int)(2040/((2+nsa+nsb)*np));
      break;

    case 3:
      max = (int)(2040/(5*np));
      break;

    case 4:
      max = (int)(2040/(4*np));
      break;

    case 7:
      max = (int)(2040/((5*np)+(4*np)));
      break;

    case 8:
      max = (int)(2040/((ptw + 7)+(4*np)));
      break;

    default:
      printf("%s: ERROR: Mode %d is not supported\n",
	     __FUNCTION__,mode);
    }


  return ((max < 9) ? max : 9);
}

/**
 *  @ingroup Config
 *  @brief Set the maximum number of unacknowledged triggers before module
 *         stops accepting incoming triggers. 
 *  @param id Slot number
 *  @param trigger_max Limit for maximum number of unacknowledged triggers. 
 *         If 0, disables the condition.
 *  @return OK if successful, otherwise ERROR.
 */

int
faSetTriggerStopCondition(int id, int trigger_max)
{
  if(id==0) id=fadcID[0];
  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : FADC in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  if(trigger_max>0xFF)
    {
      printf("%s: ERROR: Invalid trigger_max (%d)\n",
	     __FUNCTION__,trigger_max);
      return ERROR;
    }

  FALOCK;
  if(trigger_max>0)
    {
      vmeWrite32(&FAp[id]->trigger_control,
		 (vmeRead32(&FAp[id]->trigger_control) & 
		  ~(FA_TRIGCTL_TRIGSTOP_EN | FA_TRIGCTL_MAX2_MASK)) |
		 (FA_TRIGCTL_TRIGSTOP_EN | (trigger_max<<16)));
    }
  else
    {
      vmeWrite32(&FAp[id]->trigger_control,
		 (vmeRead32(&FAp[id]->trigger_control) & 
		  ~(FA_TRIGCTL_TRIGSTOP_EN | FA_TRIGCTL_MAX2_MASK)));
    }
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Set the maximum number of unacknowledged triggers before module
 *         asserts BUSY. 
 *  @param id Slot number
 *  @param trigger_max Limit for maximum number of unacknowledged triggers
 *         If 0, disables the condition
 *  @return OK if successful, otherwise ERROR.
 */

int
faSetTriggerBusyCondition(int id, int trigger_max)
{
  if(id==0) id=fadcID[0];
  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : FADC in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  if(trigger_max>0xFF)
    {
      printf("%s: ERROR: Invalid trigger_max (%d)\n",
	     __FUNCTION__,trigger_max);
      return ERROR;
    }

  FALOCK;
  if(trigger_max>0)
    {
      vmeWrite32(&FAp[id]->trigger_control,
		 (vmeRead32(&FAp[id]->trigger_control) & 
		  ~(FA_TRIGCTL_BUSY_EN | FA_TRIGCTL_MAX1_MASK)) |
		 (FA_TRIGCTL_BUSY_EN | (trigger_max)));
    }
  else
    {
      vmeWrite32(&FAp[id]->trigger_control,
		 (vmeRead32(&FAp[id]->trigger_control) & 
		  ~(FA_TRIGCTL_BUSY_EN | FA_TRIGCTL_MAX1_MASK)));
    }
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Set the number of samples that are included before and after
 *    threshold crossing that are sent through the trigger path
 *  @param id Slot number
 *  @param NSB Number of samples before threshold crossing
 *  @param NSA Number of samples after threshold crossing
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetTriggerPathSamples(int id, unsigned int TNSA, unsigned int TNSAT)
{
  unsigned int readback_nsa=0, readback_config1=0;

  if(id==0) id=fadcID[0];
  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
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

  if((TNSA < FA_ADC_MIN_TNSA) || (TNSA > FA_ADC_MAX_TNSA)) 
    {
      printf("%s: WARN: TNSA (%d) out of range. Setting to %d\n",
	     __FUNCTION__,
	     TNSA, FA_ADC_DEFAULT_TNSA);
      TNSA = FA_ADC_DEFAULT_TNSA;
    }

  if((TNSAT < FA_ADC_MIN_TNSAT) || (TNSAT > FA_ADC_MAX_TNSAT)) 
    {
      printf("%s: WARN: TNSAT (%d) out of range. Setting to %d\n",
	     __FUNCTION__,
	     TNSAT, FA_ADC_DEFAULT_TNSAT);
      TNSAT = FA_ADC_DEFAULT_TNSAT;
    }

  FALOCK;

  readback_nsa     = vmeRead32(&FAp[id]->adc_nsa)       & FA_ADC_NSA_READBACK_MASK;
  readback_config1 = vmeRead32(&FAp[id]->adc_config[0]) & ~FA_ADC_CONFIG1_TNSAT_MASK;

  vmeWrite32(&FAp[id]->adc_nsa,       (TNSA  << 9)  | readback_nsa);
  vmeWrite32(&FAp[id]->adc_config[0], ((TNSAT-1) << 12) | readback_config1);

  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Set the number of samples that are included before and after
 *    threshold crossing that are sent through the trigger path for 
 *    all initialized fADC250s
 *  @param NSB Number of samples before threshold crossing
 *  @param NSA Number of samples after threshold crossing
 *  @sa faSetTriggerPathSamples
 */
void
faGSetTriggerPathSamples(unsigned int TNSA, unsigned int TNSAT)
{
  int ii, res;

  for (ii=0;ii<nfadc;ii++) 
    {
      res = faSetTriggerPathSamples(fadcID[ii], TNSA, TNSAT);
      if(res<0) printf("ERROR: slot %d, in faSetTriggerPathSamples()\n",fadcID[ii]);
    }
}

/**
 *  @ingroup Config
 *  @brief Set the threshold used to determine what samples are sent through the
 *     trigger path
 *  @param id Slot number
 *  @param threshold Trigger Path Threshold
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetTriggerPathThreshold(int id, unsigned int TPT)
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
  
  if(TPT>FA_ADC_MAX_TPT)
    {
      printf("%s: WARN: TPT (%d) greater than MAX.  Setting to %d\n",
	     __FUNCTION__, TPT, FA_ADC_MAX_TPT);
      TPT = FA_ADC_MAX_TPT;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->config3, 
	     (vmeRead32(&FAp[id]->config3) & ~FA_ADC_CONFIG3_TPT_MASK) | 
	     TPT);
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Set the threshold used to determine what samples are sent through the
 *     trigger path for all initialized fADC250s
 *  @param threshold Trigger Path Threshold
 *  @sa faSetTriggerPathThreshold
 */
void
faGSetTriggerPathThreshold(unsigned int TPT)
{
  int ii, res;

  for (ii=0;ii<nfadc;ii++) 
    {
      res = faSetTriggerPathThreshold(fadcID[ii], TPT);
      if(res<0) printf("ERROR: slot %d, in faSetTriggerPathThreshold()\n",fadcID[ii]);
    }
}

/**
 *  @ingroup Config
 *  @brief Static routine, to wait for the ADC processing chip ready bit
 *     before proceeding with further programming
 *  @param id Slot number
 *
 */
static void
faWaitForAdcReady(int id)
{
  int iwait=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) {
    printf("%s: ERROR : FADC in slot %d is not initialized \n",__FUNCTION__,id);
    return;
  }

  while((iwait<100) && (vmeRead32(&FAp[id]->adc_status[0])&0x8000)==0)
    {
      iwait++;
    }

  //  printf(" Board %d  iwait %d \n", id, iwait);

  if(iwait==100)
    printf("%s: ERROR: Wait timeout.\n",__FUNCTION__);

}

/**
 *  @ingroup Config
 *  @brief Configure the ADC Processing in "Normal Mode"
 * 
 *    This routine is called in faSetProcMode
 *
 *  @param id Slot number
 *  @param opt Not Used
 *
 */
void
faSetNormalMode(int id, int opt)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) {
    logMsg("faSetNormalMode: ERROR : FADC in slot %d qis not initialized \n",id,0,0,0,0,0);
    return;
  }


  unsigned int read_status = 0;

  FALOCK;

  taskDelay(1);
  vmeWrite32(&FAp[id]->adc_config[2], 0);
  taskDelay(1);
  vmeWrite32(&FAp[id]->adc_config[2], 0x10);
  taskDelay(1);
  vmeWrite32(&FAp[id]->adc_config[2], 0);
  taskDelay(1);

  



  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[3], 0x0F02);
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[2], 0x40);
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[2], 0xC0);

  // Read back

  //  taskDelay(1);
  //  vmeWrite32(&FAp[id]->adc_config[2], 0x20);
  //  vmeWrite32(&FAp[id]->adc_config[2], 0xA0);
  //  taskDelay(2);

  //  read_status = vmeRead32(&FAp[id]->status4);


  //  printf(" Read back F02 from chip 1  0x%x \n", read_status);



  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[3], 0x179F);
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[2], 0x40);
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[2], 0xC0);
	    
  /* 01dec2011 This portion commented out... would change the input gain */
  /*   faWaitForAdcReady(id); */
  /*   vmeWrite32(&FAp[id]->adc_config[3], 0x1811); */
  /*   faWaitForAdcReady(id); */
  /*   vmeWrite32(&FAp[id]->adc_config[2], 0x40); */
  /*   faWaitForAdcReady(id); */
  /*   vmeWrite32(&FAp[id]->adc_config[2], 0xC0);	 */
	    
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[3], 0xFF01);		/* transfer register values */
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[2], 0x40);
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[2], 0xC0);
	    
  printf("%s: ---- FADC %2d ADC chips initialized ----\n",
	 __FUNCTION__,id);
	        
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[3], 0x0D00);
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[2], 0x40);
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[2], 0xC0);
	    
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[3], 0xFF01);		/* transfer register values */
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[2], 0x40);
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[2], 0xC0);

  FAUNLOCK;


}


/**
 *  @ingroup Config
 *  @brief Configure the ADC Processing in "Inverted (positive polarity) Mode"
 * 
 *    Call this routine to invert the digital output of the ADC chips for each channel.
 *    This routine MUST be called after faSetProcMode.
 *
 *  @sa faSetProcMode
 *  @param id Slot number
 *
 */
void
faSetInvertedMode(int id)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) {
    logMsg("faSetInvertedMode: ERROR : FADC in slot %d is not initialized \n",id,0,0,0,0,0);
    return;
  }

  FALOCK;
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[3], 0x1404);
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[2], 0x40);
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[2], 0x40 | 0x80);
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[2], 0x40);

  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[3], 0xFF01);
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[2], 0x40);
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[2], 0x40 | 0x80);
  faWaitForAdcReady(id);
  vmeWrite32(&FAp[id]->adc_config[2], 0x40);
  FAUNLOCK;
}

/**
 *  @ingroup Config
 *  @brief Setup FADC Progammable Pulse Generator
 *
 *  @param id Slot number
 *  @param pmode  Not used
 *  @param sdata  Array of sample data to be programmed
 *  @param nsamples Number of samples contained in sdata
 *
 *  @sa faPPGEnable faPPGDisable
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetPPG(int id, int pmode, unsigned short *sdata, int nsamples)
{
  
  int ii, diff;
  unsigned short rval;


  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetPPG: ERROR : FADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  if(sdata == NULL) 
    {
      printf("faSetPPG: ERROR: Invalid Pointer to sample data\n");
      return(ERROR);
    }

  /*Defaults */
  if((nsamples <= 0)||(nsamples>FA_PPG_MAX_SAMPLES)) nsamples = FA_PPG_MAX_SAMPLES;
  diff = FA_PPG_MAX_SAMPLES - nsamples;

  FALOCK;
  for(ii=0;ii<(nsamples-2);ii++) 
    {
      vmeWrite32(&FAp[id]->adc_test_data, (sdata[ii]|FA_PPG_WRITE_VALUE));
      rval = vmeRead32(&FAp[id]->adc_test_data);
      if( (rval&FA_PPG_SAMPLE_MASK) != sdata[ii])
	printf("faSetPPG: ERROR: Write error %x != %x (ii=%d)\n",rval, sdata[ii],ii);

    }

  vmeWrite32(&FAp[id]->adc_test_data, (sdata[(nsamples-2)]&FA_PPG_SAMPLE_MASK));
  rval = vmeRead32(&FAp[id]->adc_test_data);
  if(rval != sdata[(nsamples-2)])
    printf("faSetPPG: ERROR: Write error %x != %x\n",
	   rval, sdata[nsamples-2]);
  vmeWrite32(&FAp[id]->adc_test_data, (sdata[(nsamples-1)]&FA_PPG_SAMPLE_MASK));
  rval = vmeRead32(&FAp[id]->adc_test_data);
  if(rval != sdata[(nsamples-1)])
    printf("faSetPPG: ERROR: Write error %x != %x\n",
	   rval, sdata[nsamples-1]);
    
  /*   vmeWrite32(&FAp[id]->adc_test_data, (sdata[(nsamples-2)]&FA_PPG_SAMPLE_MASK)); */
  /*   vmeWrite32(&FAp[id]->adc_test_data, (sdata[(nsamples-1)]&FA_PPG_SAMPLE_MASK)); */
    
  FAUNLOCK;
  
  return(OK);
}

/**
 *  @ingroup Config
 *  @brief Enable the programmable pulse generator
 *  @param id Slot number
 *  @sa faSetPPG faPPGDisable
 */
void
faPPGEnable(int id)
{
  unsigned short val1;

  if(id==0) id=fadcID[0];
  
  FALOCK;
  val1 = (vmeRead32(&FAp[id]->adc_config[0])&0xFFFF);


  //  val1 |= (FA_PPG_ENABLE | 0xff00); 

  // Alex
  val1 |= FA_PPG_ENABLE; 

  vmeWrite32(&FAp[id]->adc_config[0], val1);
  FAUNLOCK;
  

  printf(" PPGEnable adc_config[0] 0x%x \n", val1);

}

/**
 *  @ingroup Config
 *  @brief Disable the programmable pulse generator
 *  @param id Slot number
 *  @sa faSetPPG faPPGEnable
 */
void
faPPGDisable(int id)
{
  unsigned short val1;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faPPGDisable: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }

  FALOCK;
  val1 = (vmeRead32(&FAp[id]->adc_config[0])&0xFFFF);


  val1 &= ~FA_PPG_ENABLE;

  // Alex
  //  val1 &= ~(0xff00);
  vmeWrite32(&FAp[id]->adc_config[0], val1);
  FAUNLOCK;

}

/**
 *  @ingroup Readout
 *  @brief General Data readout routine
 *
 *  @param  id     Slot number of module to read
 *  @param  data   local memory address to place data
 *  @param  nwrds  Max number of words to transfer
 *  @param  rflag  Readout Flag
 * <pre>
 *              0 - programmed I/O from the specified board
 *              1 - DMA transfer using Universe/Tempe DMA Engine 
 *                    (DMA VME transfer Mode must be setup prior)
 *              2 - Multiblock DMA transfer (Multiblock must be enabled
 *                     and daisychain in place or SD being used)
 * </pre>
 *  @return Number of words inserted into data if successful.  Otherwise ERROR.
 */
int
faReadBlock(int id, volatile UINT32 *data, int nwrds, int rflag)
{
  int ii, blknum, evnum1;
  int stat, retVal, xferCount, rmode, async;
  int dCnt, berr=0;
  int dummy=0;
  volatile unsigned int *laddr;
  unsigned int bhead, ehead, val;
  unsigned int vmeAdr, csr;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faReadBlock: ERROR : FADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  if(data==NULL) 
    {
      logMsg("faReadBlock: ERROR: Invalid Destination address\n",0,0,0,0,0,0);
      return(ERROR);
    }

  fadcBlockError=FA_BLOCKERROR_NO_ERROR;
  if(nwrds <= 0) nwrds= (FA_MAX_ADC_CHANNELS*FA_MAX_DATA_PER_CHANNEL) + 8;
  rmode = rflag&0x0f;
  async = rflag&0x80;
  
  if(rmode >= 1) 
    { /* Block Transfers */
    
      /*Assume that the DMA programming is already setup. */
      /* Don't Bother checking if there is valid data - that should be done prior
	 to calling the read routine */

      /* Check for 8 byte boundary for address - insert dummy word (Slot 0 FADC Dummy DATA)*/
      if((unsigned long) (data)&0x7) 
	{
#ifdef VXWORKS
	  *data = FA_DUMMY_DATA;
#else
	  *data = LSWAP(FA_DUMMY_DATA);
#endif
	  dummy = 1;
	  laddr = (data + 1);
	} 
      else 
	{
	  dummy = 0;
	  laddr = data;
	}

      FALOCK;
      if(rmode == 2) 
	{ /* Multiblock Mode */
	  if((vmeRead32(&(FAp[id]->ctrl1))&FA_FIRST_BOARD)==0) 
	    {
	      logMsg("faReadBlock: ERROR: FADC in slot %d is not First Board\n",id,0,0,0,0,0);
	      FAUNLOCK;
	      return(ERROR);
	    }
	  vmeAdr = (unsigned int)((unsigned long)(FApmb) - fadcA32Offset);
	}
      else
	{
	  vmeAdr = (unsigned int)((unsigned long)(FApd[id]) - fadcA32Offset);
	}
#ifdef VXWORKS
      retVal = sysVmeDmaSend((UINT32)laddr, vmeAdr, (nwrds<<2), 0);
#else
      retVal = vmeDmaSend((unsigned long)laddr, vmeAdr, (nwrds<<2));
#endif
      if(retVal != 0) 
	{
	  logMsg("faReadBlock: ERROR in DMA transfer Initialization 0x%x\n",retVal,0,0,0,0,0);
	  FAUNLOCK;
	  return(retVal);
	}

      if(async) 
	{ /* Asynchonous mode - return immediately - don't wait for done!! */
	  FAUNLOCK;
	  return(OK);
	}
      else
	{
	  /* Wait until Done or Error */
#ifdef VXWORKS
	  retVal = sysVmeDmaDone(10000,1);
#else
	  retVal = vmeDmaDone();
#endif
	}

      if(retVal > 0) 
	{
	  /* Check to see that Bus error was generated by FADC */
	  if(rmode == 2) 
	    {
	      csr = vmeRead32(&(FAp[fadcMaxSlot]->csr));  /* from Last FADC */
	      stat = (csr)&FA_CSR_BERR_STATUS;  /* from Last FADC */
	    }
	  else
	    {
	      csr = vmeRead32(&(FAp[id]->csr));  /* from Last FADC */
	      stat = (csr)&FA_CSR_BERR_STATUS;  /* from Last FADC */
	    }
	  if((retVal>0) && (stat)) 
	    {
#ifdef VXWORKS
	      xferCount = (nwrds - (retVal>>2) + dummy);  /* Number of Longwords transfered */
#else
	      xferCount = ((retVal>>2) + dummy);  /* Number of Longwords transfered */
	      /* 	xferCount = (retVal + dummy);  /\* Number of Longwords transfered *\/ */
#endif
	      FAUNLOCK;
	      return(xferCount); /* Return number of data words transfered */
	    }
	  else
	    {
#ifdef VXWORKS
	      xferCount = (nwrds - (retVal>>2) + dummy);  /* Number of Longwords transfered */
	      logMsg("faReadBlock: DMA transfer terminated by unknown BUS Error (csr=0x%x xferCount=%d id=%d)\n",
		     csr,xferCount,id,0,0,0);
	      fadcBlockError=FA_BLOCKERROR_UNKNOWN_BUS_ERROR;
#else
	      xferCount = ((retVal>>2) + dummy);  /* Number of Longwords transfered */
	      if((retVal>>2)==nwrds)
		{
		  logMsg("faReadBlock: WARN: DMA transfer terminated by word count 0x%x\n",nwrds,0,0,0,0,0);
		  fadcBlockError=FA_BLOCKERROR_TERM_ON_WORDCOUNT;
		}
	      else
		{
		  logMsg("faReadBlock: DMA transfer terminated by unknown BUS Error (csr=0x%x xferCount=%d id=%d)\n",
			 csr,xferCount,id,0,0,0);
		  fadcBlockError=FA_BLOCKERROR_UNKNOWN_BUS_ERROR;
		}
#endif
	      FAUNLOCK;
	      return(xferCount);
	      /* 	return(ERROR); */
	    }
	} 
      else if (retVal == 0)
	{ /* Block Error finished without Bus Error */
#ifdef VXWORKS
	  logMsg("faReadBlock: WARN: DMA transfer terminated by word count 0x%x\n",nwrds,0,0,0,0,0);
#else
	  logMsg("faReadBlock: WARN: DMA transfer returned zero word count 0x%x\n",nwrds,0,0,0,0,0);
#endif
	  fadcBlockError=FA_BLOCKERROR_ZERO_WORD_COUNT;
	  FAUNLOCK;
	  return(nwrds);
	} 
      else 
	{  /* Error in DMA */
#ifdef VXWORKS
	  logMsg("faReadBlock: ERROR: sysVmeDmaDone returned an Error\n",0,0,0,0,0,0);
#else
	  logMsg("faReadBlock: ERROR: vmeDmaDone returned an Error\n",0,0,0,0,0,0);
#endif
	  fadcBlockError=FA_BLOCKERROR_DMADONE_ERROR;
	  FAUNLOCK;
	  return(retVal>>2);
	}

    } 
  else 
    {  /*Programmed IO */

      /* Check if Bus Errors are enabled. If so then disable for Prog I/O reading */
      FALOCK;
      berr = vmeRead32(&(FAp[id]->ctrl1))&FA_ENABLE_BERR;
      if(berr)
	vmeWrite32(&(FAp[id]->ctrl1),vmeRead32(&(FAp[id]->ctrl1)) & ~FA_ENABLE_BERR);

      dCnt = 0;
      /* Read Block Header - should be first word */
      bhead = (unsigned int) *FApd[id]; 
#ifndef VXWORKS
      bhead = LSWAP(bhead);
#endif
      if((bhead&FA_DATA_TYPE_DEFINE)&&((bhead&FA_DATA_TYPE_MASK) == FA_DATA_BLOCK_HEADER)) {
	blknum = bhead&FA_DATA_BLKNUM_MASK;
	ehead = (unsigned int) *FApd[id];
#ifndef VXWORKS
	ehead = LSWAP(ehead);
#endif
	evnum1 = ehead&FA_DATA_TRIGNUM_MASK;
#ifdef VXWORKS
	data[dCnt] = bhead;
#else
	data[dCnt] = LSWAP(bhead); /* Swap back to little-endian */
#endif
	dCnt++;
#ifdef VXWORKS
	data[dCnt] = ehead;
#else
	data[dCnt] = LSWAP(ehead); /* Swap back to little-endian */
#endif
	dCnt++;
      }
      else
	{
	  /* We got bad data - Check if there is any data at all */
	  if( (vmeRead32(&(FAp[id]->ev_count)) & FA_EVENT_COUNT_MASK) == 0) 
	    {
	      logMsg("faReadBlock: FIFO Empty (0x%08x)\n",bhead,0,0,0,0,0);
	      FAUNLOCK;
	      return(0);
	    } 
	  else 
	    {
	      logMsg("faReadBlock: ERROR: Invalid Header Word 0x%08x\n",bhead,0,0,0,0,0);
	      FAUNLOCK;
	      return(ERROR);
	    }
	}

      ii=0;
      while(ii<nwrds) 
	{
	  val = (unsigned int) *FApd[id];
	  data[ii+2] = val;
#ifndef VXWORKS
	  val = LSWAP(val);
#endif
	  if( (val&FA_DATA_TYPE_DEFINE) 
	      && ((val&FA_DATA_TYPE_MASK) == FA_DATA_BLOCK_TRAILER) )
	    break;
	  ii++;
	}
      ii++;
      dCnt += ii;


      if(berr)
	vmeWrite32(&(FAp[id]->ctrl1),
		   vmeRead32(&(FAp[id]->ctrl1)) | FA_ENABLE_BERR);

      FAUNLOCK;
      return(dCnt);
    }

  FAUNLOCK;
  return(OK);
}

/**
 *  @ingroup Status
 *  @brief Return the type of error that occurred while attempting a
 *    block read from faReadBlock.
 *  @param pflag
 *     - >0: Print error message to standard out
 *  @sa faReadBlock
 *  @return OK if successful, otherwise ERROR.
 */
int
faGetBlockError(int pflag)
{
  int rval=0;
  const char *block_error_names[FA_BLOCKERROR_NTYPES] =
    {
      "NO ERROR",
      "DMA Terminated on Word Count",
      "Unknown Bus Error",
      "Zero Word Count",
      "DmaDone Error"
    };

  rval = fadcBlockError;
  if(pflag)
    {
      if(rval!=FA_BLOCKERROR_NO_ERROR)
	{
	  logMsg("faGetBlockError: Block Transfer Error: %s\n",
		 block_error_names[rval],2,3,4,5,6);
	}
    }

  return rval;
}

/**
 *  @ingroup Readout
 *  @brief For asychronous calls to faReadBlock, this routine completes the block transfer
 *  @param  id     Slot number of module to read
 *  @param  data   local memory address to place data
 *  @param  nwrds  Max number of words to transfer
 *  @param  rflag  Readout Flag
 * <pre>
 *              0 - programmed I/O from the specified board
 *              1 - DMA transfer using Universe/Tempe DMA Engine 
 *                    (DMA VME transfer Mode must be setup prior)
 *              2 - Multiblock DMA transfer (Multiblock must be enabled
 *                     and daisychain in place or SD being used)
 * </pre>
 *  @return Number of words read if successful, otherwise ERROR.
 */
int
faReadBlockStatus(int id, volatile UINT32 *data, int nwrds, int rflag)
{

  int stat, retVal, xferCount, rmode, async;
  int dummy=0;
  unsigned int csr=0;
  
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faReadBlockStatus: ERROR : FADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  if(nwrds <= 0) nwrds= (FA_MAX_ADC_CHANNELS*FA_MAX_DATA_PER_CHANNEL) + 8;
  rmode = rflag&0x0f;
  async = rflag&0x80;

  /* Check for 8 byte boundary for address - insert dummy word (Slot 0 FADC Dummy DATA)*/
  if((unsigned long) (data)&0x7) 
    {
      dummy = 1;
    } 
  else 
    {
      dummy = 0;
    }

#ifdef VXWORKS
  retVal = sysVmeDmaDone(10000,1);
#else
  retVal = vmeDmaDone();
#endif

  FALOCK;
  if(retVal > 0) 
    {
      /* Check to see that Bus error was generated by FADC */
      if(rmode == 2) 
	{
	  csr = vmeRead32(&(FAp[fadcMaxSlot]->csr));  /* from Last FADC */
	  stat = (csr)&FA_CSR_BERR_STATUS;  /* from Last FADC */
	}
      else
	{
	  stat = vmeRead32(&(FAp[id]->csr))&FA_CSR_BERR_STATUS;  /* from FADC id */
	}
      if((retVal>0) && (stat)) 
	{
	  xferCount = (nwrds - (retVal>>2) + dummy);  /* Number of Longwords transfered */
	  FAUNLOCK;
	  return(xferCount); /* Return number of data words transfered */
	}
      else
	{
	  xferCount = (nwrds - (retVal>>2) + dummy);  /* Number of Longwords transfered */
	  logMsg("faReadBlockStatus: DMA transfer terminated by unknown BUS Error (csr=0x%x nwrds=%d)\n",csr,xferCount,0,0,0,0);
	  FAUNLOCK;
	  return(ERROR);
	}
    } 
  else if (retVal == 0)
    { /* Block Error finished without Bus Error */
      logMsg("faReadBlockStatus: WARN: DMA transfer terminated by word count 0x%x\n",nwrds,0,0,0,0,0);
      FAUNLOCK;
      return(nwrds);
    } 
  else 
    {  /* Error in DMA */
      logMsg("faReadBlockStatus: ERROR: sysVmeDmaDone returned an Error\n",0,0,0,0,0,0);
      FAUNLOCK;
      return(retVal);
    }
  
}

/**
 *  @ingroup Readout
 *  @brief Print the current available block to standard out
 *  @param id Slot number
 *  @param rflag Not used
 *  @return Number of words read if successful, otherwise ERROR.
 */
int
faPrintBlock(int id, int rflag)
{

  int ii, blknum, evnum1;
  int nwrds=32768, dCnt, berr=0;
  unsigned int data, bhead, ehead;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("faPrintEvent: ERROR : FADC in slot %d is not initialized \n",id);
      return(ERROR);
    }

  /* Check if data available */
  FALOCK;
  if((vmeRead32(&(FAp[id]->ev_count))&FA_EVENT_COUNT_MASK)==0) 
    {
      printf("faPrintEvent: ERROR: FIFO Empty\n");
      FAUNLOCK;
      return(0);
    }

  /* Check if Bus Errors are enabled. If so then disable for reading */
  berr = vmeRead32(&(FAp[id]->ctrl1))&FA_ENABLE_BERR;
  if(berr)
    vmeWrite32(&(FAp[id]->ctrl1),
	       vmeRead32(&(FAp[id]->ctrl1)) & ~FA_ENABLE_BERR);
  
  dCnt = 0;
  /* Read Block Header - should be first word */
  bhead = (unsigned int) *FApd[id];
#ifndef VXWORKS
  bhead = LSWAP(bhead);
#endif
  if( (bhead&FA_DATA_TYPE_DEFINE)&&((bhead&FA_DATA_TYPE_MASK) == FA_DATA_BLOCK_HEADER)) 
    {
      blknum = bhead&FA_DATA_BLKNUM_MASK;
      ehead = (unsigned int) *FApd[id];
#ifndef VXWORKS
      ehead = LSWAP(ehead);
#endif
      evnum1 = ehead&FA_DATA_TRIGNUM_MASK;
      printf("%4d: ",dCnt+1); 
      faDataDecode(bhead);
      dCnt++;
      printf("%4d: ",dCnt+1); 
      faDataDecode(ehead);
      dCnt++;
    }
  else
    {
      /* We got bad data - Check if there is any data at all */
      if((vmeRead32(&(FAp[id]->ev_count))&FA_EVENT_COUNT_MASK)==0) 
	{
	  logMsg("faPrintBlock: FIFO Empty (0x%08x)\n",bhead,0,0,0,0,0);
	  FAUNLOCK;
	  return(0);
	} 
      else 
	{
	  logMsg("faPrintBlock: ERROR: Invalid Header Word 0x%08x\n",bhead,0,0,0,0,0);
	  FAUNLOCK;
	  return(ERROR);
	}
    }
  
  ii=0;
  while(ii<nwrds) 
    {
      data = (unsigned int) *FApd[id];
#ifndef VXWORKS
      data = LSWAP(data);
#endif
      printf("%4d: ",dCnt+1+ii); 
      faDataDecode(data);
      if((data&FA_DATA_TYPE_DEFINE)&&((data&FA_DATA_TYPE_MASK) == FA_DATA_BLOCK_TRAILER))
	break;
      if((data&FA_DATA_TYPE_DEFINE)&&((data&FA_DATA_TYPE_MASK) == FA_DATA_INVALID))
	break;
      ii++;
    }
  ii++;
  dCnt += ii;


  if(berr)
    vmeWrite32(&(FAp[id]->ctrl1),
	       vmeRead32( &(FAp[id]->ctrl1)) | FA_ENABLE_BERR );
  
  FAUNLOCK;
  return(dCnt);
  
}

/**
 *  @ingroup Status
 *  @brief Get the value of the Control/Status Register
 *  @param id Slot number
 *  @return OK if successful, otherwise ERROR.
 */
unsigned int
faReadCSR(int id)
{
  unsigned int rval;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faReadCSR: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(0);
    }
  
  FALOCK;
  rval = vmeRead32(&(FAp[id]->csr));
  FAUNLOCK;
  
  return(rval);
}


/**
 *  @ingroup Config
 *  @brief Perform a soft reset.
 *  @param id Slot number
 */
void
faClear(int id)
{

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faClear: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
  FALOCK;
  vmeWrite32(&(FAp[id]->csr),FA_CSR_SOFT_RESET);
  FAUNLOCK;
}

/**
 *  @ingroup Config
 *  @brief Perform a soft reset of all initialized fADC250s
 */
void
faGClear()
{

  int ii, id;

  FALOCK;
  for(ii=0;ii<nfadc;ii++) 
    {
      id = fadcID[ii];
      if((id<=0) || (id>21) || (FAp[id] == NULL)) 
	{
	  logMsg("faGClear: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
	}
      else
	{
	  vmeWrite32(&(FAp[id]->csr),FA_CSR_SOFT_RESET);
	}
    }
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Clear latched errors
 *  @param id Slot number
 */
void
faClearError(int id)
{

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faClearErr: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
  
  FALOCK;
  vmeWrite32(&(FAp[id]->csr),FA_CSR_ERROR_CLEAR);
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Clear latched errors of all initialized fADC250s
 */
void
faGClearError()
{

  int ii, id;

  FALOCK;
  for(ii=0;ii<nfadc;ii++) 
    {
      id = fadcID[ii];
      if((id<=0) || (id>21) || (FAp[id] == NULL)) 
	{
	  logMsg("faGClearErr: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
	}
      else
	{
	  vmeWrite32(&(FAp[id]->csr),FA_CSR_ERROR_CLEAR);
	}
    }
  FAUNLOCK;

}


/**
 *  @ingroup Config
 *  @brief Perform a hard reset
 *  @param id Slot number
 *  @param iFlag Decision to restore A32 readout after reset.
 *     -  0: Restore A32 readout after reset.
 *     - !0: Do not restore A32 readout after reset. (Useful for configuration changes)
 */
void
faReset(int id, int iFlag)
{
  unsigned int a32addr, addrMB;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faReset: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }

  FALOCK;
  if(iFlag==0)
    {
      a32addr = vmeRead32(&(FAp[id]->adr32));
      addrMB  = vmeRead32(&(FAp[id]->adr_mb));
    }

  vmeWrite32(&(FAp[id]->csr),FA_CSR_HARD_RESET);
  taskDelay(10);

  if(iFlag==0)
    {
      vmeWrite32(&(FAp[id]->adr32),a32addr);
      vmeWrite32(&(FAp[id]->adr_mb),addrMB);
    }
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Perform a hard reset on all initialized fADC250s
 *  @param iFlag Decision to restore A32 readout after reset.
 *     -  0: Restore A32 readout after reset.
 *     - !0: Do not restore A32 readout after reset. (Useful for configuration changes)
 */
void
faGReset(int iFlag)
{
  unsigned int a32addr[(FA_MAX_BOARDS+1)], addrMB[(FA_MAX_BOARDS+1)];
  int ifa=0, id=0;

  FALOCK;
  if(iFlag==0)
    {
      for(ifa=0; ifa<nfadc; ifa++)
	{
	  id = faSlot(ifa);
	  a32addr[id] = vmeRead32(&(FAp[id]->adr32));
	  addrMB[id]  = vmeRead32(&(FAp[id]->adr_mb));
	}
    }
  
  for(ifa=0; ifa<nfadc; ifa++)
    {
      id = faSlot(ifa);
      vmeWrite32(&(FAp[id]->csr),FA_CSR_HARD_RESET);
    }

  taskDelay(10);

  if(iFlag==0)
    {
      for(ifa=0; ifa<nfadc; ifa++)
	{
	  id = faSlot(ifa);
	  vmeWrite32(&(FAp[id]->adr32),a32addr[id]);
	  vmeWrite32(&(FAp[id]->adr_mb),addrMB[id]);
	}
    }

  FAUNLOCK;

}



/**
 *  @ingroup Config
 *  @brief Perform either a soft clear or soft reset
 *  @param id Slot number
 *  @param cflag 
 *    -  0: Soft Clear
 *    - >0: Soft Reset
 */
void
faSoftReset(int id, int cflag)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faReset: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }

  FALOCK;
  if(cflag) /* perform soft clear */
    vmeWrite32(&(FAp[id]->csr),FA_CSR_SOFT_CLEAR);
  else      /* normal soft reset */
    vmeWrite32(&(FAp[id]->csr),FA_CSR_SOFT_RESET);
  FAUNLOCK;
  
}

/**
 *  @ingroup Config
 *  @brief Reset the token
 *
 *     A call to this routine will cause the module to have the token if it
 *     has been configured to the the FIRST module in the MultiBlock chain.
 *     This routine has no effect on any other module in the chain.
 *
 *  @param id Slot number
 */
void
faResetToken(int id)
{

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faResetToken: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
  
  FALOCK;
  vmeWrite32(&(FAp[id]->reset),FA_RESET_TOKEN);
  FAUNLOCK;
}

/**
 *  @ingroup Status
 *  @brief Return the status of the token 
 *  @param id Slot number
 *  @return 1 if module has the token, 0 if not, otherwise ERROR.
 */
int
faTokenStatus(int id)
{
  int rval=0;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faResetToken: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return ERROR;
    }
  
  FALOCK;
  rval = (vmeRead32(&FAp[id]->csr) & FA_CSR_TOKEN_STATUS)>>4;
  FAUNLOCK;

  return rval;
}

/**
 *  @ingroup Status
 *  @brief Return the slotmask of those modules that have the token.
 *  @return Token Slotmask
 */
int
faGTokenStatus()
{
  int ifa=0, bit=0, rval=0;

  for(ifa = 0; ifa<nfadc; ifa++)
    {
      bit = faTokenStatus(faSlot(ifa));
      rval |= (bit<<(faSlot(ifa)));
    }

  return rval;
}

/**
 *  @ingroup Deprec
 *  @brief Set the SyncReset and Trigger Delay
 *
 *    This routine has no effect on the module. (remnant of v1)
 *
 *  @param id Slot number
 *  @param sdelay SyncReset delay
 *  @param tdelay Trigger1 delay
 */
void
faSetCalib(int id, unsigned short sdelay, unsigned short tdelay)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetCalib: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
  
  FALOCK;
  vmeWrite32(&(FAp[id]->delay),(sdelay<<16) | tdelay);
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Disable the specified channel
 *  @param id Slot number
 *  @param channel Channel Number to Disable
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetChannelDisable(int id, int channel)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetChannelDisable: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return ERROR;
    }

  if((channel<0) || (channel>=FA_MAX_ADC_CHANNELS))
    {
      logMsg("faSetChannelDisable: ERROR: Invalid channel (%d).  Must be 0-%d\n",
	     channel, FA_MAX_ADC_CHANNELS-1,3,4,5,6);
      return ERROR;
    }

  fadcChanDisable[id] = fadcChanDisable[id] | (1<<channel);

  FALOCK;
  /* Write New Disable Mask */
  vmeWrite32(&(FAp[id]->adc_config[1]), fadcChanDisable[id]);
  FAUNLOCK;
  
  return OK;
}

/**
 *  @ingroup Config
 *  @brief Disable all channels in the specified mask
 *  @param id Slot number
 *  @param cmask Channel mask of channels to disable
 */
void
faChanDisable(int id, unsigned short cmask)
{
  faSetChannelDisableMask(id,cmask);
}

/**
 *  @ingroup Config
 *  @brief Disable all channels in the specified mask
 *  @param id Slot number
 *  @param cmask Channel mask of channels to disable
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetChannelDisableMask(int id, unsigned short cmask)
{

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faChanDisable: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return ERROR;
    }

  fadcChanDisable[id] = cmask;  /* Set Global Variable */

  FALOCK;
  /* Write New Disable Mask */
  vmeWrite32(&(FAp[id]->adc_config[1]), fadcChanDisable[id]);
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Enable the specified channel
 *  @param id Slot number
 *  @param channel Channel Number to Enable
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetChannelEnable(int id, int channel)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetChannelEnable: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return ERROR;
    }

  if((channel<0) || (channel>=FA_MAX_ADC_CHANNELS))
    {
      logMsg("faSetChannelEnable: ERROR: Invalid channel (%d).  Must be 0-%d\n",
	     channel, FA_MAX_ADC_CHANNELS-1,3,4,5,6);
      return ERROR;
    }

  fadcChanDisable[id] = fadcChanDisable[id] & (~(1<<channel) & 0xFFFF);

  FALOCK;
  /* Write New Disable Mask */
  vmeWrite32(&(FAp[id]->adc_config[1]), fadcChanDisable[id]);
  FAUNLOCK;
  
  return OK;
}

/**
 *  @ingroup Config
 *  @brief Enable all channels in the specified mask
 *  @param id Slot number
 *  @param cmask Channel mask of channels to Enable
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetChannelEnableMask(int id, unsigned short enMask)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetChannelEnableMask: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return ERROR;
    }

  fadcChanDisable[id] = ~(enMask) & FA_ADC_CHAN_MASK;

  FALOCK;
  /* Write New Disable Mask */
  vmeWrite32(&FAp[id]->adc_config[1], fadcChanDisable[id]);
  FAUNLOCK;
  
  return OK;
}

/**
 *  @ingroup Status
 *  @brief Get the Enabled/Disabled Channel Mask
 *  @param id Slot number
 *  @param type
 *   - 0: Return the disabled Mask
 *   - !0: Return the enabled Mask
 *  @return Specified mask if successful, otherwise ERROR.
 */
int
faGetChannelMask(int id, int type)
{
  int rval=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faGetChannelMask: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return ERROR;
    }

  FALOCK;
  fadcChanDisable[id] = vmeRead32(&FAp[id]->adc_config[1]) & FA_ADC_CHAN_MASK;
  FAUNLOCK;

  if(type!=0)
    rval = (~fadcChanDisable[id]) & FA_ADC_CHAN_MASK;
  else
    rval = fadcChanDisable[id];

  return rval;
}

/**
 *  @ingroup Config
 *  @brief Enabled the SyncReset source
 *  @param id Slot number
 */
void
faEnableSyncSrc(int id)
{
  unsigned int reg=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faEnableSyncSrc: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
    
  FALOCK;
  vmeWrite32(&(FAp[id]->ctrl2), FA_CTRL_GO | FA_CTRL_ENABLE_SRESET);

  /* Keep this bit set, if it is */
  reg = vmeRead32(&FAp[id]->mgt_ctrl) & FA_MGT_HITBITS_TO_CTP; 

  if(fadcAlignmentDebug)
    {
      /* Disable front end channel data */
      vmeWrite32(&FAp[id]->adc_config[1],0xffff);

      printf("%s: Enabling alignment debugging sequence\n",__FUNCTION__);
      /* Enable data alignment debugging sequence */
      vmeWrite32(&FAp[id]->mgt_ctrl,FA_MGT_RESET | reg);
      vmeWrite32(&FAp[id]->mgt_ctrl,FA_RELEASE_MGT_RESET | reg);
      vmeWrite32(&FAp[id]->mgt_ctrl,FA_MGT_ENABLE_DATA_ALIGNMENT | reg);
      printf("  mgt_ctrl = 0x%08x\n",vmeRead32(&FAp[id]->mgt_ctrl));
    }
  else
    {
      //Alex
      vmeWrite32(&FAp[id]->mgt_ctrl,FA_MGT_RESET | reg);

      vmeWrite32(&FAp[id]->mgt_ctrl,FA_RELEASE_MGT_RESET | reg);
      vmeWrite32(&FAp[id]->mgt_ctrl, FA_MGT_FRONT_END_TO_CTP | FA_MGT_ENABLE_DATA_ALIGNMENT | reg);
    }

  FAUNLOCK;

  /* Allow time for the fADC250 to CTP lanes to come back up */
  taskDelay(1);
}

/**
 *  @ingroup Config
 *  @brief Enable the SyncReset Source of all initialized fADC250s
 */
void
faGEnableSyncSrc()
{
  int id=0;
  for(id=0;id<nfadc;id++)
    faEnableSyncSrc(fadcID[id]);

}

/**
 *  @ingroup Config
 *  @brief Enable data acquisition, trigger, and SyncReset on the module 
 *  @param id Slot number
 *  @param eflag Enable Internal Trigger Logic, as well
 *  @param bank  Not used
 */
void
faEnable(int id, int eflag, int bank)
{
  unsigned int reg=0, stat=0;
  int itimeout=0, timeout=10;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faEnable: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }

  FALOCK;

#if 0

  /* Wait until PROC has finished processing buffered triggers */
  stat = vmeRead32(&FAp[id]->adc_status[1]) & FA_ADC_STATUS1_TRIG_RCV_DONE;

  while( (stat == 0) && (itimeout < timeout) )
    {
      taskDelay(1);
      stat = vmeRead32(&FAp[id]->adc_status[1]) & FA_ADC_STATUS1_TRIG_RCV_DONE;
      itimeout++;
    }

  if(stat==0)
    {
      logMsg("faEnable: ERROR: ADC in slot %d NOT READY to Enable.\n",id,0,0,0,0,0);
      FAUNLOCK;
      return;
    }

#endif


  /* Keep this bit set, if it is */
  reg = vmeRead32(&FAp[id]->mgt_ctrl) & FA_MGT_HITBITS_TO_CTP; 

  if(fadcAlignmentDebug)
    {
      /* Disable data alignment debugging sequence */
      vmeWrite32(&FAp[id]->mgt_ctrl, FA_MGT_FRONT_END_TO_CTP | FA_MGT_ENABLE_DATA_ALIGNMENT | reg);
      /* Re-enable front end channel data */
      vmeWrite32(&FAp[id]->adc_config[1], fadcChanDisable[id]);
    }


  if(eflag)
    {  /* Enable Internal Trigger logic as well*/
      vmeWrite32(&(FAp[id]->ctrl2),
		 FA_CTRL_GO | FA_CTRL_ENABLE_TRIG | FA_CTRL_ENABLE_SRESET |
		 FA_CTRL_ENABLE_INT_TRIG);
    }
  else
    {
      vmeWrite32(&(FAp[id]->ctrl2),
		 FA_CTRL_GO | FA_CTRL_ENABLE_TRIG | FA_CTRL_ENABLE_SRESET);
    }

  FAUNLOCK;
}

/**
 *  @ingroup Config
 *  @brief Enable data acquisition, trigger, and SyncReset on all initialized fADC250s
 *
 *    Also enables the SDC if it is initalized and used.
 *
 *  @param eflag Enable Internal Trigger Logic, as well
 *  @param bank  Not used
 */
void
faGEnable(int eflag, int bank)
{
  int ii;

  for(ii=0;ii<nfadc;ii++)
    faEnable(fadcID[ii],eflag,bank);

  if(fadcUseSDC)
    faSDC_Enable(1);

}

/**
 *  @ingroup Config
 *  @brief Disable data acquisition, triggers, and SyncReset on the module
 *  @param id Slot number
 *  @param eflag
 *    - >0: Turn off FIFO transfer as well.
 */
void
faDisable(int id, int eflag)
{
  unsigned int stat=0;
  int itimeout=0, timeout=10;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faDisable: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }

  FALOCK;
  /* Wait until PROC has finished processing buffered triggers */
  stat = vmeRead32(&FAp[id]->adc_status[1]) & FA_ADC_STATUS1_TRIG_RCV_DONE;

  while( (stat == 0) && (itimeout < timeout) )
    {
      taskDelay(1);
      stat = vmeRead32(&FAp[id]->adc_status[1]) & FA_ADC_STATUS1_TRIG_RCV_DONE;
      itimeout++;
    }

  if(stat==0)
    {
      logMsg("faDisable: ERROR: ADC in slot %d NOT READY to Disable.\n",id,0,0,0,0,0);
      FAUNLOCK;
      return;
    }

  if(eflag)
    vmeWrite32(&(FAp[id]->ctrl2),0);   /* Turn FIFO Transfer off as well */
  else
    vmeWrite32(&(FAp[id]->ctrl2),FA_CTRL_GO);
  FAUNLOCK;
}

/**
 *  @ingroup Config
 *  @brief Disable data acquisition, triggers, and SyncReset on all initialized fADC250s
 *  @param eflag
 *    - >0: Turn off FIFO transfer as well.
 */
void
faGDisable(int eflag)
{
  int ii;

  if(fadcUseSDC)
    faSDC_Disable();

  for(ii=0;ii<nfadc;ii++)
    faDisable(fadcID[ii],eflag);

}

/**
 *  @ingroup Readout
 *  @brief Pulse a software trigger to the module.
 *  @param id Slot number
 */
void
faTrig(int id)
{

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faTrig: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }

  FALOCK;
  if( vmeRead32(&(FAp[id]->ctrl1)) & (FA_ENABLE_SOFT_TRIG) )
    vmeWrite32(&(FAp[id]->csr), FA_CSR_TRIGGER);
  else
    logMsg("faTrig: ERROR: Software Triggers not enabled",0,0,0,0,0,0);
  FAUNLOCK;
}

/**
 *  @ingroup Readout
 *  @brief Pulse a software trigger to all initialized fADC250s
 */
void
faGTrig()
{
  int ii;

  for(ii=0;ii<nfadc;ii++)
    faTrig(fadcID[ii]);
}

/**
 *  @ingroup Readout
 *  @brief Pulse a software playback trigger to the module.
 *  @param id Slot number
 */
void
faTrig2(int id)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faTrig2: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
  FALOCK;
  if( vmeRead32(&(FAp[id]->ctrl1)) & (FA_ENABLE_SOFT_TRIG) )
    vmeWrite32(&(FAp[id]->csr), FA_CSR_SOFT_PULSE_TRIG2);
  else
    logMsg("faTrig2: ERROR: Software Triggers not enabled",0,0,0,0,0,0);
  FAUNLOCK;
}

/**
 *  @ingroup Readout
 *  @brief Pulse a software playback trigger to all initialized fADC250s
 */
void
faGTrig2()
{
  int ii;

  for(ii=0;ii<nfadc;ii++)
    faTrig2(fadcID[ii]);
}

/**
 *  @ingroup Config
 *  @brief Configure the delay between the software playback trigger and trigger
 *  @param id Slot number
 *  @param delay Delay between the playback trigger and trigger in units of 4 ns
 *  @sa faEnableInternalPlaybackTrigger
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetTrig21Delay(int id, int delay)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  if(delay>FA_TRIG21_DELAY_MASK)
    {
      printf("%s: ERROR: Invalid value for delay (%d).\n",
	     __FUNCTION__,delay);
      return ERROR;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->trig21_delay, delay);
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Status
 *  @brief Return the value of the delay between the software playback trigger and trigger
 *  @param id Slot number
 *  @return Trigger delay, otherwise ERROR.
 */
int
faGetTrig21Delay(int id)
{
  int rval=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  FALOCK;
  rval = vmeRead32(&FAp[id]->trig21_delay) & FA_TRIG21_DELAY_MASK;
  FAUNLOCK;

  return rval;
}

/**
 *  @ingroup Config
 *  @brief Enable the software playback trigger and trigger
 *  @param id Slot number
 *  @sa faSetTrig21Delay
 *  @return OK if successful, otherwise ERROR.
 */
int
faEnableInternalPlaybackTrigger(int id)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->ctrl1, 
	     (vmeRead32(&FAp[id]->ctrl1) & ~FA_TRIG_MASK) | FA_TRIG_VME_PLAYBACK);
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Pulse a software SyncReset
 *  @param id Slot number
 */
void
faSync(int id)
{

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSync: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }

  FALOCK;
  if(vmeRead32(&(FAp[id]->ctrl1))&(FA_ENABLE_SOFT_SRESET))
    vmeWrite32(&(FAp[id]->csr), FA_CSR_SYNC);
  else
    logMsg("faSync: ERROR: Software Sync Resets not enabled\n",0,0,0,0,0,0);
  FAUNLOCK;
}



/**
 *  @ingroup Readout
 *  @brief  Return Event/Block count
 *  @param id Slot number
 *  @param dflag 
 *   -  0: Event Count
 *   - >0: Block count
 *  @return OK if successful, otherwise ERROR.
 */
int
faDready(int id, int dflag)
{
  unsigned int dcnt=0;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faDready: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }
  
  FALOCK;
  if(dflag)
    dcnt = vmeRead32(&(FAp[id]->blk_count))&FA_BLOCK_COUNT_MASK;
  else
    dcnt = vmeRead32(&(FAp[id]->ev_count))&FA_EVENT_COUNT_MASK;
  FAUNLOCK;

  
  return(dcnt);
}

/**
 *  @ingroup Readout
 *  @brief Return a Block Ready status
 *  @param id Slot number
 *  @return 1 if block is ready for readout, 0 if not, otherwise ERROR.
 */
int
faBready(int id)
{
  int stat=0;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faBready: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }
  
  FALOCK;
  stat = (vmeRead32(&(FAp[id]->csr))) &FA_CSR_BLOCK_READY;
  FAUNLOCK;

  if(stat)
    return(1);
  else
    return(0);
}

/**
 *  @ingroup Readout
 *  @brief Return a Block Ready status mask for all initialized fADC250s
 *  @return block ready mask, otherwise ERROR.
 */
unsigned int
faGBready()
{
  int ii, id, stat=0;
  unsigned int dmask=0;
  
  FALOCK;
  for(ii=0;ii<nfadc;ii++) 
    {
      id = fadcID[ii];
      
      stat = vmeRead32(&(FAp[id]->csr))&FA_CSR_BLOCK_READY;

      if(stat)
	dmask |= (1<<id);
    }
  FAUNLOCK;
  
  return(dmask);
}

/**
 *  @ingroup Status
 *  @brief Return the vme slot mask of all initialized fADC250s
 *  @return VME Slot mask, otherwise ERROR.
 */
unsigned int
faScanMask()
{
  int ifadc, id, dmask=0;

  for(ifadc=0; ifadc<nfadc; ifadc++)
    {
      id = fadcID[ifadc];
      dmask |= (1<<id);
    }

  return(dmask);
}


/**
 *  @ingroup Config
 *  @brief Set/Readback Busy Level
 *  @param id Slot number
 *  @param val 
 *    - >0: set the busy level to val
 *    -  0: read back busy level
 *  @param bflag   i
 *    - >0: force the module Busy
 *
 *  @return OK if successful, otherwise ERROR.
 */
int
faBusyLevel(int id, unsigned int val, int bflag)
{
  unsigned int blreg=0;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faBusyLevel: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }
  if(val>FA_BUSY_LEVEL_MASK)
    return(ERROR);
  
  /* if Val > 0 then set the Level else leave it alone*/
  FALOCK;
  if(val) 
    {
      if(bflag)
	vmeWrite32(&(FAp[id]->busy_level),(val | FA_FORCE_BUSY));
      else
	vmeWrite32(&(FAp[id]->busy_level),val);
    }
  else
    {
      blreg = vmeRead32(&(FAp[id]->busy_level));
      if(bflag)
	vmeWrite32(&(FAp[id]->busy_level),(blreg | FA_FORCE_BUSY));
    }
  FAUNLOCK;

  return((blreg&FA_BUSY_LEVEL_MASK));
}

/**
 *  @ingroup Status
 *  @brief Get the busy status
 *  @param id Slot number
 *  @return OK if successful, otherwise ERROR.
 */
int
faBusy(int id)
{
  unsigned int blreg=0;
  unsigned int dreg=0;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faBusy: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }
  
  FALOCK;
  blreg = vmeRead32(&(FAp[id]->busy_level))&FA_BUSY_LEVEL_MASK;
  dreg  = vmeRead32(&(FAp[id]->ram_word_count))&FA_RAM_DATA_MASK;
  FAUNLOCK;

  if(dreg>=blreg)
    return(1);
  else
    return(0);
}

/**
 *  @ingroup Config
 *  @brief Enable software triggers
 *  @param id Slot number
 */
void
faEnableSoftTrig(int id)
{

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faEnableSoftTrig: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
  
  /* Clear the source */
  FALOCK;
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) & ~FA_TRIG_MASK );
  /* Set Source and Enable*/
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) | (FA_TRIG_VME | FA_ENABLE_SOFT_TRIG) );
  FAUNLOCK;
}

/**
 *  @ingroup Config
 *  @brief Enable Software Triggers for all initialized fADC250s
 */
void
faGEnableSoftTrig()
{
  int ii, id;

  for(ii=0;ii<nfadc;ii++) 
    {
      id = fadcID[ii];
      faEnableSoftTrig(id);
    }
  
}

/**
 *  @ingroup Config
 *  @brief Disable Software Triggers
 *  @param id Slot number
 */
void
faDisableSoftTrig(int id)
{

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faDisableSoftTrig: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
  
  FALOCK;
  vmeWrite32(&(FAp[id]->ctrl1), 
	     vmeRead32(&(FAp[id]->ctrl1)) & ~FA_ENABLE_SOFT_TRIG );
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Enable Software SyncReset
 *  @param id Slot number
 */
void
faEnableSoftSync(int id)
{

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faEnableSoftSync: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
  
  /* Clear the source */
  FALOCK;
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) & ~FA_SRESET_MASK);
  /* Set Source and Enable*/
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) | (FA_SRESET_VME | FA_ENABLE_SOFT_SRESET));
  FAUNLOCK;
}

/**
 *  @ingroup Config
 *  @brief Disable Software SyncReset
 *  @param id Slot number
 */
void
faDisableSoftSync(int id)
{

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faDisableSoftSync: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
  
  FALOCK;
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) & ~FA_ENABLE_SOFT_SRESET);
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Enable the internal clock
 *  @param id Slot number
 */
void
faEnableClk(int id)
{

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faEnableClk: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
  
  FALOCK;
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) | (FA_REF_CLK_INTERNAL|FA_ENABLE_INTERNAL_CLK) );
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Disable the internal clock
 *  @param id Slot number
 */
void
faDisableClk(int id)
{

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faDisableClk: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
  
  FALOCK;
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) & ~FA_ENABLE_INTERNAL_CLK );
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Enable trigger out for front panel or p0
 *  @param id Slot number
 *  @param output 
 *    - 0: FP trigger out
 *    - 1: P0 trigger out
 *    - 2: FP and P0 trigger out
 */
void
faEnableTriggerOut(int id, int output)
{
  int bitset=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faEnableBusError: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }

  if(output>2)
    {
      logMsg("faEnableTriggerOut: ERROR: output (%d) out of range.  Must be less than 3",
	     output,2,3,4,5,6);
      return;

    }

  switch(output)
    {
    case 0:
      bitset = FA_ENABLE_TRIG_OUT_FP;
      break;
    case 1:
      bitset = FA_ENABLE_TRIG_OUT_P0;
      break;
    case 2:
      bitset = FA_ENABLE_TRIG_OUT_FP | FA_ENABLE_TRIG_OUT_P0;
      break;
	
    }
  FALOCK;
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) | bitset );
  FAUNLOCK;



}

/**
 *  @ingroup Config
 *  @brief Enable bus errors to terminate a block transfer
 *  @param id Slot number
 */
void
faEnableBusError(int id)
{

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faEnableBusError: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
  
  FALOCK;
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) | FA_ENABLE_BERR );
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Enable bus errors to terminate a block transfer for all initialized fADC250s
 */
void
faGEnableBusError()
{
  int ii;

  FALOCK;
  for(ii=0;ii<nfadc;ii++) 
    {
      vmeWrite32(&(FAp[fadcID[ii]]->ctrl1),
		 vmeRead32(&(FAp[fadcID[ii]]->ctrl1)) | FA_ENABLE_BERR );
    }
  FAUNLOCK;
  
}

/**
 *  @ingroup Config
 *  @brief Disable bus errors
 *  @param id Slot number
 */
void
faDisableBusError(int id)
{

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faDisableBusError: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
  
  FALOCK;
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) & ~FA_ENABLE_BERR );
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Enable and setup multiblock transfers for all initialized fADC250s
 *  @param tflag Token Flag
 *    - >0: Token via P0/VXS
 *    -  0: Token via P2
 */
void
faEnableMultiBlock(int tflag)
{
  int ii, id;
  unsigned int mode;

  if((nfadc <= 1) || (FAp[fadcID[0]] == NULL)) 
    {
      logMsg("faEnableMultiBlock: ERROR : Cannot Enable MultiBlock mode \n",0,0,0,0,0,0);
      return;
    }

  /* if token = 0 then send via P2 else via VXS */
  if(tflag)
    mode = (FA_ENABLE_MULTIBLOCK | FA_MB_TOKEN_VIA_P0);
  else
    mode = (FA_ENABLE_MULTIBLOCK | FA_MB_TOKEN_VIA_P2);
    
  for(ii=0;ii<nfadc;ii++) 
    {
      id = fadcID[ii];
      FALOCK;
      vmeWrite32(&(FAp[id]->ctrl1),
		 vmeRead32(&(FAp[id]->ctrl1)) | mode );
      FAUNLOCK;
      faDisableBusError(id);
      if(id == fadcMinSlot) 
	{
	  FALOCK;
	  vmeWrite32(&(FAp[id]->ctrl1),
		     vmeRead32(&(FAp[id]->ctrl1)) | FA_FIRST_BOARD );
	  FAUNLOCK;
	}
      if(id == fadcMaxSlot) 
	{
	  FALOCK;
	  vmeWrite32(&(FAp[id]->ctrl1),
		     vmeRead32(&(FAp[id]->ctrl1)) | FA_LAST_BOARD );
	  FAUNLOCK;
	  faEnableBusError(id);   /* Enable Bus Error only on Last Board */
	}
    }

}

/**
 *  @ingroup Config
 *  @brief Disable multiblock transfer for all initialized fADC250s
 */
void
faDisableMultiBlock()
{
  int ii;

  if((nfadc <= 1) || (FAp[fadcID[0]] == NULL)) 
    {
      logMsg("faDisableMultiBlock: ERROR : Cannot Disable MultiBlock Mode\n",0,0,0,0,0,0);
      return;
    }
  
  FALOCK;
  for(ii=0;ii<nfadc;ii++)
    vmeWrite32(&(FAp[fadcID[ii]]->ctrl1),
	       vmeRead32(&(FAp[fadcID[ii]]->ctrl1)) & ~FA_ENABLE_MULTIBLOCK );
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Set the block level for the module
 *  @param id Slot number
 *  @param level block level
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetBlockLevel(int id, int level)
{
  int rval;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetBlockLevel: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }
  
  if(level<=0) level = 1;
  FALOCK;
  vmeWrite32(&(FAp[id]->blk_level), level);
  fadcBlockLevel = level;
  rval = vmeRead32(&(FAp[id]->blk_level)) & FA_BLOCK_LEVEL_MASK;
  FAUNLOCK;

  return(rval);

}

/**
 *  @ingroup Config
 *  @brief Set the block level for all initialized fADC250s
 *  @param level block level
 */
void
faGSetBlockLevel(int level)
{
  int ii;

  if(level<=0) level = 1;
  FALOCK;
  for(ii=0;ii<nfadc;ii++)
    vmeWrite32(&(FAp[fadcID[ii]]->blk_level), level);
  FAUNLOCK;

  fadcBlockLevel = level;
}

/**
 *  @ingroup Config
 *  @brief Set the Clock Source for the module
 *  @param id Slot number
 *  @param source Clock Source
 *    - 0: internal
 *    - 1: front panel
 *    - 2: P0/VXS
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetClkSource(int id, int source)
{
  int rval;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetClkSource: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }
  
  FALOCK;
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) & ~FA_REF_CLK_SEL_MASK );
  if((source<0)||(source>7)) source = FA_REF_CLK_INTERNAL;
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) | source );
  rval = vmeRead32(&(FAp[id]->ctrl1)) & FA_REF_CLK_SEL_MASK;
  FAUNLOCK;


  return(rval);

}

/**
 *  @ingroup Config
 *  @brief Set the trigger source for the module
 *  @param id Slot number
 *  @param source Trigger Source
 *   - 0: Front Panel
 *   - 1: Front Panel (Synchronized)
 *   - 2: P0/VXS
 *   - 3: P0/VXS (Synchronized)
 *   - 4: Not used
 *   - 5: Software (with playback)
 *   - 6: Software
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetTrigSource(int id, int source)
{
  int rval;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetTrigSource: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }
  
  if((source<0)||(source>6))
    {
      logMsg("faSetTrigSource: ERROR: Invalid source (%d)\n",source,2,3,4,5,6);
    }

  FALOCK;
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) & ~FA_TRIG_SEL_MASK );
  if((source<0)||(source>7)) source = FA_TRIG_FP_ISYNC;
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) | source );
  rval = vmeRead32(&(FAp[id]->ctrl1)) & FA_TRIG_SEL_MASK;
  FAUNLOCK;

  return(rval);

}

/**
 *  @ingroup Config
 *  @brief Set the SyncReset source for the module
 *  @param id Slot number
 *  @param source
 *   - 0: FP
 *   - 1: FP (synchronized)
 *   - 2: P0/VXS
 *   - 3: P0/VXS (synchronized)
 *   - 4: Not used
 *   - 5: Not used
 *   - 6: Software
 *   - 7: No Source
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetSyncSource(int id, int source)
{
  int rval;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetSyncSource: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }
  
  FALOCK;
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) & ~FA_SRESET_SEL_MASK );
  if((source<0)||(source>7)) source = FA_SRESET_FP_ISYNC;
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) | source );
  rval = vmeRead32(&(FAp[id]->ctrl1)) & FA_SRESET_SEL_MASK;
  FAUNLOCK;

  return(rval);

}

/**
 *  @ingroup Config
 *  @brief Enable Front Panel Inputs
 *
 *    Also disables software triggers/syncs but leaves the clock source alone
 *
 *  @param id Slot number
 */
void
faEnableFP(int id)
{

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faEnableFP: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
  
  FALOCK;
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) & 
	     ~(FA_TRIG_SEL_MASK | FA_SRESET_SEL_MASK | FA_ENABLE_SOFT_SRESET | FA_ENABLE_SOFT_TRIG));
  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&(FAp[id]->ctrl1)) | (FA_TRIG_FP_ISYNC | FA_SRESET_FP_ISYNC));
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Set trigger output options
 *  @param id Slot number
 *  @param trigout bits:  
 * <pre>
 *      0  1  0  Enable Front Panel Trigger Output
 *      1  0  0  Enable VXS Trigger Output
 * </pre>
 *
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetTrigOut(int id, int trigout)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("faSetTrigOut: ERROR : ADC in slot %d is not initialized \n",id);
      return ERROR;
    }

  if((trigout & ~(0x6))!=0)
    {
      printf("faSetTrigOut: ERROR : Invalid trigout value (%d) \n",trigout);
      return ERROR;
    }

  FALOCK;
  vmeWrite32(&(FAp[id]->ctrl1),
	     (vmeRead32(&(FAp[id]->ctrl1)) & ~FA_TRIGOUT_MASK) |
	     trigout<<12);
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Reset the trigger count for the module
 *  @param id Slot number
 *  @return OK if successful, otherwise ERROR.
 */
int
faResetTriggerCount(int id)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faResetTriggerCount: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  FALOCK;
  vmeWrite32(&FAp[id]->trig_scal,FA_TRIG_SCAL_RESET);
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Set the readout threshold value for specified channel mask
 *  @param id Slot number
 *  @param tvalue Threshold value 
 *  @param chmask Mask of channels to set
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetThreshold(int id, unsigned short tvalue, unsigned short chmask)
{

  int ii, doWrite=0;
  unsigned int lovalue=0, hivalue=0;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetThreshold: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  if(chmask==0) chmask = 0xffff;  /* Set All channels the same */

  FALOCK;
  for(ii=0;ii<FA_MAX_ADC_CHANNELS;ii++) 
    {
      if(ii%2==0)
	{
	  lovalue = (vmeRead16(&FAp[id]->adc_thres[ii]));
	  hivalue = (vmeRead16(&FAp[id]->adc_thres[ii+1]));

	  if((1<<ii)&chmask)
	    {
	      lovalue = tvalue;
	      doWrite=1;
	    }
	  if((1<<(ii+1))&chmask)
	    {
	      hivalue = tvalue;
	      doWrite=1;
	    }

	  if(doWrite)
	    vmeWrite32((unsigned int *)&(FAp[id]->adc_thres[ii]),
		       lovalue<<16 | hivalue);

	  lovalue = 0; 
	  hivalue = 0;
	  doWrite=0;
	}
    }
  FAUNLOCK;

  return(OK);
}

/**
 *  @ingroup Status
 *  @brief Print the thresholds of all channels to standard out
 *  @param id Slot number
 *  @return OK if successful, otherwise ERROR.
 */
int
faPrintThreshold(int id)
{
  int ii;
  unsigned short tval[FA_MAX_ADC_CHANNELS];

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faPrintThreshold: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  FALOCK;
  for(ii=0;ii<FA_MAX_ADC_CHANNELS;ii++)
    {
      tval[ii] = vmeRead16(&(FAp[id]->adc_thres[ii]));
    }
  FAUNLOCK;


  printf(" Threshold Settings for FADC in slot %d:",id);
  for(ii=0;ii<FA_MAX_ADC_CHANNELS;ii++) 
    {
      if((ii%4)==0) 
	{
	  printf("\n");
	}
      printf("Chan %2d: %5d   ",(ii+1),tval[ii]);
    }
  printf("\n");
  

  return(OK);
}

/**
 *  @ingroup Readout
 *  @brief Configure pedestal parameters to be used by processing algorythm
 *  @param id Slot number
 *  @param nsamples Number of samples to contribute to sum
 *  @param maxvalue Maximum sample value to be included in the sum
 *  @return OK if successful, otherwise ERROR.
 */

int
faProcPedConfig(int id, int nsamples, int maxvalue)
{
  if(id==0) id=fadcID[0];
  
  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faProcPedConfig: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  if((nsamples < FA_ADC_MIN_NPED) || (nsamples > FA_ADC_MAX_NPED))
    {
      printf("%s: ERROR: Invalid nsamples (%d)\n",
	     __FUNCTION__, nsamples);
      return ERROR;
    }

  if((maxvalue < 0) || (maxvalue > 0x3ff))
    {
      printf("%s: ERROR: Invalid maxvalue (%d)\n",
	     __FUNCTION__, maxvalue);
      return ERROR;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->config7,
	     (nsamples - 1)<<10 | maxvalue);
  FAUNLOCK;
  
  return OK;
}

/**
 *  @ingroup Readout
 *  @brief Configure pedestal parameters to be used by processing algorythm
 *    for all initialized modules.
 *  @param nsamples Number of samples to contribute to sum
 *  @param maxvalue Maximum sample value to be included in the sum
 *  @return OK if successful, otherwise ERROR.
 */

int
faGProcPedConfig(int nsamples, int maxvalue)
{
  int ifa=0, rval=OK;
  

  for(ifa = 0; ifa < nfadc; ifa++)
    rval |= faProcPedConfig(faSlot(ifa), nsamples, maxvalue);

  return rval;
}

/**
 *  @ingroup Readout
 *  @brief Configure output of sample data from @faReadChannelSample and @faReadAllChannelSamples
 *  @param id Slot number
 *  @param nsamples Number of samples to contribute to sum
 *  @param maxvalue Maximum sample value to be included in the sum
 *  @return OK if successful, otherwise ERROR.
 */

int
faSampleConfig(int id, int nsamples, int maxvalue)
{
  if(id==0) id=fadcID[0];
  
  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSampleConfig: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  if((nsamples < FA_ADC_MIN_MNPED) || (nsamples > FA_ADC_MAX_MNPED))
    {
      printf("%s: ERROR: Invalid nsamples (%d)\n",
	     __FUNCTION__, nsamples);
      return ERROR;
    }

  if((maxvalue < 0) || (maxvalue > 0x3ff))
    {
      printf("%s: ERROR: Invalid maxvalue (%d)\n",
	     __FUNCTION__, maxvalue);
      return ERROR;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->config6,
	     (nsamples - 1)<<10 | maxvalue);
  FAUNLOCK;
  
  return OK;
}

/**
 *  @ingroup Readout
 *  @brief Configure output of sample data from @faReadChannelSample and @faReadAllChannelSamples
 *    for all initialized modules.
 *  @param nsamples Number of samples to contribute to sum
 *  @param maxvalue Maximum sample value to be included in the sum
 *  @return OK if successful, otherwise ERROR.
 */

int
faGSampleConfig(int nsamples, int maxvalue)
{
  int ifa=0, rval=OK;
  

  for(ifa = 0; ifa < nfadc; ifa++)
    rval |= faSampleConfig(faSlot(ifa), nsamples, maxvalue);

  return rval;
}

/**
 *  @ingroup Readout
 *  @brief Read the current sample data from the specified channel and module.
 *  @param id Slot number
 *  @param chan Channel Number
 *  @return Sample data if successful, otherwise ERROR.
 */
int
faReadChannelSample(int id, int chan)
{
  int rval=0;
  unsigned int write=0;
  if(id==0) id=fadcID[0];
  
  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faReadChannelSample: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  if(chan>=FA_MAX_ADC_CHANNELS)
    {
      logMsg("faReadChannelSample: ERROR: Invalid channel (%d)\n",chan,2,3,4,5,6);
      return ERROR;
    }

  FALOCK;
  write = vmeRead32(&FAp[id]->adc_config[0]) & 0xFF;
  
  // Alex
  //  vmeWrite32(&FAp[id]->adc_config[0], write | 
  //	     ((chan<<8) | FA_ADC_CONFIG0_CHAN_READ_ENABLE) );

  vmeWrite32(&FAp[id]->adc_config[0], (write |  FA_ADC_CONFIG0_CHAN_READ_ENABLE) );


  rval = vmeRead32(&FAp[id]->adc_status[2]) & FA_ADC_STATUS2_CHAN_DATA_MASK;

  FAUNLOCK;

  return rval;
}

/**
 *  @ingroup Readout
 *  @brief Read the current sample data from the specified channel and module.
 *  @param id     Slot number
 *  @param data   local memory address to place data
 *                * Least significant 16bits contain lesser channel number data
 *  @return Number of words stored in data if successful, otherwise ERROR.
 */
int
faReadAllChannelSamples(int id, volatile unsigned int *data)
{
  int ichan=0;
  unsigned int write=0, read=0, set_readout = 0;
  
  if(id==0) id=fadcID[0];
  
  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faReadAllChannelSamples: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }
  
  FALOCK;

  write = vmeRead32(&FAp[id]->adc_config[0]) & 0xFFFF;

  // Enable processing
  vmeWrite32(&FAp[id]->adc_config[0], (write |  FA_ADC_CONFIG0_CHAN_READ_ENABLE) );

  // Disable processing
  vmeWrite32(&FAp[id]->adc_config[0], (write &  (~FA_ADC_CONFIG0_CHAN_READ_ENABLE)) );


  for(ichan = 0; ichan < FA_MAX_ADC_CHANNELS; ichan++)
    {
  
      read = vmeRead32(&FAp[id]->adc_status[2]) & FA_ADC_STATUS2_CHAN_DATA_MASK;

      //      printf("Channel =  %2d    0%x \n", ichan,  read &  0xFFFF);


      if( (ichan % 2)==0 )
	{
	  data[ichan/2] = (data[ichan/2] & 0xFFFF0000) | read;
	}
      else
	{
	  data[ichan/2] = (data[ichan/2] & 0xFFFF) | (read<<16);
	}
    }
  

  //  printf("faReadAllChannelSamples End readout :  adc_config[0]   0x%x \n",(write &  (~FA_ADC_CONFIG0_CHAN_READ_ENABLE)) );


  FAUNLOCK;

  return (FA_MAX_ADC_CHANNELS/2);
}


/**
 *  @ingroup Config
 *  @brief Set the DAC value of the specified channel mask
 *  @param id Slot number
 *  @param dvalue DAC Value
 *  @param chmask Mask of channels to set
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetDAC(int id, unsigned short dvalue, unsigned short chmask)
{
  int ii, doWrite=0, rval=OK;
  unsigned int lovalue=0, hivalue=0;
  unsigned int lovalue_rb=0, hivalue_rb=0;
  
  if(id==0) id=fadcID[0];
  
  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetDAC: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }
  
  if(chmask==0) chmask = 0xffff;  /* Set All channels the same */
  
  if(dvalue>0xfff) 
    {
      logMsg("faSetDAC: ERROR : DAC value (%d) out of range (0-4095) \n",
	     dvalue,0,0,0,0,0);
      return(ERROR);
    }
  
  FALOCK;
  for(ii=0;ii<FA_MAX_ADC_CHANNELS;ii++)
    {

      if(ii%2==0)
	{
	  lovalue = (vmeRead16(&FAp[id]->dac[ii]));
	  hivalue = (vmeRead16(&FAp[id]->dac[ii+1]));

	  if((1<<ii)&chmask)
	    {
	      lovalue = dvalue&FA_DAC_VALUE_MASK;
	      doWrite=1;
	    }
	  if((1<<(ii+1))&chmask)
	    {
	      hivalue = (dvalue&FA_DAC_VALUE_MASK);
	      doWrite=1;
	    }

	  if(doWrite)
	    {

              //              taskDelay(1);

	      vmeWrite32((unsigned int *)&(FAp[id]->dac[ii]), 
			 lovalue<<16 | hivalue);


              // Alex
              //              taskDelay(1);

	      /* Readback to check values, and write timeout error */

	      lovalue_rb = (vmeRead16(&FAp[id]->dac[ii]));

              //              taskDelay(1);

	      hivalue_rb = (vmeRead16(&FAp[id]->dac[ii+1]));
              
              
              //              printf(" lovalue_rb  %d   %d  %d \n", lovalue_rb, lovalue, ii);
              //              printf(" hivalue_rb  %d   %d  %d \n", hivalue_rb, hivalue, ii);
              

	      if((lovalue_rb != lovalue) || (hivalue_rb != hivalue_rb))
		{
		  printf("%s: ERROR: Readback of DAC Channels (%d, %d) != Write value\n",
			 __FUNCTION__,ii, ii+1);
		  printf("  %2d: Read: 0x%04x %s Write: 0x%04x\n",
			 ii, lovalue_rb & FA_DAC_VALUE_MASK, 
			 (lovalue_rb & FA_DAC_WRITE_TIMEOUT_ERROR)?
			 "-Write Timeout ERROR-":
			 "                     ",
			 lovalue);
		  printf("  %2d: Read: 0x%04x %s Write: 0x%04x\n",
			 ii+1, hivalue_rb & FA_DAC_VALUE_MASK,
			 (hivalue_rb & FA_DAC_WRITE_TIMEOUT_ERROR)?
			 "-Write Timeout ERROR-":
			 "                     ",
			 hivalue);
		  rval=ERROR;
		}
	    }

	  lovalue = 0; 
	  hivalue = 0;
	  doWrite=0;
	}

    }
  FAUNLOCK;

  return(rval);
}

/**
 *  @ingroup Config
 *  @brief Set the DAC value of the specified channel mask and readback and check that it
 *         was written properly
 *  @param id Slot number
 *  @param dvalue DAC Value
 *  @param chmask Mask of channels to set
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetDACandCheck(int id, unsigned short dvalue, unsigned short chmask)
{
  int ichan, rval=OK;
  unsigned short dacRB[FA_MAX_ADC_CHANNELS];

  if(id==0) id=fadcID[0];
  
  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetDACandCheck: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  if(faSetDAC(id, dvalue, chmask)!=OK)
    return ERROR;

  if(faGetDAC(id,(unsigned short *)&dacRB)!=OK)
    return ERROR;

  for(ichan=0; ichan<FA_MAX_ADC_CHANNELS; ichan++)
    {
      if((1<<ichan)&chmask)
	{
	  if(dacRB[ichan] != dvalue)
	    {
	      printf("%s(%d): ERROR: DAC Readback for channel %d does not equal specified value",
		     __FUNCTION__,id,ichan);
	      printf(" (0x%04x != 0x$04x)\n",
		     dacRB[ichan] != dvalue);
	      rval = ERROR;
	    }
	}
    }

  return rval;
}


/**
 *  @ingroup Status
 *  @brief Print DAC values for each channel to standard out
 *  @param id Slot number
 */
void
faPrintDAC(int id)
{
  int ii;
  unsigned short dval[FA_MAX_ADC_CHANNELS];

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faPrintDAC: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }

  FALOCK;
  for(ii=0;ii<FA_MAX_ADC_CHANNELS;ii++)
    dval[ii] = vmeRead16(&(FAp[id]->dac[ii])) & FA_DAC_VALUE_MASK;
  FAUNLOCK;
  
  
  printf(" DAC Settings for FADC in slot %d:",id);
  for(ii=0;ii<FA_MAX_ADC_CHANNELS;ii++) 
    {
      if((ii%4)==0) printf("\n");
      printf("Chan %2d: %5d   ",(ii+1),dval[ii]);
    }
  printf("\n");
  
}

/**
 *  @ingroup Config
 *  @brief Readback the DAC values currently used by the module in the specified slot.
 *
 *  @param id Slot number
 *  @param intdata Local location to store DAC values.
 *  @return OK if successful, otherwise ERROR.
 */
int
faGetDAC(int id, unsigned short *indata)
{
  int idac;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  if(indata==NULL)
    {
      printf("%s: ERROR: Invalid destintation address\n",
	     __FUNCTION__);
      return ERROR;
    }

  FALOCK;
  for(idac=0;idac<FA_MAX_ADC_CHANNELS;idac++)
    indata[idac] = vmeRead16(&(FAp[id]->dac[idac])) & FA_DAC_VALUE_MASK;
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Readback the DAC for a specific channel by the module in the specified slot.
 *
 *  @param id Slot number
 *  @param channel Channel number (0-15)
 *  @return DAC Value if successful, otherwise ERROR.
 */
int
faGetChannelDAC(int id, int channel)
{
  int dac;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,id);
      return ERROR;
    }

  if((channel<0)||(channel>15))
    {
      printf("%s: ERROR: Invalid channel (%d)\n",
	     __FUNCTION__, channel);
      return ERROR;
    }

  FALOCK;
  dac = vmeRead16(&(FAp[id]->dac[channel])) & FA_DAC_VALUE_MASK;
  FAUNLOCK;

  return dac;
}


/**
 *  @ingroup Config
 *  @brief Set the pedestal value of specified channel
 *
 *    The pedestal is the value that will be subtracted from specified channel
 *    for each sample before it is sent through the trigger path
 *
 *  @param id Slot number
 *  @param chan Channel Number
 *  @param ped Pedestal value
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetChannelPedestal(int id, unsigned int chan, unsigned int ped)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetChannelPedestal: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  if(chan>16)
    {
      logMsg("faSetChannelPedestal: ERROR : Channel (%d) out of range (0-15) \n",
	     chan,0,0,0,0,0);
      return(ERROR);
    }

  if(ped>0xffff) 
    {
      logMsg("faSetChannelPedestal: ERROR : PED value (%d) out of range (0-65535) \n",
	     ped,0,0,0,0,0);
      return(ERROR);
    }

  FALOCK;
  vmeWrite32(&FAp[id]->adc_pedestal[chan], ped);
  FAUNLOCK;

  return(OK);
}

/**
 *  @ingroup Status
 *  @brief Get the pedestal value of specified channel
 *  @param id Slot number
 *  @param chan Channel Number
 *  @return OK if successful, otherwise ERROR.
 */
int
faGetChannelPedestal(int id, unsigned int chan)
{
  unsigned int rval=0;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetChannelPedestal: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  if(chan>16)
    {
      logMsg("faSetChannelPedestal: ERROR : Channel (%d) out of range (0-15) \n",
	     chan,0,0,0,0,0);
      return(ERROR);
    }

  FALOCK;
  rval = vmeRead32(&FAp[id]->adc_pedestal[chan]) & FA_ADC_PEDESTAL_MASK;
  FAUNLOCK;

  return(rval);
}

/**
 *  @ingroup Deprec
 *  @brief Set the fa250 operation when Sync Reset is received
 *
 *   This routine is deprecated.  Use faSetAlignmentDebugMode
 *
 *  @param id Slot number
 *  @param  mode 
 *    - 0:  Send a calibration sequence to the CTP for alignment purposes
 *    - 1:  Normal operation
 *  @sa faSetAlignmentDebugMode
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetMGTTestMode(int id, unsigned int mode)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return(ERROR);
    }

  if(mode==0)
    printf("%s: This routine is deprecated.  Replace with faEnableSyncSrc(int id)\n",
	   __FUNCTION__);
  else 
    printf("%s: This routine is deprecated.  Remove it if after SyncReset\n",
	   __FUNCTION__);

#ifdef OLDMGTCTRL
  FALOCK;
  if(mode) 
    { /* After Sync Reset (Normal mode) */
      vmeWrite32(&FAp[id]->mgt_ctrl, FA_RELEASE_MGT_RESET);
      vmeWrite32(&FAp[id]->mgt_ctrl, FA_MGT_FRONT_END_TO_CTP);
    }
  else     
    { /* Before Sync Reset (Calibration Mode) */
      vmeWrite32(&FAp[id]->mgt_ctrl,FA_MGT_RESET);
      vmeWrite32(&FAp[id]->mgt_ctrl,FA_RELEASE_MGT_RESET);
      vmeWrite32(&FAp[id]->mgt_ctrl,FA_MGT_ENABLE_DATA_ALIGNMENT);
    }
  FAUNLOCK;
#else
  if(mode==0) /* Before sync reset */
    {
      faEnableSyncSrc(id);
    }
  /* Nothing needs to be done after Sync Reset... now taken care of in faEnable(..) */
#endif /* OLDMGTCTRL */

  return(OK);
}

/**
 *  @ingroup Config
 *  @brief Enable/Disable the alignment sequence that is sent to the CTP for debugging.
 *  @param enable
 *   -  0: Disable
 *   - >0: Enable
 *  @return 1 if enabled, 0 if disabled
 */
int
faSetAlignmentDebugMode(int enable)
{
  fadcAlignmentDebug = (enable)?1:0;
  return fadcAlignmentDebug;
}

/**
 *  @ingroup Status
 *  @brief Return whether or not the module will send the alignment sequence to the CTP
 *  @return 1 if enabled, 0 if disabled
 */
int
faGetAlignmentDebugMode()
{
  return fadcAlignmentDebug;
}

/**
 *  @ingroup Config
 *  @brief Enable/Disable Hitbits mode on the module
 *  @param id Slot number
 *  @param enable
 *   -  0: Disable
 *   - >0: Enable
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetHitbitsMode(int id, int enable)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return(ERROR);
    }

  FALOCK;
  if(enable) 
    { 
      vmeWrite32(&FAp[id]->mgt_ctrl, 
		 vmeRead32(&FAp[id]->mgt_ctrl) | FA_MGT_HITBITS_TO_CTP);
    }
  else     
    { /* Before Sync Reset (Calibration Mode) */
      vmeWrite32(&FAp[id]->mgt_ctrl, 
		 vmeRead32(&FAp[id]->mgt_ctrl) & ~FA_MGT_HITBITS_TO_CTP);
    }
  FAUNLOCK;

  return(OK);
}

/**
 *  @ingroup Config
 *  @brief Enable/Disable Hitbits mode for all initialized fADC250s
 *  @param enable
 *   -  0: Disable
 *   - >0: Enable
 */
void
faGSetHitbitsMode(int enable)
{
  int ifadc;

  for(ifadc=0;ifadc<nfadc;ifadc++)
    faSetHitbitsMode(faSlot(ifadc),enable);

}

/**
 *  @ingroup Status
 *  @brief Get the enabled/disabled status of hitbits mode for the module
 *  @param id Slot number
 *  @return 1 if enabled, 0 if disabled, otherwise ERROR.
 */
int
faGetHitbitsMode(int id)
{
  int rval;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return(ERROR);
    }

  FALOCK;
  rval = (vmeRead32(&FAp[id]->mgt_ctrl)&FA_MGT_HITBITS_TO_CTP)>>3;
  FAUNLOCK;

  return rval;
}

/**
 *  @ingroup Readout
 *  @brief Scaler Data readout routine
 *
 *        Readout the desired scalers (indicated by the channel mask), as well
 *        as the timer counter.  The timer counter will be the last word
 *        in the "data" array.
 *
 *  @param id Slot number
 *  @param data   - local memory address to place data
 *  @param chmask - Channel Mask (indicating which channels to read)
 *  @param rflag  - Readout Flag
 *    - bit 0: Latch Scalers before read
 *    - bit 1: Clear Scalers after read
 *  @return OK if successful, otherwise ERROR.
 */
int
faReadScalers(int id, volatile unsigned int *data, unsigned int chmask, int rflag)
{
  int doLatch=0, doClear=0, ichan=0;
  int dCnt=0;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faReadScalers: ERROR : ADC in slot %d is not initialized \n",
	     id,0,0,0,0,0);
      return ERROR;
    }

  if(rflag & ~(FA_SCALER_CTRL_MASK))
    {
      logMsg("faReadScalers: WARN : rflag (0x%x) has undefined bits \n",
	     rflag,0,0,0,0,0);
    }

  doLatch = rflag&(1<<0);
  doClear = rflag&(1<<1);

  FALOCK;
  if(doLatch)
    vmeWrite32(&FAp[id]->scaler_ctrl,
	       FA_SCALER_CTRL_ENABLE | FA_SCALER_CTRL_LATCH);

  for(ichan=0; ichan<16; ichan++)
    {
      if( (1<<ichan) & chmask )
	{
	  data[dCnt] = vmeRead32(&FAp[id]->scaler[ichan]);
	  dCnt++;
	}
    }
  
  data[dCnt] =  vmeRead32(&FAp[id]->time_count);
  dCnt++;

  if(doClear)
    vmeWrite32(&FAp[id]->scaler_ctrl,
	       FA_SCALER_CTRL_ENABLE | FA_SCALER_CTRL_RESET);
  FAUNLOCK;

  return dCnt;

}

/**
 *  @ingroup Readout
 *  @brief Scaler Print Out routine
 *
 *        Print out the scalers as well as the timer counter.
 *
 *  @param id Slot number
 *  @param rflag  - Printout Flag
 *     - bit 0: Latch Scalers before read
 *     - bit 1: Clear Scalers after read

 *  @return OK if successful, otherwise ERROR.
 */
int
faPrintScalers(int id, int rflag)
{
  int doLatch=0, doClear=0, ichan=0;
  unsigned int data[16], time_count;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faPrintScalers: ERROR : ADC in slot %d is not initialized \n",
	     id,0,0,0,0,0);
      return ERROR;
    }

  if(rflag & ~(FA_SCALER_CTRL_MASK))
    {
      logMsg("faPrintScalers: WARN : rflag (0x%x) has undefined bits \n",
	     rflag,0,0,0,0,0);
    }

  doLatch = rflag&(1<<0);
  doClear = rflag&(1<<1);

  FALOCK;
  if(doLatch)
    vmeWrite32(&FAp[id]->scaler_ctrl,
	       FA_SCALER_CTRL_ENABLE | FA_SCALER_CTRL_LATCH);

  for(ichan=0; ichan<16; ichan++)
    {
      data[ichan] = vmeRead32(&FAp[id]->scaler[ichan]);
    }
  
  time_count =  vmeRead32(&FAp[id]->time_count);

  if(doClear)
    vmeWrite32(&FAp[id]->scaler_ctrl,
	       FA_SCALER_CTRL_ENABLE | FA_SCALER_CTRL_RESET);
  FAUNLOCK;

  printf("%s: Scaler Counts\n",__FUNCTION__);
  for(ichan=0; ichan<16; ichan++)
    {
      if( (ichan%4) == 0 )
	printf("\n");

      printf("%2d: %10d ",ichan,data[ichan]);
    }
  printf("\n  timer: %10d\n",time_count);

  return OK;

}

/**
 *  @ingroup Config
 *  @brief Clear the scalers (and enable, if disabled)
 *  @param id Slot number
 *  @return OK if successful, otherwise ERROR.
 */
int
faClearScalers(int id)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faClearScalers: ERROR : ADC in slot %d is not initialized \n",
	     id,0,0,0,0,0);
      return ERROR;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->scaler_ctrl,
	     FA_SCALER_CTRL_ENABLE | FA_SCALER_CTRL_RESET);
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Latch the current scaler count
 *  @param id Slot number
 *  @return OK if successful, otherwise ERROR.
 */
int
faLatchScalers(int id)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faLatchScalers: ERROR : ADC in slot %d is not initialized \n",
	     id,0,0,0,0,0);
      return ERROR;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->scaler_ctrl,
	     FA_SCALER_CTRL_ENABLE | FA_SCALER_CTRL_LATCH);
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Enable the scalers to count
 *  @param id Slot number
 *  @return OK if successful, otherwise ERROR.
 */
int
faEnableScalers(int id)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faEnableScalers: ERROR : ADC in slot %d is not initialized \n",
	     id,0,0,0,0,0);
      return ERROR;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->scaler_ctrl,FA_SCALER_CTRL_ENABLE);
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Disable counting in the scalers
 *  @param id Slot number
 *  @return OK if successful, otherwise ERROR.
 */
int
faDisableScalers(int id)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faDisableScalers: ERROR : ADC in slot %d is not initialized \n",
	     id,0,0,0,0,0);
      return ERROR;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->scaler_ctrl,~FA_SCALER_CTRL_ENABLE);
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Status
 *  @brief Get the minimum address used for multiblock
 *  @param id Slot number
 *  @return multiblock min address if successful, otherwise ERROR.
 */
unsigned int
faGetMinA32MB(int id)
{
  unsigned int rval=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return ERROR;
    }

  FALOCK;
  rval = (vmeRead32(&FAp[id]->adr_mb) & FA_AMB_MIN_MASK)<<16;
  FAUNLOCK;

  return rval;
}

/**
 *  @ingroup Status
 *  @brief Get the maximum address used for multiblock
 *  @param id Slot number
 *  @return multiblock max address if successful, otherwise ERROR.
 */
unsigned int
faGetMaxA32MB(int id)
{
  unsigned int rval=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return ERROR;
    }

  FALOCK;
  rval = vmeRead32(&FAp[id]->adr_mb) & FA_AMB_MAX_MASK;
  FAUNLOCK;

  return rval;
}

/**
 *  @ingroup Config
 *  @brief Insert ADC parameter word into datastream.
 *     The data word appears as a block header continuation word.
 *  @param id Slot number
 *  @param enable Enable flag
 *      -  0: Disable
 *      - !0: Enable
 *  @return OK if successful, otherwise ERROR.
 */
int
faDataInsertAdcParameters(int id, int enable)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return ERROR;
    }

  FALOCK;
  if(enable)
    vmeWrite32(&FAp[id]->ctrl1,
	       vmeRead32(&FAp[id]->ctrl1) | FA_ENABLE_ADC_PARAMETERS_DATA);
  else
    vmeWrite32(&FAp[id]->ctrl1,
	       vmeRead32(&FAp[id]->ctrl1) & ~FA_ENABLE_ADC_PARAMETERS_DATA);
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Enable/Disable suppression of one or both of the trigger time words
 *    in the data stream.
 *  @param id Slot number
 *  @param suppress Suppression Flag
 *      -  0: Trigger time words are enabled in datastream
 *      -  1: Suppress BOTH trigger time words
 *      -  2: Suppress trigger time word 2 (that with most significant bytes)
 *  @return OK if successful, otherwise ERROR.
 */
int
faDataSuppressTriggerTime(int id, int suppress)
{
  unsigned int suppress_bits=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return ERROR;
    }

  switch(suppress)
    {
    case 0: /* Enable trigger time words */
      suppress_bits = FA_SUPPRESS_TRIGGER_TIME_DATA;
      break;

    case 1: /* Suppress both trigger time words */
      suppress_bits = FA_SUPPRESS_TRIGGER_TIME_DATA;
      break;

    case 2: /* Suppress trigger time word 2 */
      suppress_bits = FA_SUPPRESS_TRIGGER_TIME_WORD2_DATA;
      break;

    default:
      printf("%s(%d): ERROR: Invalid suppress (%d)\n",
	     __FUNCTION__,id,suppress);
      return ERROR;
    }

  FALOCK;
  if(suppress)
    vmeWrite32(&FAp[id]->ctrl1,
	       vmeRead32(&FAp[id]->ctrl1) | suppress_bits);
  else
    vmeWrite32(&FAp[id]->ctrl1,
	       vmeRead32(&FAp[id]->ctrl1) & ~suppress_bits);
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Status
 *  @brief Return raw register data from the Control FPGA containing the temperature, 
 *    core voltage, and auxiliary voltage.
 *  @param id Slot number
 *  @return Register value if successful, otherwise ERROR.
 */
unsigned int
faGetCtrlFPGAData(int id)
{
  unsigned int rval=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return ERROR;
    }

  FALOCK;
  rval = vmeRead32(&FAp[id]->system_monitor);
  FAUNLOCK;

  return rval;
}

/**
 *  @ingroup Status
 *  @brief Return raw register data from the Processing FPGA containing the temperature.
 *  @param id Slot number
 *  @return Register value if successful, otherwise ERROR.
 */
unsigned int
faGetProcFPGAData(int id)
{
  unsigned int rval=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return ERROR;
    }

  FALOCK;
  rval = vmeRead32(&FAp[id]->status3) & 0xFFFF;
  FAUNLOCK;

  return rval;
}

/**
 *  @ingroup Status
 *  @brief Return the value of the Control FPGA temperature (in degrees Celsius)
 *  @param id Slot number
 *  @param pflag Print Flag
 *    -  !0: Print temperature to standard out
 *  @return Temperature if successful, otherwise ERROR.
 */
float
faGetCtrlFPGATemp(int id, int pflag)
{
  float rval=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return ERROR;
    }

  FALOCK;
  rval = 
    ((float)(vmeRead32(&FAp[id]->system_monitor) & FA_SYSMON_CTRL_TEMP_MASK) *
     (503.975/1024.0) - 273.15);
  FAUNLOCK;

  if(pflag)
    {
      printf("%s: CTRL FPGA Temperature = %.1f [deg C]\n",
	     __FUNCTION__,rval);
    }

  return rval;
}

/**
 *  @ingroup Status
 *  @brief Return the value of specified Control FPGA voltage.
 *  @param id Slot number
 *  @param vtype Voltage type
 *    -  0: Core Voltage
 *    -  1: Auxiliary Voltage
 *  @param pflag Print flag
 *    - !0: Print voltage to standard out.
 *  @return Specified voltage if successful, otherwise ERROR.
 */
float
faGetCtrlFPGAVoltage(int id, int vtype, int pflag)
{
  float rval=0;
  unsigned int reg=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return ERROR;
    }

  if((pflag>2) || (pflag<0))
    {
      printf("%s: ERROR: Invalid vtype (%d)\n",
	     __FUNCTION__,vtype);
      return ERROR;
    }

  FALOCK;
  reg = vmeRead32(&FAp[id]->system_monitor);
  if(vtype==0)
    {
      reg  = (reg & FA_SYSMON_FPGA_CORE_V_MASK)>>11;
    }
  else
    {
      reg  = (reg & FA_SYSMON_FPGA_AUX_V_MASK)>>22;
    }
  rval = ((float)(reg))*3.0/1024.0;
  FAUNLOCK;

  if(pflag)
    {
      printf("%s: CTRL FPGA %s Voltage = %.1f [V]\n",
	     __FUNCTION__,
	     (vtype==0)?
	     "Core":
	     "Auxiliary",
	     rval);
    }

  return rval;
}

/**
 *  @ingroup Status
 *  @brief Return the value of the Processing FPGA temperature (in degrees Celsius)
 *  @param id Slot number
 *  @param pflag Print Flag
 *    -  !0: Print temperature to standard out
 *  @return Temperature if successful, otherwise ERROR.
 */
float
faGetProcFPGATemp(int id, int pflag)
{
  float rval=0;
  unsigned int reg=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return ERROR;
    }

  FALOCK;
  reg = (vmeRead32(&FAp[id]->status3) & FA_STATUS3_PROC_TEMP_MASK);
  rval = ((float)reg * (503.975/1024.0) - 273.15);
  FAUNLOCK;

  if(pflag)
    {
      printf("%s: PROC FPGA Temperature = %.1f [deg C]\n",
	     __FUNCTION__,rval);
    }

  return rval;
}

/* -------------------------------------------------------------------------------------
   Utility routines
*/

/**
 *  @ingroup Status
 *  @brief Print to standard out some auxillary scalers
 *
 *   Prints out 
 *     - Total number of words generated
 *     - Total number of headers generated
 *     - Total number of trailers generated  
 *     - Total number of lost triggers
 *
 *  @param id Slot number
 */
void 
faPrintAuxScal(int id)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faPrintAuxScal: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
  
  FALOCK;
  printf("Auxillary Scalers:\n");
  printf("       Word Count:         %d\n",
	 vmeRead32(&FAp[id]->proc_words_scal));
  printf("       Headers   :         %d\n",
	 vmeRead32(&FAp[id]->header_scal));
  printf("       Trailers  :         %d\n",
	 vmeRead32(&FAp[id]->trailer_scal));
  printf("  Lost Triggers  :         %d\n",
	 vmeRead32(&FAp[id]->lost_trig_scal));
  FAUNLOCK;

  return;
}

/**
 *  @ingroup Status
 *  @brief Print the status of the FIFO to standard out
 *  @param id Slot number
 */
void 
faPrintFifoStatus(int id)
{ 
  unsigned int ibuf, bbuf, obuf, dflow;
  unsigned int wc[2],mt[2],full[2];
  unsigned int rdy[2];

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faPrintFifoStatus: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }
  
  FALOCK;
  dflow = vmeRead32(&(FAp[id]->dataflow_status));
  ibuf = vmeRead32(&(FAp[id]->status[0]))&0xdfffdfff;
  bbuf = vmeRead32(&(FAp[id]->status[1]))&0x1fff1fff;
  obuf = vmeRead32(&(FAp[id]->status[2]))&0x3fff3fff;
  FAUNLOCK;

  printf("%s: Fifo Buffers Status (DataFlow Status = 0x%08x\n",
	 __FUNCTION__,dflow);

  mt[1]  = full[1] = 0;
  wc[1]  = (ibuf&0x7ff0000)>>16;
  rdy[1] = (ibuf&0x80000000)>>31;
  if(ibuf&0x8000000)  full[1]=1;
  if(ibuf&0x10000000) mt[1]=1;

  printf("  Input Buffer : 0x%08x \n",ibuf);
  printf("    FPGA : wc=%d   Empty=%d Full=%d Ready=%d\n",wc[1],mt[1],full[1],rdy[1]);

  mt[0]=full[0]=0;
  wc[0]   =  bbuf&0x7ff;
  if(bbuf&0x800) full[0]=1;
  if(bbuf&0x1000) mt[0]=1;

  mt[1]=full[1]=0;
  wc[1]   = (bbuf&0x7ff0000)>>16;
  if(bbuf&0x8000000)  full[1]=1;
  if(bbuf&0x10000000) mt[1]=1;

  printf("  Build Buffer : 0x%08x \n",bbuf);
  printf("    BUF_A: wc=%d   Empty=%d Full=%d \n",wc[1],mt[1],full[1]);
  printf("    BUF_B: wc=%d   Empty=%d Full=%d \n",wc[0],mt[0],full[0]);

  mt[0]=full[0]=0;
  wc[0]   =  obuf&0xfff;
  if(obuf&0x1000) full[0]=1;
  if(obuf&0x2000) mt[0]=1;

  mt[1]=full[1]=0;
  wc[1]   = (obuf&0xfff0000)>>16;
  if(obuf&0x10000000)  full[1]=1;
  if(obuf&0x20000000) mt[1]=1;

  printf("  Output Buffer: 0x%08x \n",obuf);
  printf("    BUF_A: wc=%d   Empty=%d Full=%d \n",wc[1],mt[1],full[1]);
  printf("    BUF_B: wc=%d   Empty=%d Full=%d \n",wc[0],mt[0],full[0]);


  return;

}

/**
 *  @ingroup Status
 *  @brief Decode a data word from an fADC250 and print to standard out.
 *  @param data 32bit fADC250 data word
 */
void 
faDataDecode(unsigned int data)
{
  int i_print = 1;
  static unsigned int type_last = 15;	/* initialize to type FILLER WORD */
  static unsigned int time_last = 0;
  int idata=0;

  if( data & 0x80000000 )		/* data type defining word */
    {
      fadc_data.new_type = 1;
      fadc_data.type = (data & 0x78000000) >> 27;
    }
  else
    {
      fadc_data.new_type = 0;
      fadc_data.type = type_last;
    }
        
  switch( fadc_data.type )
    {
    case 0:		/* BLOCK HEADER */
      if( fadc_data.new_type )
	{
	  fadc_data.slot_id_hd = ((data) & 0x7C00000) >> 22;
	  fadc_data.modID      = (data & 0x3C0000)>>18;
	  fadc_data.blk_num    = (data & 0x3FF00) >> 8;
	  fadc_data.n_evts     = (data & 0xFF);
	  if( i_print ) 
	    printf("%8X - BLOCK HEADER - slot = %d  modID = %d   n_evts = %d   n_blk = %d\n",
		   data, fadc_data.slot_id_hd, 
		   fadc_data.modID, fadc_data.n_evts, fadc_data.blk_num);
	}
      else
	{
	  fadc_data.PL  = (data & 0x1FFC0000) >> 18;
	  fadc_data.NSB = (data & 0x0003FE00) >> 9;
	  fadc_data.NSA = (data & 0x000001FF) >> 0;

	  printf("%8X - BLOCK HEADER 2 - PL = %d  NSB = %d  NSA = %d\n",
		 data, 
		 fadc_data.PL,
		 fadc_data.NSB,
		 fadc_data.NSA);
	}
      break;

    case 1:		/* BLOCK TRAILER */
      fadc_data.slot_id_tr = (data & 0x7C00000) >> 22;
      fadc_data.n_words = (data & 0x3FFFFF);
      if( i_print ) 
	printf("%8X - BLOCK TRAILER - slot = %d   n_words = %d\n",
	       data, fadc_data.slot_id_tr, fadc_data.n_words);
      break;

    case 2:		/* EVENT HEADER */
      fadc_data.time_low_10 = (data & 0x003FF000) >> 12;
      fadc_data.evt_num_1 = (data & 0x3FFFFF);
      if( i_print ) 
	printf("%8X - EVENT HEADER 1 - trig time = %d   trig num = %d\n", data, 
	       fadc_data.time_low_10, fadc_data.evt_num_1);
      break;

    case 3:		/* TRIGGER TIME */
      if( fadc_data.new_type )
	{
	  fadc_data.time_1 = (data & 0x07FFFFFF);
	  if( i_print ) 
	    printf("%8X - TRIGGER TIME 1 - time = %08x\n", data, fadc_data.time_1);
	  fadc_data.time_now = 1;
	  time_last = 1;
	}    
      else
	{
	  if( time_last == 1 )
	    {
	      fadc_data.time_2 = (data & 0xFFFFFF);
	      if( i_print ) 
		printf("%8X - TRIGGER TIME 2 - time = %08x\n", data, fadc_data.time_2);
	      fadc_data.time_now = 2;
	    }    
	  else if( time_last == 2 )
	    {
	      fadc_data.time_3 = (data & 0xFFFFFF);
	      if( i_print ) 
		printf("%8X - TRIGGER TIME 3 - time = %08x\n", data, fadc_data.time_3);
	      fadc_data.time_now = 3;
	    }    
	  else if( time_last == 3 )
	    {
	      fadc_data.time_4 = (data & 0xFFFFFF);
	      if( i_print ) 
		printf("%8X - TRIGGER TIME 4 - time = %08x\n", data, fadc_data.time_4);
	      fadc_data.time_now = 4;
	    }    
	  else
	    if( i_print ) 
	      printf("%8X - TRIGGER TIME - (ERROR)\n", data);
	                
	  time_last = fadc_data.time_now;
	}    
      break;

    case 4:		/* WINDOW RAW DATA */
      if( fadc_data.new_type )
	{
	  fadc_data.chan = (data & 0x7800000) >> 23;
	  fadc_data.width = (data & 0xFFF);
	  if( i_print ) 
	    printf("%8X - WINDOW RAW DATA - chan = %d   nsamples = %d\n", 
		   data, fadc_data.chan, fadc_data.width);
	}    
      else
	{
	  fadc_data.valid_1 = 1;
	  fadc_data.valid_2 = 1;
	  fadc_data.adc_1 = (data & 0x1FFF0000) >> 16;
	  if( data & 0x20000000 )
	    fadc_data.valid_1 = 0;
	  fadc_data.adc_2 = (data & 0x1FFF);
	  if( data & 0x2000 )
	    fadc_data.valid_2 = 0;
	  if( i_print ) 
	    printf("%8X - RAW SAMPLES - valid = %d  adc = %4d   valid = %d  adc = %4d\n", 
		   data, fadc_data.valid_1, fadc_data.adc_1, 
		   fadc_data.valid_2, fadc_data.adc_2);
	}    
      break;
 
    case 5:		/* UNDEFINED TYPE */
      if( i_print ) 
	printf("%8X - UNDEFINED TYPE = %d\n", data, fadc_data.type);
      break;

    case 6:		/* PULSE RAW DATA */
      if( fadc_data.new_type )
	{
	  fadc_data.chan = (data & 0x7800000) >> 23;
	  fadc_data.pulse_num = (data & 0x600000) >> 21;
	  fadc_data.thres_bin = (data & 0x3FF);
	  if( i_print ) 
	    printf("%8X - PULSE RAW DATA - chan = %d   pulse # = %d   threshold bin = %d\n", 
		   data, fadc_data.chan, fadc_data.pulse_num, fadc_data.thres_bin);
	}    
      else
	{
	  fadc_data.valid_1 = 1;
	  fadc_data.valid_2 = 1;
	  fadc_data.adc_1 = (data & 0x1FFF0000) >> 16;
	  if( data & 0x20000000 )
	    fadc_data.valid_1 = 0;
	  fadc_data.adc_2 = (data & 0x1FFF);
	  if( data & 0x2000 )
	    fadc_data.valid_2 = 0;
	  if( i_print ) 
	    printf("%8X - PULSE RAW SAMPLES - valid = %d  adc = %d   valid = %d  adc = %d\n", 
		   data, fadc_data.valid_1, fadc_data.adc_1, 
		   fadc_data.valid_2, fadc_data.adc_2);
	}    
      break;

    case 7:		/* PULSE INTEGRAL */
      fadc_data.chan = (data & 0x7800000) >> 23;
      fadc_data.pulse_num = (data & 0x600000) >> 21;
      fadc_data.quality = (data & 0x180000) >> 19;
      fadc_data.integral = (data & 0x7FFFF);
      if( i_print ) 
	printf("%8X - PULSE INTEGRAL - chan = %d   pulse # = %d   quality = %d   integral = %d\n", 
	       data, fadc_data.chan, fadc_data.pulse_num, 
	       fadc_data.quality, fadc_data.integral);
      break;
 
    case 8:		/* PULSE TIME */
      fadc_data.chan = (data & 0x7800000) >> 23;
      fadc_data.pulse_num = (data & 0x600000) >> 21;
      fadc_data.quality = (data & 0x180000) >> 19;
      fadc_data.time = (data & 0xFFFF);
      if( i_print ) 
	printf("%8X - PULSE TIME - chan = %d   pulse # = %d   quality = %d   time = %d\n", 
	       data, fadc_data.chan, fadc_data.pulse_num, 
	       fadc_data.quality, fadc_data.time);
      break;

    case 9:		/* PULSE PARAMETERS */
      if( fadc_data.new_type )
	{ /* Channel ID and Pedestal Info */
	  fadc_data.pulse_num  = 0; /* Initialize */
	  fadc_data.evt_of_blk = (data & 0x07f80000)>>19;
	  fadc_data.chan       = (data & 0x00078000)>>15;
	  fadc_data.quality    = (data & (1<<14))>>14;
	  fadc_data.ped_sum    = (data & 0x00003fff);

	      printf("%8X - PULSEPARAM 1 - evt = %d   chan = %d   quality = %d   pedsum = %d\n", 
		 data, 
		 fadc_data.evt_of_blk, 
		 fadc_data.chan, 
		 fadc_data.quality, 
		 fadc_data.ped_sum);
	}
      else
	{
	  if(data & (1<<30))
	    { /* Word 1: Integral of n-th pulse in window */
	      fadc_data.pulse_num++;
	      fadc_data.adc_sum = (data & 0x3ffff000)>>12;
	      fadc_data.nsa_ext = (data & (1<<11))>>11;
	      fadc_data.over    = (data & (1<<10))>>10;
	      fadc_data.under   = (data & (1<<9))>>9;
	      fadc_data.samp_ov_thres = (data & 0x000001ff);

	      printf("%8X - PULSEPARAM 2 - P# = %d  Sum = %d  NSA+ = %d  Ov/Un = %d/%d  #OT = %d\n", 
		     data, 
		     fadc_data.pulse_num, 
		     fadc_data.adc_sum, 
		     fadc_data.nsa_ext, 
		     fadc_data.over, 
		     fadc_data.under, 
		     fadc_data.samp_ov_thres);
	    }
	  else
	    { /* Word 2: Time of n-th pulse in window */
	      fadc_data.time_coarse = (data & 0x3fe00000)>>21;
	      fadc_data.time_fine   = (data & 0x001f8000)>>15;
	      fadc_data.vpeak       = (data & 0x00007ff8)>>3;
	      fadc_data.quality     = (data & 0x2)>>1;
	      fadc_data.quality2    = (data & 0x1);

	      printf("%8X - PULSEPARAM 3 - CTime = %d  FTime = %d  Peak = %d  NoVp = %d  Q = %d\n", 
		     data, 
		     fadc_data.time_coarse, 
		     fadc_data.time_fine, 
		     fadc_data.vpeak, 
		     fadc_data.quality, 
		     fadc_data.quality2);
	    }
	}

      break;

    case 10:		/* UNDEFINED TYPE */
      if( i_print ) 
	printf("%8X - UNDEFINED TYPE = %d\n", data, fadc_data.type);
      break;

    case 11:		/* UNDEFINED TYPE */
      if( i_print ) 
	printf("%8X - UNDEFINED TYPE = %d\n", data, fadc_data.type);
      break;

    case 12:		/* SCALER HEADER */
      if( fadc_data.new_type )
	{
	  fadc_data.scaler_data_words = (data & 0x3F);
	  if( i_print ) 
	    printf("%8X - SCALER HEADER - data words = %d\n", data, fadc_data.scaler_data_words);
	}
      else
	{
	  for(idata=0; idata<fadc_data.scaler_data_words; idata++)
	    {
	      if( i_print ) 
		printf("%8X - SCALER DATA - word = %2d  counter = %d\n", 
		       data, idata, data);
	    }
	}
      break;
 
    case 13:		/* END OF EVENT */
      if( i_print ) 
	printf("%8X - END OF EVENT = %d\n", data, fadc_data.type);
      break;

    case 14:		/* DATA NOT VALID (no data available) */
      if( i_print ) 
	printf("%8X - DATA NOT VALID = %d\n", data, fadc_data.type);
      break;

    case 15:		/* FILLER WORD */
      if( i_print ) 
	printf("%8X - FILLER WORD = %d\n", data, fadc_data.type);
      break;
    }
	
  type_last = fadc_data.type;	/* save type of current data word */
		   
}        

/**
 *  @ingroup Config
 *  @brief Enable/Disable System test mode
 *  @param id Slot number
 *  @param mode 
 *    -  0: Disable Test Mode
 *    - >0: Enable Test Mode
 */
void
faTestSetSystemTestMode(int id, int mode)
{
  int reg=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return;
    }

  if(mode>=1) 
    reg=FA_CTRL1_SYSTEM_TEST_MODE;
  else 
    reg=0;
    
  FALOCK;

  vmeWrite32(&(FAp[id]->ctrl1),
	     vmeRead32(&FAp[id]->ctrl1) | reg);

  /*   printf(" ctrl1 = 0x%08x\n",vmeRead32(&FAp[id]->ctrl1)); */
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Set the level of Trig Out to the SD
 *
 *   Available only in System Test Mode   
 *
 *  @sa faTestSetSystemTestMode
 *  @param id Slot number
 *  @param mode 
 *    -  0: Not asserted
 *    - >0: Asserted
 */
void
faTestSetTrigOut(int id, int mode)
{
  int reg=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return;
    }

  if(mode>=1) 
    reg=FA_TESTBIT_TRIGOUT;
  else 
    reg=0;
    
  FALOCK;
  vmeWrite32(&(FAp[id]->testBit),reg);
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Set the level of Busy Out to the SD
 *
 *   Available only in System Test Mode   
 *
 *  @sa faTestSetSystemTestMode
 *  @param id Slot number
 *  @param mode 
 *    -  0: Not asserted
 *    - >0: Asserted
 */
void
faTestSetBusyOut(int id, int mode)
{
  int reg=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return;
    }

  if(mode>=1) 
    reg=FA_TESTBIT_BUSYOUT;
  else 
    reg=0;
    
  FALOCK;
  vmeWrite32(&(FAp[id]->testBit),reg);
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Set the level of the SD Link
 *
 *   Available only in System Test Mode   
 *
 *  @sa faTestSetSystemTestMode
 *  @param id Slot number
 *  @param mode 
 *    -  0: Not asserted
 *    - >0: Asserted
 */
void
faTestSetSdLink(int id, int mode)
{
  int reg=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return;
    }

  if(mode>=1) 
    reg=FA_TESTBIT_SDLINKOUT;
  else 
    reg=0;
    
  FALOCK;
  vmeWrite32(&(FAp[id]->testBit),reg);
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Set the level of Token Out to the SD
 *
 *   Available only in System Test Mode   
 *
 *  @sa faTestSetSystemTestMode
 *  @param id Slot number
 *  @param mode 
 *    -  0: Not asserted
 *    - >0: Asserted
 */
void
faTestSetTokenOut(int id, int mode)
{
  int reg=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return;
    }

  if(mode>=1) 
    reg=FA_TESTBIT_TOKENOUT;
  else 
    reg=0;
    
  FALOCK;
  vmeWrite32(&(FAp[id]->testBit),reg);
  FAUNLOCK;

}

/**
 *  @ingroup Status
 *  @brief Get the level of the StatBitB to the SD
 *
 *   Available only in System Test Mode   
 *
 *  @sa faTestSetSystemTestMode
 *  @param id Slot number
 *  @return 1 if asserted, 0 if not, otherwise ERROR.
 */
int
faTestGetStatBitB(int id)
{
  int reg=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return ERROR;
    }

  FALOCK;
  reg = (vmeRead32(&FAp[id]->testBit) & FA_TESTBIT_STATBITB)>>8;
  FAUNLOCK;

  return reg;

}

/**
 *  @ingroup Status
 *  @brief Get the level of the Token In from the SD
 *
 *   Available only in System Test Mode   
 *
 *  @sa faTestSetSystemTestMode
 *  @param id Slot number
 *  @return 1 if asserted, 0 if not, otherwise ERROR.
 */
int
faTestGetTokenIn(int id)
{
  int reg=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return ERROR;
    }

  FALOCK;
  reg = (vmeRead32(&FAp[id]->testBit) & FA_TESTBIT_TOKENIN)>>9;
  FAUNLOCK;

  return reg;

}

/**
 *  @ingroup Status
 *  @brief Return the status of the 250Mhz Clock Counter
 *
 *   Available only in System Test Mode   
 *
 *  @sa faTestSetSystemTestMode
 *  @param id Slot number
 *  @return 1 if counting, 0 if not counting, otherwise ERROR.
 */
int
faTestGetClock250CounterStatus(int id)
{
  int reg=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return ERROR;
    }

  FALOCK;
  reg = (vmeRead32(&FAp[id]->testBit) & FA_TESTBIT_CLOCK250_STATUS)>>15;
  FAUNLOCK;

  return reg;

}

/**
 *  @ingroup Status
 *  @brief Return the value of the 250Mhz Clock scaler
 *
 *   Available only in System Test Mode   
 *
 *  @sa faTestSetSystemTestMode
 *  @param id Slot number
 *  @return 250Mhz Clock scaler counter if successful, otherwise ERROR.
 */
unsigned int
faTestGetClock250Counter(int id)
{
  unsigned int reg=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return ERROR;
    }

  FALOCK;
  reg = vmeRead32(&FAp[id]->clock250count);
  FAUNLOCK;

  return reg;

}

/**
 *  @ingroup Status
 *  @brief Return the value of the SyncReset scaler
 *
 *   Available only in System Test Mode   
 *
 *  @sa faTestSetSystemTestMode
 *  @param id Slot number
 *  @return SyncReset scaler counter if successful, otherwise ERROR.
 */
unsigned int
faTestGetSyncCounter(int id)
{
  unsigned int reg=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return ERROR;
    }

  FALOCK;
  reg = vmeRead32(&FAp[id]->syncp0count);
  FAUNLOCK;

  return reg;

}

/**
 *  @ingroup Status
 *  @brief Return the value of the trig1 scaler
 *
 *   Available only in System Test Mode   
 *
 *  @sa faTestSetSystemTestMode
 *  @param id Slot number
 *  @return trig1 scaler counter if successful, otherwise ERROR.
 */
unsigned int
faTestGetTrig1Counter(int id)
{
  unsigned int reg=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return ERROR;
    }

  FALOCK;
  reg = vmeRead32(&FAp[id]->trig1p0count);
  FAUNLOCK;

  return reg;

}

/**
 *  @ingroup Status
 *  @brief Return the value of the trig2 scaler
 *
 *   Available only in System Test Mode   
 *
 *  @sa faTestSetSystemTestMode
 *  @param id Slot number
 *  @return trig2 scaler counter if successful, otherwise ERROR.
 */
unsigned int
faTestGetTrig2Counter(int id)
{
  unsigned int reg=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return ERROR;
    }

  FALOCK;
  reg = vmeRead32(&FAp[id]->trig2p0count);
  FAUNLOCK;

  return reg;

}

/**
 *  @ingroup Status
 *  @brief Reset the counter of the 250MHz Clock scaler
 *
 *   Available only in System Test Mode   
 *
 *  @sa faTestSetSystemTestMode
 *  @param id Slot number
 */
void
faTestResetClock250Counter(int id)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->clock250count,FA_CLOCK250COUNT_RESET);
  vmeWrite32(&FAp[id]->clock250count,FA_CLOCK250COUNT_START);
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Reset the counter of the SyncReset scaler
 *
 *   Available only in System Test Mode   
 *
 *  @sa faTestSetSystemTestMode
 *  @param id Slot number
 */
void
faTestResetSyncCounter(int id)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->syncp0count,FA_SYNCP0COUNT_RESET);
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Reset the counter of the trig1 scaler
 *
 *   Available only in System Test Mode   
 *
 *  @sa faTestSetSystemTestMode
 *  @param id Slot number
 */
void
faTestResetTrig1Counter(int id)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->trig1p0count,FA_TRIG1P0COUNT_RESET);
  FAUNLOCK;

}

/**
 *  @ingroup Config
 *  @brief Reset the counter of the trig2 scaler
 *
 *   Available only in System Test Mode   
 *
 *  @sa faTestSetSystemTestMode
 *  @param id Slot number
 */
void
faTestResetTrig2Counter(int id)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->trig2p0count,FA_TRIG2P0COUNT_RESET);
  FAUNLOCK;

}

/**
 *  @ingroup Status
 *  @brief Return the current value of the testBit register
 *
 *   Available only in System Test Mode   
 *
 *  @sa faTestSetSystemTestMode
 *  @param id Slot number
 *  @return testBit register value if successful, otherwise ERROR.
 */
unsigned int
faTestGetTestBitReg(int id)
{
  unsigned int rval=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return 0;
    }

  FALOCK;
  rval = vmeRead32(&FAp[id]->testBit);
  FAUNLOCK;

  return rval;
}


/**
 *  @ingroup Status
 *  @brief Fills 'rval' with a character array containing the fa250 serial number.
 *  @param id Slot number
 *  @param rval Where to return Serial number string
 *  @param snfix
 *<pre>
 *      If snfix >= 1, will attempt to format the serial number to maintain
 *        consistency between serials made by the same vendor.
 *        e.g. Advanced Assembly:
 *          snfix=0: B21595-02R  B2159515R1
 *          snfix=1: B21595-02R  B21595-15R1
 *        e.g. ACDI
 *          snfix=0: ACDI002
 *          snfix=1: ACDI-002
 *</pre>
 *  @return length of character array 'rval' if successful, otherwise ERROR
 */
int
faGetSerialNumber(int id, char **rval, int snfix)
{
  unsigned int sn[3];
  int i=0, ivme=0, ibyte=0;
  unsigned int byte, prev_byte;
  unsigned int shift=0, mask=0;
  unsigned int boardID;
  char boardID_c[4];
  char byte_c[2];
  char adv_str[12], acdi_str[12];
  char ret[12];
  int ret_len;
  
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return ERROR;
    }

  FALOCK;
  for(i=0; i<3; i++)
    sn[i] = vmeRead32(&FAp[id]->serial_number[i]);
  FAUNLOCK;

  if(sn[0]==FA_SERIAL_NUMBER_ACDI)
    { /* ACDI */
      strcpy(acdi_str,"");
      for(ibyte=3; ibyte>=0; ibyte--)
	{
	  shift = (ibyte*8);
	  mask = (0xFF)<<shift;
	  byte = (sn[ivme]&mask)>>shift;
	  sprintf(byte_c,"%c",byte);
	  strcat(acdi_str,byte_c);
	}
      boardID = (sn[1] & FA_SERIAL_NUMBER_ACDI_BOARDID_MASK);
      if(boardID>999)
	{
	  printf("%s: WARN: Invalid Board ACDI Board ID (%d)\n",
		 __FUNCTION__,boardID);
	}

      if(snfix>0) /* If needed, Add '-' after the ACDI */
	sprintf(boardID_c,"-%03d",boardID);
      else
	sprintf(boardID_c,"%03d",boardID);

      strcat(acdi_str,boardID_c);
#ifdef DEBUGSN
      printf("acdi_str = %s\n",acdi_str);
#endif
      strcpy(ret,acdi_str);

    }

  else if((sn[0] & FA_SERIAL_NUMBER_ADV_ASSEM_MASK)==FA_SERIAL_NUMBER_ADV_ASSEM)

    { /* ADV ASSEM */
      /* Make sure manufacture's ID is correct */
      if((sn[0] == FA_SERIAL_NUMBER_ADV_MNFID1) &&
	 ((sn[1] & FA_SERIAL_NUMBER_ADV_MNFID2_MASK) == FA_SERIAL_NUMBER_ADV_MNFID2) )
	{
	  strcpy(adv_str,"");
	  for(ivme=0; ivme<3; ivme++)
	    {
	      for(ibyte=3; ibyte>=0; ibyte--)
		{
		  shift = (ibyte*8);
		  mask = (0xFF)<<shift;
		  byte = (sn[ivme]&mask)>>shift;
		  if(byte==0xFF)
		    {
		      break;
		    }
		  if(snfix>0)
		    { /* If needed, Add '-' after the B21595 */
		      if(ivme==1 && ibyte==1)
			{
			  if(byte!=0x2D) /* 2D = - */
			    {
			      strcat(adv_str,"-");
			    }
			}
		    }

		  sprintf(byte_c,"%c",byte);
		  strcat(adv_str,byte_c);
		  prev_byte = byte;
		}
	    }
#ifdef DEBUGSN
	  printf("adv_str = %s\n",adv_str);
#endif
	  strcpy(ret,adv_str);
	}
      else
	{
	  printf("%s: ERROR: Unable to determine manufacture's ID.  SN regs:\n",
		 __FUNCTION__);
	  for(i=0; i<3; i++)
	    printf("\t%d: 0x%08x\n",i,sn[i]);
	  return -1;
	}
    }
  else
    {
      printf("%s: ERROR: Unable to determine manufacture's ID. SN regs:\n",
	     __FUNCTION__);
      for(i=0; i<3; i++)
	printf("\t%d: 0x%08x\n",i,sn[i]);
      return -1;
    }

#ifdef DEBUGSN
  printf("ret = %s\n",ret);
#endif
  strcpy((char *)rval,ret);

  ret_len = (int)strlen(ret);
  
  return(ret_len);

}


/**
 *  @ingroup Config
 *  @brief Set the block interval of scaler data insertion
 *
 *   Data from scalers may be inserted into the readout data stream at
 *   regular event count intervals.  The interval is specified in
 *   multiples of blocks.
 *    Note: Scalers are NOT reset after their values are captured.
 *
 *  @param id Slot number
 *  @param nblock
 *    -   0: No insertion of scaler data into the data stream
 *    - >=1: The current scaler values are appended to the last event of the appropriate n'th block of events.
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetScalerBlockInterval(int id, unsigned int nblock)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return ERROR;
    }

  if(nblock > FA_SCALER_INTERVAL_MASK)
    {
      printf("%s: ERROR: Invalid value of nblock (%d).\n",
	     __FUNCTION__,nblock);
      return ERROR;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->scaler_interval,nblock);
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Status
 *  @brief 
 *  @param id Slot number
 *  @return OK if successful, otherwise ERROR.
 */
int
faGetScalerBlockInterval(int id)
{
  int rval=0;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",__FUNCTION__,
	     id);
      return ERROR;
    }

  FALOCK;
  rval = vmeRead32(&FAp[id]->scaler_interval) & FA_SCALER_INTERVAL_MASK;
  FAUNLOCK;

  return rval;
}

/**
 *  @ingroup Config
 *  @brief Allows for the insertion of a block trailer into the data stream.
 *
 *      Allows for the insertion of a block trailer into the data stream.  This is
 *      useful for the efficient extraction of a partial block of events
 *      from the FADC (e.g. for an end of run event, or the resynchonize with
 *      other modules).  
 *      Event count within block is reset, after successful execution.
 *
 *  @param id Slot number
 *  @param scalers If set to > 0, scalers will also be inserted with the End of Block
 *  @return OK if successful, otherwise ERROR.
 */
int
faForceEndOfBlock(int id, int scalers)
{
  int rval=OK, icheck=0, timeout=1000, csr=0;
  int proc_config=0;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faForceEndOfBlock: ERROR : ADC in slot %d is not initialized \n",
	     id,2,3,4,5,6);
      return ERROR;
    }

  FALOCK;
  /* Disable triggers to Processing FPGA (if enabled) */
  proc_config = vmeRead32(&FAp[id]->adc_config[0]);
  vmeWrite32(&FAp[id]->adc_config[0],
	     proc_config & ~(FA_ADC_PROC_ENABLE));

  csr = FA_CSR_FORCE_EOB_INSERT;
  if(scalers>0)
    csr |= FA_CSR_DATA_STREAM_SCALERS;

  vmeWrite32(&FAp[id]->csr, csr);

  for(icheck=0; icheck<timeout; icheck++)
    {
      csr = vmeRead32(&FAp[id]->csr);
      if(csr & FA_CSR_FORCE_EOB_SUCCESS)
	{
	  logMsg("faForceEndOfBlock: Block trailer insertion successful\n",
		 1,2,3,4,5,6);
	  rval = ERROR;
	  break;
	}

      if(csr & FA_CSR_FORCE_EOB_FAILED)
	{
	  logMsg("faForceEndOfBlock: Block trailer insertion FAILED\n",
		 1,2,3,4,5,6);
	  rval = ERROR;
	  break;
	}
    }

  if(icheck==timeout)
    {
      logMsg("faForceEndOfBlock: Block trailer insertion FAILED on timeout\n",
	     1,2,3,4,5,6);
      rval = ERROR;
    }

  /* Restore the original state of the Processing FPGA */
  vmeWrite32(&FAp[id]->adc_config[0], proc_config);

  FAUNLOCK;

  return rval;
}
/**
 *  @ingroup Config
 *  @brief Allows for the insertion of a block trailer into the data stream for all 
 *    initialized fADC250s
 *
 *      Allows for the insertion of a block trailer into the data stream.  This is
 *      useful for the efficient extraction of a partial block of events
 *      from the FADC (e.g. for an end of run event, or the resynchonize with
 *      other modules).  
 *      Event count within block is reset, after successful execution.
 *
 *  @param scalers If set to > 0, scalers will also be inserted with the End of Block
 */
void
faGForceEndOfBlock(int scalers)
{
  int ii, res;

  for (ii=0;ii<nfadc;ii++) {
    res = faForceEndOfBlock(fadcID[ii], scalers);
    if(res<0) 
      printf("%s: ERROR: slot %d, in faForceEndOfBlock()\n",
	     __FUNCTION__,fadcID[ii]);
  }

}

/**
 *  @ingroup Config
 *  @brief Set the threshold to trigger for the history buffer to be saved for readout
 *  @param id Slot number
 *  @param thres History Buffer Threshold
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetHistoryBufferThreshold(int id, int thres)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return ERROR;
    }

  if(thres>FA_SUM_THRESHOLD_MASK)
    {
      printf("%s: ERROR: Invalid value for threshold (%d)\n",
	     __FUNCTION__,thres);
      return ERROR;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->sum_threshold,thres);
  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Set the threshold to trigger for the history buffer to be saved for readout 
 *     for all initialized fADC250s
 *  @param thres History Buffer Threshold
 */
void
faGSetHistoryBufferThreshold(int thres)
{
  int ifa=0;

  for (ifa=0;ifa<nfadc;ifa++) 
    {
      faSetHistoryBufferThreshold(faSlot(ifa),thres);
    }
}

/**
 *  @ingroup Status
 *  @brief Get the history buffer threshold
 *  @param id Slot number
 *  @return threshold if successful, otherwise ERROR.
 */
int
faGetHistoryBufferThreshold(int id)
{
  int rval=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return ERROR;
    }
  
  FALOCK;
  rval = vmeRead32(&FAp[id]->sum_threshold) & FA_SUM_THRESHOLD_MASK;
  FAUNLOCK;

  return rval;
}

/**
 *  @ingroup Config
 *  @brief Enable the history buffer for data acquisition for the module
 *  @param id Slot number
 *  @return OK if successful, otherwise ERROR.
 */
int
faArmHistoryBuffer(int id)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faArmHistoryBuffer: ERROR : ADC in slot %d is not initialized \n",
	     id,2,3,4,5,6);
      return ERROR;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->sum_data, FA_SUM_DATA_ARM_HISTORY_BUFFER);
  FAUNLOCK;
  return OK;
}

/**
 *  @ingroup Config
 *  @brief Enable the history buffer for data acquisition for all initialized fADC250s
 */
void
faGArmHistoryBuffer()
{
  int ifa=0;

  for (ifa=0;ifa<nfadc;ifa++) 
    {
      faArmHistoryBuffer(faSlot(ifa));
    }
}

/**
 *  @ingroup Readout
 *  @brief Return whether or not the history buffer has been triggered
 *  @param id Slot number
 *  @return 1 if history buffer data is ready for readout, 0 if not, otherwise ERROR.
 */
int
faHistoryBufferDReady(int id)
{
  int rval=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faHistoryBufferDReady: ERROR : ADC in slot %d is not initialized \n",
	     id,2,3,4,5,6);
      return ERROR;
    }

  FALOCK;
  rval = (vmeRead32(&FAp[id]->sum_threshold) & FA_SUM_THRESHOLD_DREADY)>>31;
  FAUNLOCK;

  return rval;
}

/**
 *  @ingroup Readout
 *  @brief Read out history buffer from the module
 *  @param id Slot number
 *  @param  data   local memory address to place data
 *  @param  nwrds  Max number of words to transfer
 *  @return Number of words read if successful, otherwise ERROR.
 */
int
faReadHistoryBuffer(int id, volatile unsigned int *data, int nwrds)
{
  int idata=0, dCnt=0;
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faHistoryBufferDReady: ERROR : ADC in slot %d is not initialized \n",
	     id,2,3,4,5,6);
      return ERROR;
    }

  FALOCK;
  while(idata<nwrds)
    {
      data[idata] = vmeRead32(&FAp[id]->sum_data) & FA_SUM_DATA_SAMPLE_MASK;
#ifndef VXWORKS
      data[idata] = LSWAP(data[idata]);
#endif
      idata++;
    }
  idata++;

  /* Use this to clear the data ready bit (dont set back to zero) */
  vmeWrite32(&FAp[id]->sum_data,FA_SUM_DATA_ARM_HISTORY_BUFFER);

  FAUNLOCK;
  dCnt += idata;

  return dCnt;
}


const char *fa_mode_names[FA_MAX_PROC_MODE+1] = 
  {
    "NOT DEFINED", // 0
    "NOT DEFINED",
    "NOT DEFINED",
    "NOT DEFINED",
    "NOT DEFINED",
    "NOT DEFINED", // 5
    "NOT DEFINED",
    "NOT DEFINED",
    "NOT DEFINED",
    "PULSE PARAMETER",       // 9
    "RAW + PULSE PARAMETER", // 10
    "RAW"                    // 11
  };

/***************************************************************************************
   JLAB FADC Signal Distribution Card (SDC) Routines
***************************************************************************************/


/**
 *  @ingroup SDCConfig
 *  @brief Configure the Signal Distribution Card (SDC)
 *  @param id Slot number
 *  @param  cFlag  controls the configuation of the SDC
 *   -  0:  Default Mode  Internal CLK, Sync External Trigger and Sync Reset
 *   - >0:  Pass through mode
 *  @param bMask:  mask of Busy enables for the SDC - Do not Enable busy if there is no FADC
 *  @return OK if successful, otherwise ERROR.
 */
int
faSDC_Config(unsigned short cFlag, unsigned short bMask)
{

  if(FASDCp == NULL) 
    {
      logMsg("faSDC_Config: ERROR : Cannot Configure FADC Signal Board \n",0,0,0,0,0,0);
      return(ERROR);
    }
  
  /* Reset the Board */
  FASDCLOCK;
  vmeWrite16(&(FASDCp->csr),FASDC_CSR_INIT);

  if(cFlag == 0) 
    {
      /* Default - Enable Internal Clock, Sync Trigger and Sync-Reset*/
      vmeWrite16(&(FASDCp->ctrl),(FASDC_CTRL_ENABLE_SOFT_TRIG | FASDC_CTRL_ENABLE_SOFT_SRESET));
    }
  else if(cFlag==1) 
    {
      /* Pass Through - */
      vmeWrite16(&(FASDCp->ctrl),(FASDC_CTRL_NOSYNC_TRIG | FASDC_CTRL_NOSYNC_SRESET | 
				  FASDC_CTRL_ENABLE_SOFT_TRIG | FASDC_CTRL_ENABLE_SOFT_SRESET));
    }
  else 
    { 
      /* Level Translator */
      vmeWrite16(&(FASDCp->ctrl),(FASDC_CTRL_NOSYNC_TRIG | FASDC_CTRL_NOSYNC_SRESET));
    }
  
  vmeWrite16(&(FASDCp->busy_enable),bMask);
  FASDCUNLOCK;
  
  return(OK);
}

/**
 *  @ingroup SDCStatus
 *  @brief Print status of SDC to standard out
 *  @param sFlag Not used
 */
void
faSDC_Status(int sFlag)
{

  unsigned short sdc[4];


  if(FASDCp == NULL) 
    {
      printf("faSDC_Status: ERROR : No FADC SDC available \n");
      return;
    }
  
  FASDCLOCK;
  sdc[0] = vmeRead16(&(FASDCp->csr));
  sdc[1] = vmeRead16(&(FASDCp->ctrl))&FASDC_CTRL_MASK;
  sdc[2] = vmeRead16(&(FASDCp->busy_enable))&FASDC_BUSY_MASK;
  sdc[3] = vmeRead16(&(FASDCp->busy_status));
  FASDCUNLOCK;


#ifdef VXWORKS
  printf("\nSTATUS for FADC Signal Distribution Card at base address 0x%x \n",(UINT32) FASDCp);
#else
  printf("\nSTATUS for FADC Signal Distribution Card at VME (Local) base address 0x%x (0x%lx)\n",
	 (UINT32)((unsigned long)FASDCp - fadcA16Offset), (unsigned long) FASDCp);
#endif
  printf("---------------------------------------------------------------- \n");

  printf(" Board Firmware Rev/ID = 0x%02x\n",((sdc[0]&0xff00)>>8));
  printf(" Registers: \n");
  printf("   CSR         = 0x%04x     Control     = 0x%04x\n",sdc[0],sdc[1]);
  printf("   Busy Enable = 0x%04x     Busy Status = 0x%04x\n",sdc[2],sdc[3]);
  printf("\n");

  if((sdc[1]&FASDC_CTRL_CLK_EXT))
    printf(" Ref Clock : External\n");
  else
    printf(" Ref Clock : Internal\n");

  if((sdc[1]&FASDC_CTRL_ENABLE_SOFT_TRIG)) 
    {
      printf(" Software Triggers\n");
    }
  else
    {
      if((sdc[1]&FASDC_CTRL_NOSYNC_TRIG))
	printf(" External Triggers (Pass through)\n");
      else
	printf(" External Triggers (Sync with clock)\n");
    }

  if((sdc[1]&FASDC_CTRL_ENABLE_SOFT_SRESET)) 
    {
      printf(" Software Sync Reset\n");
    }
  else
    {
      if((sdc[1]&FASDC_CTRL_NOSYNC_SRESET))
	printf(" External Sync Reset (Pass through)\n");
      else
	printf(" External Sync Reset (Sync with clock)\n");
    }
  
}


/**
 *  @ingroup SDCConfig
 *  @brief Enable Triggers and/or SyncReset on the SDC
 *  @param nsync
 *    -  0: Front panel triggers and syncreset
 *    - !0: Front panel triggers only
 */
void
faSDC_Enable(int nsync)
{

  if(FASDCp == NULL) 
    {
      logMsg("faSDC_Enable: ERROR : No FADC SDC available \n",0,0,0,0,0,0);
      return;
    }
  
  FASDCLOCK;
  if(nsync != 0) /* FP triggers only */
    vmeWrite16(&(FASDCp->ctrl),FASDC_CTRL_ENABLE_SOFT_SRESET);
  else      /* both FP triggers and sync reset */
    vmeWrite16(&(FASDCp->ctrl),0);
  FASDCUNLOCK;
}

/**
 *  @ingroup SDCConfig
 *  @brief Disable Triggers and SyncReset on the SDC
 */
void
faSDC_Disable()
{

  if(FASDCp == NULL) 
    {
      logMsg("faSDC_Disable: ERROR : No FADC SDC available \n",0,0,0,0,0,0);
      return;
    }
  
  FASDCLOCK;
  vmeWrite16(&(FASDCp->ctrl),(FASDC_CTRL_ENABLE_SOFT_TRIG | FASDC_CTRL_ENABLE_SOFT_SRESET));
  FASDCUNLOCK;
}



/**
 *  @ingroup SDCConfig
 *  @brief Perform a SyncReset from the SDC
 */
void
faSDC_Sync()
{

  if(FASDCp == NULL) 
    {
      logMsg("faSDC_Sync: ERROR : No FADC SDC available \n",0,0,0,0,0,0);
      return;
    }
  
  FASDCLOCK;
  vmeWrite16(&(FASDCp->csr),FASDC_CSR_SRESET);
  FASDCUNLOCK;
}

/**
 *  @ingroup SDCConfig
 *  @brief Perform a trigger pulse from the SDC
 */
void
faSDC_Trig()
{
  if(FASDCp == NULL) 
    {
      logMsg("faSDC_Trig: ERROR : No FADC SDC available \n",0,0,0,0,0,0);
      return;
    }
  
  FASDCLOCK;
  vmeWrite16(&(FASDCp->csr),FASDC_CSR_TRIG);
  FASDCUNLOCK;
}

/**
 *  @ingroup SDCStatus
 *  @brief Return Busy status of the SDC
 *  @return 1 if busy, 0 if not, otherwise ERROR.
 */
int
faSDC_Busy()
{
  int busy=0;

  if(FASDCp == NULL) 
    {
      logMsg("faSDC_Busy: ERROR : No FADC SDC available \n",0,0,0,0,0,0);
      return -1;
    }
  
  FASDCLOCK;
  busy = vmeRead16(&(FASDCp->csr))&FASDC_CSR_BUSY;
  FASDCUNLOCK;

  return(busy);
}



/**
 *  @ingroup Config
 *  @brief Set the readout data form which allows for suppression of 
 *         repetitious data words
 *  @param id Slot number
 *  @param format Data Format
 *      -  0: Standard Format - No data words suppressed
 *      -  1: Intermediate compression - Event headers suppressed if no data
 *      -  2: Full compression - Only first event header in the block.
 *  @return OK if successful, otherwise ERROR.
 */
int
faSetDataFormat(int id, int format)
{

  unsigned int orig = 0;

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      printf("%s: ERROR : ADC in slot %d is not initialized \n",
	     __FUNCTION__,id);
      return ERROR;
    }

  if((format < 0) || (format > 2))
    {
      printf("%s: ERROR: Invalid format (%d) \n",
	     __FUNCTION__, format);
      return ERROR;
    }
  
  FALOCK;


  orig  = (vmeRead32(&FAp[id]->ctrl1) & (~FA_CTRL1_DATAFORMAT_MASK));
  vmeWrite32(&FAp[id]->ctrl1, (orig | (format << 26)) );

  printf("Write Format 0x%x \n", (orig | (format << 26)));

  FAUNLOCK;

  return OK;
}

/**
 *  @ingroup Config
 *  @brief Set the readout data form for all initialized modules.
 *  @param format Data Format
 *      -  0: Standard Format - No data words suppressed
 *      -  1: Intermediate compression - Event headers suppressed if no data
 *      -  2: Full compression - Only first event header in the block.
 */
void
faGSetDataFormat(int format)
{
  int ifadc;

  for(ifadc=0;ifadc<nfadc;ifadc++)
    faSetDataFormat(faSlot(ifadc), format);
}


/**
 *  @ingroup Config
 *  @brief Perform a reset of the DAC chip
 *  @param id Slot number
 */
void
faDACReset(int id)
{
  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faDACReset: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return;
    }

  FALOCK;
  vmeWrite32(&FAp[id]->reset, FA_RESET_DAC); 
  FAUNLOCK;
  
}

/**
 *  @ingroup Config
 *  @brief Perform a reset of the DAC chip for all initialized modules.
 */
void
faGDACReset()
{
  int ifa;

  FALOCK;
  for(ifa=0; ifa<nfadc; ifa++)
    {
      vmeWrite32(&FAp[faSlot(ifa)]->reset, FA_RESET_DAC);
    }
  FAUNLOCK;
  
}


int faReSetDAC(int id) {
  
  int ii, rval = 0;
  
  unsigned short dacRead[FA_MAX_ADC_CHANNELS];
  int  doWrite = 0;
  unsigned int lovalue=0, hivalue=0;
  unsigned int lovalue_rb=0, hivalue_rb=0;


  if(id==0) id=fadcID[0];
  
  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetDAC: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }
  
  FALOCK;
  

  printf("Read DAC for FADC250  in slot %d \n", id);
  
  for(ii = 0; ii < FA_MAX_ADC_CHANNELS; ii++){
    
    dacRead[ii] = vmeRead16(&(FAp[id]->dac[ii])) & FA_DAC_VALUE_MASK;
    
    printf("  %d ", dacRead[ii]);
  }

  printf("\n");

  printf("Reset DAC \n");
  
  vmeWrite32(&FAp[id]->reset, FA_RESET_DAC);

  printf("Load  DAC \n");
  


  for(ii = 0; ii < FA_MAX_ADC_CHANNELS; ii++) {

    if(ii%2 == 0) {

      lovalue = (dacRead[ii]   &  FA_DAC_VALUE_MASK);
      hivalue = (dacRead[ii+1] &  FA_DAC_VALUE_MASK);
      
      vmeWrite32((unsigned int *)&(FAp[id]->dac[ii]), 
                 lovalue<<16 | hivalue);

      // Alex
      taskDelay(100);


      /* Readback to check values, and write timeout error */
      lovalue_rb = (vmeRead16(&FAp[id]->dac[ii]));
      hivalue_rb = (vmeRead16(&FAp[id]->dac[ii+1]));

      if((lovalue_rb != lovalue) || (hivalue_rb != hivalue_rb))
        {
          printf("%s: ERROR: Readback of DAC Channels (%d, %d) != Write value\n",
                 __FUNCTION__,ii, ii+1);
          printf("  %2d: Read: 0x%04x %s Write: 0x%04x\n",
                 ii, lovalue_rb & FA_DAC_VALUE_MASK, 
                 (lovalue_rb & FA_DAC_WRITE_TIMEOUT_ERROR)?
                 "-Write Timeout ERROR-":
                 "                     ",
                 lovalue);
          printf("  %2d: Read: 0x%04x %s Write: 0x%04x\n",
                 ii+1, hivalue_rb & FA_DAC_VALUE_MASK,
                 (hivalue_rb & FA_DAC_WRITE_TIMEOUT_ERROR)?
                 "-Write Timeout ERROR-":
                 "                     ",
                 hivalue);
          rval=ERROR;
        }
    }
    
    lovalue = 0; 
    hivalue = 0;
  }
  
  FAUNLOCK;
  
  
  return 0;
  
}

void faPrintBaseline(int id)
{
  int ichan = 0;
  unsigned int write = 0, read = 0;
  
  if(id==0) id=fadcID[0];
  
  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faReadAllChannelSamples: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }
  
  FALOCK;

  write = vmeRead32(&FAp[id]->adc_config[0]) & 0xFFFF;

  // Enable processing
  vmeWrite32(&FAp[id]->adc_config[0], (write |  FA_ADC_CONFIG0_CHAN_READ_ENABLE) );

  // Disable processing
  vmeWrite32(&FAp[id]->adc_config[0], (write &  (~FA_ADC_CONFIG0_CHAN_READ_ENABLE)) );


  for(ichan = 0; ichan < FA_MAX_ADC_CHANNELS; ichan++) {
    
    read = vmeRead32(&FAp[id]->adc_status[2]) & FA_ADC_STATUS2_CHAN_DATA_MASK;
    
    printf("Channel =  %2d     %5d     Valid =  %d  Quality = %d  0x%x  \n", ichan,  (read &  0x3FFF), (read >> 15) & 0x1, (read >> 14) & 0x1, read );

  }    

  //  printf("faReadAllChannelSamples End readout :  adc_config[0]   0x%x \n",(write &  (~FA_ADC_CONFIG0_CHAN_READ_ENABLE)) );


  FAUNLOCK;
}


int faSetDACCheck(int id, unsigned short dvalue, unsigned short chmask)
{
  int ii, doWrite=0, rval=OK;
  unsigned int lovalue=0, hivalue=0;
  unsigned int lovalue_rb=0, hivalue_rb=0;
  
  if(id==0) id=fadcID[0];
  
  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetDAC: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }
  
  if(chmask==0) chmask = 0xffff;  /* Set All channels the same */
  
  if(dvalue>0xfff) 
    {
      logMsg("faSetDAC: ERROR : DAC value (%d) out of range (0-4095) \n",
	     dvalue,0,0,0,0,0);
      return(ERROR);
    }
  
  FALOCK;
  for(ii=0;ii<FA_MAX_ADC_CHANNELS;ii++)
    {

      if(ii%2==0)
	{
	  lovalue = (vmeRead16(&FAp[id]->dac[ii]));
	  hivalue = (vmeRead16(&FAp[id]->dac[ii+1]));

	  if((1<<ii)&chmask)
	    {
	      lovalue = dvalue&FA_DAC_VALUE_MASK;
	      doWrite=1;
	    }
	  if((1<<(ii+1))&chmask)
	    {
	      hivalue = (dvalue&FA_DAC_VALUE_MASK);
	      doWrite=1;
	    }

	  if(doWrite)
	    {
	      vmeWrite32((unsigned int *)&(FAp[id]->dac[ii]), 
			 lovalue<<16 | hivalue);
	      /* Readback to check values, and write timeout error */
	      lovalue_rb = (vmeRead16(&FAp[id]->dac[ii]));
	      hivalue_rb = (vmeRead16(&FAp[id]->dac[ii+1]));

              
              printf(" Read Back DAC 1 =  0x%x \n", lovalue_rb); 
              printf(" Read Back DAC 2 =  0x%x \n", hivalue_rb); 


	      if((lovalue_rb != lovalue) || (hivalue_rb != hivalue_rb))
		{
		  printf("%s: ERROR: Readback of DAC Channels (%d, %d) != Write value\n",
			 __FUNCTION__,ii, ii+1);
		  printf("  %2d: Read: 0x%04x %s Write: 0x%04x\n",
			 ii, lovalue_rb & FA_DAC_VALUE_MASK, 
			 (lovalue_rb & FA_DAC_WRITE_TIMEOUT_ERROR)?
			 "-Write Timeout ERROR-":
			 "                     ",
			 lovalue);
		  printf("  %2d: Read: 0x%04x %s Write: 0x%04x\n",
			 ii+1, hivalue_rb & FA_DAC_VALUE_MASK,
			 (hivalue_rb & FA_DAC_WRITE_TIMEOUT_ERROR)?
			 "-Write Timeout ERROR-":
			 "                     ",
			 hivalue);
		  rval=ERROR;
		}
	    }

	  lovalue = 0; 
	  hivalue = 0;
	  doWrite=0;
	}

    }
  FAUNLOCK;

  return(rval);
}


void faPrintPedestal(int id){
  
  unsigned int rval = 0;
  unsigned int ii;

  if(id == 0) id = fadcID[0];
  
  if((id <= 0) || (id > 21) || (FAp[id] == NULL)) {
    logMsg("faSetChannelPedestal: ERROR : ADC in slot %d is not initialized \n",id,0,0,0,0,0);
    return(ERROR);
  }
  
  printf(" Pedestal Settings for FADC in slot %d:",id);

  FALOCK;

  for(ii = 0;ii < FA_MAX_ADC_CHANNELS;ii++){
    
    rval = vmeRead32(&FAp[id]->adc_pedestal[ii]) & FA_ADC_PEDESTAL_MASK;
    if((ii % 4)==0) printf("\n");
    printf("Chan %2d: %5d   ",(ii+1), rval );
  }
  
  FAUNLOCK;

}

void faPrintBaselineAll()
{

  int ifa, id;
  int ichan = 0;
  unsigned int write = 0, read = 0;
  
  
  FALOCK;

  for (ifa=0;ifa<nfadc;ifa++) {
    id = faSlot(ifa);

    printf("\n  Board in slot  %d \nc\n",id);  

    write = vmeRead32(&FAp[id]->adc_config[0]) & 0xFFFF;
    
    // Enable processing
    vmeWrite32(&FAp[id]->adc_config[0], (write |  FA_ADC_CONFIG0_CHAN_READ_ENABLE) );
    
    // Disable processing
    vmeWrite32(&FAp[id]->adc_config[0], (write &  (~FA_ADC_CONFIG0_CHAN_READ_ENABLE)) );
    
    
    for(ichan = 0; ichan < FA_MAX_ADC_CHANNELS; ichan++) {
      
      read = vmeRead32(&FAp[id]->adc_status[2]) & FA_ADC_STATUS2_CHAN_DATA_MASK;
      
      printf("Channel =  %2d     %5d     Valid =  %d  Quality = %d  0x%x  \n", ichan,  (read &  0x3FFF), (read >> 15) & 0x1, (read >> 14) & 0x1, read );
      
    }    
    
  //  printf("faReadAllChannelSamples End readout :  adc_config[0]   0x%x \n",(write &  (~FA_ADC_CONFIG0_CHAN_READ_ENABLE)) );
    
  }

  FAUNLOCK;
}



int
faSetProcModeDef(int id)
{
  
  int pmode = 10;
  unsigned int PL = 900;
  unsigned int PTW = 50;
  unsigned int NSB = 3;
  unsigned int NSA = 15;
  unsigned int NP = 1;
  unsigned int NPED = 5;
  unsigned int MAXPED = 600;
  unsigned int NSAT = 2;


  


  int err=0;
  int imode=0, supported_modes[FA_SUPPORTED_NMODES] = {FA_SUPPORTED_MODES};
  int mode_supported=0;
  int mode_bit=0;


  printf(" NSAT   = %d \n", NSAT);
  printf(" MAXPED = %d \n", MAXPED);
  printf(" NPED   = %d \n", NPED);

  if(id==0) id=fadcID[0];

  if((id<=0) || (id>21) || (FAp[id] == NULL)) 
    {
      logMsg("faSetProcMode: ERROR : FADC in slot %d is not initialized \n",id,0,0,0,0,0);
      return(ERROR);
    }

  for(imode=0; imode<FA_SUPPORTED_NMODES; imode++)
    {
      if(pmode == supported_modes[imode])
	mode_supported=1;
    }
  if(!mode_supported)
    {
      printf("%s: ERROR: Processing Mode (%d) not supported\n",
	     __FUNCTION__,pmode);
      return ERROR;
    }
  
  /* Set Min/Max parameters if specified values are out of bounds */
  if((PL < FA_ADC_MIN_PL) || (PL > FA_ADC_MAX_PL))  
    {
      printf("%s: WARN: PL (%d) out of bounds.  ",__FUNCTION__,PL);
      PL  = (PL < FA_ADC_MIN_PL) ? FA_ADC_MIN_PL : FA_ADC_MAX_PL;
      printf("Setting to %d.\n",PL);
    }

  if((PTW < FA_ADC_MIN_PTW) || (PTW > FA_ADC_MAX_PTW)) 
    {
      printf("%s: WARN: PTW (%d) out of bounds.  ",__FUNCTION__,PTW);
      PTW = (PTW < FA_ADC_MIN_PTW) ? FA_ADC_MIN_PTW : FA_ADC_MAX_PTW;
      printf("Setting to %d.\n",PTW);
    }

  if((NSB < FA_ADC_MIN_NSB) || (NSB > FA_ADC_MAX_NSB)) 
    {
      printf("%s: WARN: NSB (%d) out of bounds.  ",__FUNCTION__,NSB);
      NSB = (NSB < FA_ADC_MIN_NSB) ? FA_ADC_MIN_NSB : FA_ADC_MAX_NSB;
      printf("Setting to %d.\n",NSB);
    }


  // Alex
  //  if((NSA < FA_ADC_MIN_NSA) || (NSA > FA_ADC_MAX_NSA)) 
  //    {
  //      printf("%s: WARN: NSA (%d) out of bounds.  ",__FUNCTION__,NSA);
  //      NSA = (NSA < FA_ADC_MIN_NSA) ? FA_ADC_MIN_NSA : FA_ADC_MAX_NSA;
  //      if(((NSB + NSA) % 2)==0) /* Make sure NSA+NSB is an odd number */
  //	NSA = (NSA==FA_ADC_MIN_NSA) ? NSA + 1 : NSA - 1;
  //      printf("Setting to %d.\n",NSA);
  //    }


  //   if( (NSB & 0x8) && (NSA <= NSB) )
  //    {
  //      printf("%s: WARN: ", __FUNCTION__);
  //    }

  
  if((NP < FA_ADC_MIN_NP) || (NP > FA_ADC_MAX_NP))
    {
      printf("%s: WARN: NP (%d) out of bounds.  ",__FUNCTION__,NP);
      NP = (NP < FA_ADC_MIN_NP) ? FA_ADC_MIN_NP : FA_ADC_MAX_NP;
      printf("Setting to %d.\n",NP);
    }

  if((NPED < FA_ADC_MIN_NPED) || (NPED > FA_ADC_MAX_NPED))
    {
      printf("%s: WARN: NPED (%d) out of bounds.  ",__FUNCTION__,NPED);
      NPED = (NPED < FA_ADC_MIN_NPED) ? FA_ADC_MIN_NPED : FA_ADC_MAX_NPED;
      printf("Setting to %d.\n",NPED);
    }

  if(NPED >= PTW)
    {
      printf("%s: WARN: NPED (%d) >= PTW (%d)  ",__FUNCTION__, NPED, PTW);
      NPED = PTW - 1;
      printf("Setting to %d.\n",NPED);
    }
  
  
  if((MAXPED < FA_ADC_MIN_MAXPED) || (MAXPED > FA_ADC_MAX_MAXPED))
    {
      printf("%s: WARN: MAXPED (%d) out of bounds.  ",__FUNCTION__,MAXPED);
      MAXPED = (MAXPED < FA_ADC_MIN_MAXPED) ? FA_ADC_MIN_MAXPED : FA_ADC_MAX_MAXPED;
      printf("Setting to %d.\n",MAXPED);
    }

  if((NSAT < FA_ADC_MIN_NSAT) || (NSAT > FA_ADC_MAX_NSAT))
    {
      printf("%s: WARN: NSAT (%d) out of bounds.  ",__FUNCTION__,NSAT);
      NSAT = (NSAT < FA_ADC_MIN_NSAT) ? FA_ADC_MIN_NSAT : FA_ADC_MAX_NSAT;
      printf("Setting to %d.\n",NSAT);
    }

  /* Consistancy check */
  // Alex
  //  if(((NSB + NSA) % 2)==0) 
  //    {
  //      err++;
  //      printf("%s: ERROR: NSB+NSA must be an odd number\n",__FUNCTION__); 
  //    }

 
  //  faSetNormalMode(id,0);
  
  //  faSetNormalMode(id,0);

  FALOCK;
  /* Disable ADC processing while writing window info */
  if(pmode == FA_ADC_PROC_MODE_PULSE_PARAM)
    mode_bit = 0;
  else if(pmode == FA_ADC_PROC_MODE_RAW_PULSE_PARAM)
    mode_bit = 1;
  // ALEX
  else if(pmode == FA_ADC_PROC_MODE_RAW)
    mode_bit = 3;
  else
    {
      printf("%s: ERROR: Unsupported mode (%d)\n",
	     __FUNCTION__, pmode);
      return ERROR;
    }

  /* Configure the mode (mode_bit), # of pulses (NP), # samples above TET (NSAT)
     keep TNSAT, if it's already been configured */
  vmeWrite32(&FAp[id]->adc_config[0],
	     (vmeRead32(&FAp[id]->adc_config[0]) & FA_ADC_CONFIG1_TNSAT_MASK) |
	     (mode_bit << 8) | ((NP-1) << 4) | ((NSAT-1) << 10) );
  /* Disable user-requested channels */
  vmeWrite32(&FAp[id]->adc_config[1], fadcChanDisable[id]);

  /* Set window parameters */
  vmeWrite32(&FAp[id]->adc_pl, PL);
  vmeWrite32(&FAp[id]->adc_ptw, PTW - 1);

  /* Set Readback NSB, NSA
     NSA */
  vmeWrite32(&FAp[id]->adc_nsb, NSB);
  vmeWrite32(&FAp[id]->adc_nsa,
	     (vmeRead32(&FAp[id]->adc_nsa) & FA_ADC_TNSA_MASK) |
	     NSA );

  /* Set Pedestal parameters */
  vmeWrite32(&FAp[id]->config7, (NPED-1)<<10 | (MAXPED));

  /* Enable ADC processing */
  vmeWrite32(&FAp[id]->adc_config[0],
	     vmeRead32(&FAp[id]->adc_config[0]) | FA_ADC_PROC_ENABLE );

  /* Set default value of trigger path threshold (TPT) */
  vmeWrite32(&FAp[id]->config3, FA_ADC_DEFAULT_TPT);
  FAUNLOCK;

  //  faSetTriggerStopCondition(id, faCalcMaxUnAckTriggers(pmode,PTW,NSA,NSB,NP));
  //  faSetTriggerBusyCondition(id, faCalcMaxUnAckTriggers(pmode,PTW,NSA,NSB,NP));

  return(OK);
}


void faResetADC(int id) {

  printf(" Reset ADC chip for FADC in slot %d \n", id);

  FALOCK;

  vmeWrite32(&FAp[id]->adc_config[2], 0x10);
  taskDelay(1);

  vmeWrite32(&FAp[id]->adc_config[2], 0);
  taskDelay(1);

  FAUNLOCK;

}

void faWriteConfig(int id, int config, unsigned int value) {


  FALOCK;

  printf(" FAp address 0x%x \n", &FAp[id]);

  if(config == 2){
    vmeWrite32(&FAp[id]->adc_config[2], value);
    taskDelay(1);
    printf(" Write 0x%x to config 2 \n", value);
  }

  if(config == 3) {
    vmeWrite32(&FAp[id]->adc_config[3], value);
    taskDelay(1);
    printf(" Write 0x%x to config 3 \n", value);
  }
  
  FAUNLOCK;

}

void faMGTReset(int id, int reset){

  printf(" Reset MGT in slot %id  reset %d\n",id, reset);

  FALOCK;

  if(reset) 
      vmeWrite32(&FAp[id]->mgt_ctrl, FA_MGT_RESET);
  else 
      vmeWrite32(&FAp[id]->mgt_ctrl,FA_RELEASE_MGT_RESET);
  FAUNLOCK;

}
