/*----------------------------------------------------------------------------*
 *  Copyright (c) 2010        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Authors: Bryan Moffit                                                   *
 *             moffit@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5660             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 * Description:
 *     Driver library for TJNAF VME 16 Channel Discriminator/Scaler using
 *      a vxWorks 5.4 or later or Linux 2.6.18 or later based Single Board
 *      Computer.
 *
 * SVN: $Rev$
 *
 *----------------------------------------------------------------------------*/

#ifdef VXWORKS
#include <vxWorks.h>
#include <vxLib.h>
#include <logLib.h>
#else
#include "jvme.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "vmeDSClib.h"

/* Global variables */
int Ndsc = 0;                                     /* Number of DSCs in Crate */
volatile struct dsc_struct *dscp[DSC_MAX_SLOTS+1]; /* pointers to DSC A24 memory map */
volatile unsigned int *dscpd[DSC_MAX_SLOTS+1];     /* pointers to DSC A32 memory map */
int dscID[DSC_MAX_BOARDS+1];                        /* array of slot numbers for DSCs */
unsigned int dscA24Offset = 0;       /* Offset between VME A24 and Local address space */
unsigned int dscA32Offset = 0;       /* Offset between VME A32 and Local address space */
unsigned int dscA32Base   = 0x08000000;
unsigned int dscAddrList[DSC_MAX_BOARDS];            /* array of a24 addresses for DSCs */
static int dscIndexedBySlotNumber = 1;  /* How the library pointers are indexed */

/* Macro to check for module initialization */
#define CHECKID(id) {							\
    if(id==0) id=dscID[0];						\
    if((id<0) || (id>21) || (dscp[id] == NULL))				\
      {									\
	logMsg("%s: ERROR: DSC in slot %d is not initialized \n",	\
	       __FUNCTION__,id,3,4,5,6);				\
	return ERROR;							\
      }									\
  }

/* Mutex to guard flexio read/writes */
pthread_mutex_t   dscMutex = PTHREAD_MUTEX_INITIALIZER;
#define DSCLOCK   if(pthread_mutex_lock(&dscMutex)<0) perror("pthread_mutex_lock");
#define DSCUNLOCK if(pthread_mutex_unlock(&dscMutex)<0) perror("pthread_mutex_unlock");

/*******************************************************************************
 *
 * vmeDSCInit - Initialize JLAB VME Discriminator/Scaler Library. 
 *
 *
 *   iFlag: 17 bit integer
 *      bit 16:  Exit before board initialization
 *             0 Initialize DSC (default behavior)
 *             1 Skip initialization (just setup register map pointers)
 *
 *      bit 17:  Use dscAddrList instead of addr and addr_inc
 *               for VME addresses.
 *             0 Initialize with addr and addr_inc
 *             1 Use dscAddrList 
 *
 *      bit 18:  Allow initialization of DSC with firmware with a version
 *               less than the required version.  For firmware updating.
 *             0 Default.  Do not allow initialization of older firmware
 *             1 Allow initialization with older firmware.
 *
 *      bit 19:  Use order of initialization as the module index.  May be
 *               useful for firmware updating.
 *             0 Use Slot ID
 *             1 Use order of initialization
 *
 * RETURNS: OK, or ERROR if the address is invalid or a board is not present.
 */

int
vmeDSCInit(unsigned int addr, unsigned int addr_inc, int ndsc, int iFlag)
{
  unsigned int laddr, laddr_inc, errFlag, fwrev;
  int res, ii, slotno;
  unsigned int boardID;
  int noBoardInit=0;
  int useList=0;
  int allowOlderFirmware=0;
  int indexByOrder=0;
  volatile struct dsc_struct *dsc;  
  unsigned int a32addr=0;

  /* Check if we are to exit when pointers are setup */
  if(iFlag&(1<<16))
    noBoardInit=1;

  /* Check if we're initializing using a list */
  if(iFlag&(1<<17))
     useList=1;

  /* Check if we're allowing for older firmware */
  if(iFlag&(1<<18))
    allowOlderFirmware=1;

  /* Check if we're allowing for older firmware */
  if(iFlag&(1<<19))
    {
      indexByOrder=1;
      dscIndexedBySlotNumber=0;
    }

  /* Check for valid address */
  if( (addr==0) || (addr>0x00FFFFFF) )
    {
      printf("%s: ERROR: Must specify a Bus (VME-based A24) address for DSC\n",
	     __FUNCTION__);
      return ERROR;
    }

  if( ((addr_inc==0) || (ndsc==0)) && useList!=1 )
    ndsc = 1; /* assume only one DSC to initialize */


  /* First obtain the A24 offsets */
#ifdef VXWORKS
  res = sysBusToLocalAdrs(0x39,(char *)addr,(char **)&laddr);
#else
  res = vmeBusToLocalAdrs(0x39,(char *)addr,(char **)&laddr);
#endif
  if (res != 0) 
    {
#ifdef VXWORKS
      printf("%s: ERROR in sysBusToLocalAdrs(0x%x,0x%x,&laddr) \n",
	     __FUNCTION__,0x39,addr);
#else
      printf("%s: ERROR in vmeBusToLocalAdrs(0x%x,0x%x,&laddr) \n",
	     __FUNCTION__,0x39,addr);
#endif
      return(ERROR);
    }
  dscA24Offset = laddr - addr;

  Ndsc = 0;
  for (ii=0;ii<ndsc;ii++) 
    {
      if(useList==1)
	{
	  laddr_inc = (dscAddrList[ii] & 0x00FFFFFF) + dscA24Offset;
	}
      else
	{
	  laddr_inc = laddr + ii*addr_inc;
	}

      dsc = (struct dsc_struct *)laddr_inc;
      
      /* Check if Board exists at that address */
#ifdef VXWORKS
      res = vxMemProbe((char *) &(dsc->boardID),0,4,(char *)&boardID);
#else
      res = vmeMemProbe((char *) &(dsc->boardID),4,(char *)&boardID);
#endif
      if(res < 0) 
	{
	  printf("%s: WARN: No addressable board at A24 Address 0x%x\n",
		 __FUNCTION__,(UINT32) dsc - dscA24Offset);
	  errFlag = 1;
	  continue;
	} 

      /* Check if this is a DSC */
      if(boardID != DSC_BOARD_ID) 
	{
	  printf("%s: ERROR: Board ID at addr=0x%x does not match: 0x%08x \n",
		 __FUNCTION__,(UINT32) dsc - dscA24Offset,boardID);
	  errFlag = 1;
	  continue;
	}

      /* Check the firwmare version */
      fwrev = vmeRead32(&dsc->firmwareRev);
      if(fwrev < DSC_SUPPORTED_FIRMWARE)
	{
	  printf("%s: ERROR: vmeDSC firmware (0x%x) at addr=0x%x not supported by this driver. \n",
		 __FUNCTION__,fwrev,(UINT32) dsc - dscA24Offset);
	  printf("  Minimum required = 0x%x\n",DSC_SUPPORTED_FIRMWARE);
	  errFlag = 1;
	  if(!allowOlderFirmware)
	    continue;
	}

      /* Get and check the module's slot number */
      slotno = vmeRead32(&dsc->Geo) & DSC_GEO_SLOTID_MASK;
      if((slotno<1) || (slotno>DSC_MAX_SLOTS))
	{
	  printf("%s: Module at addr=0x%x has an invalid slot number (%d)\n",
		 __FUNCTION__, (UINT32) dsc - dscA24Offset, slotno);
	  errFlag = 1;
	  if(!indexByOrder)
	    continue;
	}

      if(indexByOrder)
	slotno = Ndsc;

      dscID[Ndsc] = slotno;

      dscp[dscID[Ndsc]] = (struct dsc_struct*)laddr_inc;
      printf("Initialized vmeDSC ID %d at VME (USER) address 0x%x (0x%x).\n",
	     dscID[Ndsc], (UINT32) dscp[dscID[Ndsc]] - dscA24Offset, (UINT32) dscp[dscID[Ndsc]]);
      Ndsc++;
    }

  /* Calculate the A32 Offset for use in Block Transfers */
#ifdef VXWORKS
  res = sysBusToLocalAdrs(0x09,(char *)dscA32Base,(char **)&laddr);
  if (res != 0) 
    {
      printf("%s: ERROR in sysBusToLocalAdrs(0x09,0x%x,&laddr) \n",
	     __FUNCTION__,dscA32Base);
      return(ERROR);
    } 
  else 
    {
      dscA32Offset = laddr - dscA32Base;
    }
#else
  res = vmeBusToLocalAdrs(0x09,(char *)dscA32Base,(char **)&laddr);
  if (res != 0) 
    {
      printf("%s: ERROR in vmeBusToLocalAdrs(0x09,0x%x,&laddr) \n",
	     __FUNCTION__,dscA32Base);
      return(ERROR);
    } 
  else 
    {
      dscA32Offset = laddr - dscA32Base;
    }
#endif

  /* Loop through and program the A32 access addresses to the DSC's fifo */
  for(ii=0; ii<Ndsc; ii++)
    {
      if(!noBoardInit)
	{
	  a32addr = dscA32Base + ii*DSC_MAX_A32_MEM;
	}
      else
	{
	  /* Assume DSC already has an A32 programmed */
	  a32addr = (vmeRead32(&dscp[dscID[ii]]->Adr32) & DSC_ADR32_BASE_MASK)<<16;
	}
      
#ifdef VXWORKS
      res = sysBusToLocalAdrs(0x09,(char *)a32addr,(char **)&laddr);
      if (res != 0) 
	{
	  printf("%s: ERROR in sysBusToLocalAdrs(0x09,0x%x,&laddr) \n",
		 __FUNCTION__,a32addr);
	  return(ERROR);
	}
#else
      res = vmeBusToLocalAdrs(0x09,(char *)a32addr,(char **)&laddr);
      if (res != 0) 
	{
	  printf("%s: ERROR in vmeBusToLocalAdrs(0x09,0x%x,&laddr) \n",
		 __FUNCTION__,a32addr);
	  return(ERROR);
	}
#endif
      dscpd[dscID[ii]] = (unsigned int *)(laddr);  /* Set a pointer to the FIFO */

      if(!noBoardInit)
	{
	  vmeWrite32(&(dscp[dscID[ii]]->Adr32),
		     (a32addr>>16) | DSC_ADR32_ENABLE);  /* Write the register and enable */
	}
    }


  if(Ndsc==0)
    {
      printf("%s: ERROR: Unable to initialize any vmeDSC modules\n",
	     __FUNCTION__);
      return ERROR;
    }

  printf("%s: Found and configured %d vmeDSC modules\n",
	 __FUNCTION__,Ndsc);

  return OK;

}

/*******************************************************************************
 *
 * vmeDSCSlot - Convert an index into a slot number, where the index is
 *              the element of an array of vmeDSCs in the order in which they were
 *              initialized.
 *
 * RETURNS: Slot number if Successfull, otherwise ERROR.
 *
 */

int
vmeDSCSlot(unsigned int i)
{
  if(i>=Ndsc)
    {
      printf("%s: ERROR: Index (%d) >= vmeDSCs initialized (%d).\n",
	     __FUNCTION__,i,Ndsc);
      return ERROR;
    }

  return dscID[i];
}

/*******************************************************************
 *   Function : vmeDSCStatus
 *                                                    
 *   Function : Print DSC module status to standard output
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 INT32  pflag - unused
 *                                                    
 *   Returns -1 if Error, 0 if OK.                    
 *                                                    
 *******************************************************************/

int
vmeDSCStatus(UINT32 id, int pflag)
{
  UINT32 Adr32;
  UINT32 pulsewidth, chEnable, orMask, delay, testCtrl;
  UINT32 firmwareRev, boardID;
  UINT32 ScalerGateGrp1, ScalerGateGrp2;
  UINT32 readoutStart;
  CHECKID(id);

  DSCLOCK;
  Adr32       = vmeRead32(&dscp[id]->Adr32);
  pulsewidth  = vmeRead32(&dscp[id]->pulsewidth);
  chEnable    = vmeRead32(&dscp[id]->chEnable);
  orMask      = vmeRead32(&dscp[id]->orMask);
  delay       = vmeRead32(&dscp[id]->delay);
  testCtrl    = vmeRead32(&dscp[id]->testCtrl);
  firmwareRev = vmeRead32(&dscp[id]->firmwareRev);
  boardID     = vmeRead32(&dscp[id]->boardID);
  ScalerGateGrp1 = vmeRead32(&dscp[id]->ScalerGateGrp1);
  ScalerGateGrp2 = vmeRead32(&dscp[id]->ScalerGateGrp2);
  readoutStart = vmeRead32(&dscp[id]->readoutStart);
  DSCUNLOCK;

  printf("\nSTATUS for DSC in slot %d at VME (USER) base address 0x%x (0x%x)\n",
	 id,  (UINT32) dscp[id]-dscA24Offset, (UINT32) dscp[id]);
  printf("-----------------------------------------------------------------------\n");
  printf(" Board Firmware = 0x%04x  Board ID = 0x%08x\n",
	 firmwareRev&DSC_FIRMWAREREV_MASK, boardID);
  if(Adr32&DSC_ADR32_ENABLE)
    printf(" A32 Enabled at VME (Local) base 0x%08x (0x%08x)\n",
	   ((Adr32 & DSC_ADR32_BASE_MASK)<<16),(UINT32) dscpd[id]);
  else
    printf(" A32 Disabled\n");

  printf("\n");
  printf(" Pulse widths (0x%08x):\n",
	 pulsewidth);
  printf("   TDC: 0x%02x (%d ns)\n   TRG: 0x%02x (%d ns)\n",
	 pulsewidth&DSC_PULSEWIDTH_TDC_MASK,
	 pulsewidth&DSC_PULSEWIDTH_TDC_MASK,
	 (pulsewidth&DSC_PULSEWIDTH_TRG_MASK)>>16,
	 (pulsewidth&DSC_PULSEWIDTH_TRG_MASK)>>16);
  printf(" Channel Enabled Mask           = 0x%08x\n",chEnable);
  printf(" Channel Enabled OR Output Mask = 0x%08x\n",orMask);
  printf(" Input Delays (ns): (0x%08x)\n",delay);
  printf("   Group 1: 0x%02x (%d ns)\n   Group 2: 0x%02x (%d ns)\n",
	 (delay&DSC_DELAY_GRP1_MASK), 
	 8*(delay&DSC_DELAY_GRP1_MASK), 
	 ((delay&DSC_DELAY_GRP2_MASK)>>16),
	 8*((delay&DSC_DELAY_GRP2_MASK)>>16));
  if(testCtrl & DSC_TESTCTRL_FP_MASK)
    {
      printf(" Front panel Test Input ENABLED: ");
      if(testCtrl & DSC_TESTCTRL_ROUTE_IN1)
	printf(" IN1");
      if(testCtrl & DSC_TESTCTRL_ROUTE_IN2)
	printf(" IN2");
      printf("\n");
    }
  printf(" Scaler Gate Source:\n");
  printf("   Group 1: ");
  if(ScalerGateGrp1 & DSC_SCALERGATEGRP_SOURCE_MASK)
    {
      if(ScalerGateGrp1 & DSC_SCALERGATEGRP_SOURCE_IN1)
	printf(" IN1");
      if(ScalerGateGrp1 & DSC_SCALERGATEGRP_SOURCE_IN2)
	printf(" IN2");
      if(ScalerGateGrp1 & DSC_SCALERGATEGRP_SOURCE_CONSTANT)
	printf(" Constant");
      if(ScalerGateGrp1 & DSC_SCALERGATEGRP_SOURCE_PULSER)
	printf(" Pulser");
      printf("\n");
    }
  else
    printf(" OFF\n");
  printf("   Group 2: ");
  if(ScalerGateGrp2 & DSC_SCALERGATEGRP_SOURCE_MASK)
    {
      if(ScalerGateGrp2 & DSC_SCALERGATEGRP_SOURCE_IN1)
	printf(" IN1");
      if(ScalerGateGrp2 & DSC_SCALERGATEGRP_SOURCE_IN2)
	printf(" IN2");
      if(ScalerGateGrp2 & DSC_SCALERGATEGRP_SOURCE_CONSTANT)
	printf(" Constant");
      if(ScalerGateGrp2 & DSC_SCALERGATEGRP_SOURCE_PULSER)
	printf(" Pulser");
      printf("\n");
    }
  else
    printf(" OFF\n");
	 
  printf(" Event Buffer Readout Configuration (0x%08x)\n", readoutStart);
  if(readoutStart & DSC_READOUTSTART_MASK)
    {
      printf("   Group 1: ");
      if(readoutStart & DSC_READOUTSTART_TRG_GRP1)
	printf(" TRG");
      if(readoutStart & DSC_READOUTSTART_TDC_GRP1)
	printf(" TDC");
      if(readoutStart & DSC_READOUTSTART_REF_GRP1)
	printf(" REF");
      if(readoutStart & DSC_READOUTSTART_REF_GRP1)
	printf(" Latch");
      printf("\n");
      printf("   Group 2: ");
      if(readoutStart & DSC_READOUTSTART_TRG_GRP2)
	printf(" TRG");
      if(readoutStart & DSC_READOUTSTART_TDC_GRP2)
	printf(" TDC");
      if(readoutStart & DSC_READOUTSTART_REF_GRP2)
	printf(" REF");
      if(readoutStart & DSC_READOUTSTART_REF_GRP2)
	printf(" Latch");
      printf("\n");
      printf("   TrigSrc: ");
      if((readoutStart & DSC_READOUTSTART_SOURCE_MASK)==0)
	printf(" NONE");
      else
	{
	  if(readoutStart & DSC_READOUTSTART_SOURCE_IN1)
	    printf(" IN1");
	  if(readoutStart & DSC_READOUTSTART_SOURCE_IN2)
	    printf(" IN2");
	  if(readoutStart & DSC_READOUTSTART_SOURCE_SOFT)
	    printf(" Software");
	  if(readoutStart & DSC_READOUTSTART_SOURCE_PULSER_ROLLOVER)
	    printf(" Pulser Rollover");
	}
      printf("\n");
    }
  

  printf("-----------------------------------------------------------------------\n");
  printf("\n\n");

  return OK;
}

int
vmeDSCGStatus(int pflag)
{
  int idsc=0;
  for(idsc = 0; idsc < Ndsc; idsc++)
    {
      vmeDSCStatus(vmeDSCSlot(idsc),pflag);
    }
  return OK;
}

/*******************************************************************
 *  Function : vmeDSCGetFirmwareVersion
 *
 *  Function : Get the firmware versions of the FPGA
 *
 *  Parameters :   pval - print to standard out
 *                    0: Print nothing to stdout
 *                   !0: Print firmware version to stdout
 *
 *  Returns: -1 if Error, FPGA Version if OK
 *
 ******************************************************************/

int
vmeDSCGetFirmwareVersion(int id, int pflag)
{
  int rval=0;
  CHECKID(id);
  
  DSCLOCK;
  /* FPGA firmware version */
  rval = vmeRead32(&dscp[id]->firmwareRev) & 0xFFFF;
  DSCUNLOCK;

  if(pflag)
    {
      printf("%s:  Board Firmware Rev/ID = 0x%04x\n",
	     __FUNCTION__,rval);
    }

  return rval;
}

int
vmeDSCGetSerialNumber(int id, char **rSN, int pflag)
{
  char retSN[10];
  unsigned int sn=0, sm=0;
  CHECKID(id);

  DSCLOCK;
  sn = vmeRead32(&dscp[id]->SerialNum);
  sm = vmeRead32(&dscp[id]->SerialMfg);
  DSCUNLOCK;

  if(pflag)
    {
      printf("%s(%2d): SerialNum = 0x%x (%d)\n",
	     __FUNCTION__,id,sn,sn);
      printf("%s(%2d): SerialMfg = 0x%x (%d)\n",
	     __FUNCTION__,id,sm,sm);
    }
  

  if(rSN!=NULL)
    {
      if(sm==0x41434449)
	sprintf(retSN,"ACDI-%03d",sn&0xFFFF);
      else if (sm==0x54524953)
	sprintf(retSN,"TRIS-%03d",sn&0xFFFF);
      else
	sprintf(retSN,"UNKNOWN-%03d",sn&0xFFFF);
      strcpy((char *)rSN,retSN);
    }

  return sn;
}

unsigned int
vmeDSCGetMfg(int id, int pflag)
{
  unsigned int sm=0;
  CHECKID(id);

  DSCLOCK;
  sm = vmeRead32(&dscp[id]->SerialMfg);
  DSCUNLOCK;

  if(pflag)
    {
      printf("%s: SerialMfg = 0x%x (%d)\n",
	     __FUNCTION__,sm,sm);
    }
  

  return sm;
}

/*******************************************************************
 *   Function : vmeDSCSetThreshold
 *                                                    
 *   Function : Set threshold for individual channels
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT16 chan  - Channel Number
 *                 UINT16 val   - Threshold value (-1 mV units)
 *                 UINT16 type  - 1 to set TDC threshold
 *                                2 to set TRG threshold
 *                                0 to set Both TDC & TRG
 *                                                    
 *   Returns -1 if Error, 0 if OK.                    
 *                                                    
 *******************************************************************/

int
vmeDSCSetThreshold(UINT32 id, UINT16 chan, UINT16 val, UINT16 type)
{
  UINT32 oldval=0;
  CHECKID(id);

  if(chan>16)
    {
      printf("%s: ERROR: invalid channel (%d)\n",__FUNCTION__,chan);
      return ERROR;
    }

  if(val>0xfff)
    {
      printf("%s: ERROR: invalid value=%d (0x%x)\n",__FUNCTION__,val,val);
      return ERROR;
    }

  if(type>2)
    {
      printf("%s: ERROR: invalid type=%d\n",__FUNCTION__,type);
      return ERROR;
    }


  DSCLOCK;
  switch(type)
    {
    case 1:  /* TDC */
      /* Save the TRG value so it's not lost */
      oldval = vmeRead32(&dscp[id]->threshold[chan])&DSC_THRESHOLD_TRG_MASK;
      vmeWrite32(&dscp[id]->threshold[chan],
	       ((val)&DSC_THRESHOLD_TDC_MASK) | oldval);
      break;
    case 2:  /* TRG */
      /* Save the TDC value so it's not lost */
      oldval = vmeRead32(&dscp[id]->threshold[chan])&DSC_THRESHOLD_TDC_MASK;
      vmeWrite32(&dscp[id]->threshold[chan],
	       ((val<<16)&DSC_THRESHOLD_TRG_MASK) | oldval);
      break;
    case 0:
    default: /* Both */
      vmeWrite32(&dscp[id]->threshold[chan],
	       ((val)&DSC_THRESHOLD_TDC_MASK) | ((val<<16)&DSC_THRESHOLD_TRG_MASK));
    }
  DSCUNLOCK;
  
  return OK;
}

/*******************************************************************
 *   Function : vmeDSCGetThreshold
 *                                                    
 *   Function : Get threshold for individual channels
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT16 chan  - Channel Number
 *                 UINT16 type  - 1 to set TDC threshold
 *                                2 to set TRG threshold
 *                                                    
 *   Returns value of threshold (-1 mV units) or Error.
 *                                                    
 *******************************************************************/

int
vmeDSCGetThreshold(UINT32 id, UINT16 chan, UINT16 type)
{
  int rval;
  CHECKID(id);

  if(chan>16)
    {
      printf("%s: ERROR: invalid channel (%d)\n",__FUNCTION__,chan);
      return ERROR;
    }

  if(type==0 || type>2)
    {
      printf("%s: ERROR: invalid type=%d\n",__FUNCTION__,type);
      return ERROR;
    }

  DSCLOCK;
  rval = vmeRead32(&dscp[id]->threshold[chan]);
  DSCUNLOCK;
  switch(type)
    {
    case 1:  /* TDC */
      rval = rval&DSC_THRESHOLD_TDC_MASK;
      break;
    case 2:  /* TRG */
    default:
      rval = (rval&DSC_THRESHOLD_TRG_MASK)>>16;
    }
  
  return rval;
}

/*******************************************************************
 *   Function : vmeDSCSetBipolarThreshold
 *                                                    
 *   Function : Set threshold for individual channels
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT16 chan  - Channel Number
 *                 INT16 val    - Threshold value (-1 mV units)
 *                 UINT16 type  - 1 to set TDC threshold
 *                                2 to set TRG threshold
 *                                0 to set Both TDC & TRG
 *                                                    
 *   Returns -1 if Error, 0 if OK.                    
 *                                                    
 *******************************************************************/

int
vmeDSCSetBipolarThreshold(UINT32 id, UINT16 chan, INT16 val, UINT16 type)
{
  UINT32 oldval=0;
  CHECKID(id);

  if(chan > 16)
    {
      printf("%s: ERROR: invalid channel (%d)\n",__FUNCTION__,chan);
      return ERROR;
    }

  if((val > 2047) || (val < -2047))
    {
      printf("%s: ERROR: invalid value=%d (0x%x)\n",__FUNCTION__,val,val);
      return ERROR;
    }

  if(type > 2)
    {
      printf("%s: ERROR: invalid type=%d\n",__FUNCTION__,type);
      return ERROR;
    }

  val &= 0xFFF;

  DSCLOCK;
  switch(type)
    {
    case 1:  /* TDC */
      /* Save the TRG value so it's not lost */
      oldval = vmeRead32(&dscp[id]->threshold[chan])&DSC_THRESHOLD_TRG_MASK;
      vmeWrite32(&dscp[id]->threshold[chan],
	       ((val)&DSC_THRESHOLD_TDC_MASK) | oldval);
      break;
    case 2:  /* TRG */
      /* Save the TDC value so it's not lost */
      oldval = vmeRead32(&dscp[id]->threshold[chan])&DSC_THRESHOLD_TDC_MASK;
      vmeWrite32(&dscp[id]->threshold[chan],
	       ((val<<16)&DSC_THRESHOLD_TRG_MASK) | oldval);
      break;
    case 0:
    default: /* Both */
      vmeWrite32(&dscp[id]->threshold[chan],
	       ((val)&DSC_THRESHOLD_TDC_MASK) | ((val<<16)&DSC_THRESHOLD_TRG_MASK));
    }
  DSCUNLOCK;
  
  return OK;
}

/*******************************************************************
 *   Function : vmeDSCGetBipolarThreshold
 *                                                    
 *   Function : Get threshold for individual channels
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT16 chan  - Channel Number
 *                 UINT16 type  - 1 to set TDC threshold
 *                                2 to set TRG threshold
 *                                                    
 *   Returns value of threshold (-1 mV units) or Error.
 *                                                    
 *******************************************************************/

int
vmeDSCGetBipolarThreshold(UINT32 id, UINT16 chan, UINT16 type)
{
  int rval;
  CHECKID(id);

  if(chan>16)
    {
      printf("%s: ERROR: invalid channel (%d)\n",__FUNCTION__,chan);
      return ERROR;
    }

  if(type==0 || type>2)
    {
      printf("%s: ERROR: invalid type=%d\n",__FUNCTION__,type);
      return ERROR;
    }

  DSCLOCK;
  rval = vmeRead32(&dscp[id]->threshold[chan]);
  DSCUNLOCK;
  switch(type)
    {
    case 1:  /* TDC */
      rval = rval&DSC_THRESHOLD_TDC_MASK;
      break;
    case 2:  /* TRG */
    default:
      rval = (rval&DSC_THRESHOLD_TRG_MASK)>>16;
    }

  if(rval & 0x800) rval |= 0xFFFFF000;
  
  return rval;
}

/*******************************************************************
 *   Function : vmeDSCSetTRGOut
 *                                                    
 *   Function : Set the trg output width and delay characteristics
 *              for a given channel
 *                                                    
 *   Parameters :  UINT32 id      - Module slot number
 *                 UINT16 channel - Channel number
 *                 UINT16 width   - digital width
 *                 UINT16 delay   - digital delay
 *                                                    
 *   Returns -1 if Error, 0 if OK.                    
 *                                                    
 *******************************************************************/

int
vmeDSCSetTRGOut(UINT32 id, UINT16 channel, UINT16 width, UINT16 delay)
{
  CHECKID(id);

  if(channel>16)
    {
      printf("%s: Invalid channel (%d)\n",
	     __FUNCTION__, channel);
      return ERROR;
    }
  
  if(width>0xFF)
    {
      printf("%s: Invalid width (%d)\n",
	     __FUNCTION__, width);
      return ERROR;
    }
  
  if(delay>0xFF)
    {
      printf("%s: Invalid delay (%d)\n",
	     __FUNCTION__, delay);
      return ERROR;
    }
  
  DSCLOCK;
  vmeWrite32(&dscp[id]->trgOut[channel],
	     (width<<8) | (delay));
  DSCUNLOCK;

  return OK;
}

/*******************************************************************
 *   Function : vmeDSCSetTRGOutSource
 *                                                    
 *   Function : Set the trg output source masks 
 *      (threshold selection mask and reshaper bypass mask)
 *                                                    
 *   Parameters :  UINT32 id      - Module slot number
 *                 UINT16 selMask - Mask of channels to use TRG threshold
 *                 UINT16 bypMask - Mask of channels to bypass reshaper
 *                                                    
 *   Returns -1 if Error, 0 if OK.                    
 *                                                    
 *******************************************************************/

int
vmeDSCSetTRGOutSource(UINT32 id, UINT16 selMask, UINT16 bypMask)
{
  CHECKID(id);

  DSCLOCK;
  vmeWrite32(&dscp[id]->trgOutSrc, selMask | (bypMask<<16));
  DSCUNLOCK;

  return OK;
}

/*******************************************************************
 *   Function : vmeDSCSetPulseWidth
 *                                                    
 *   Function : Set the pulse width
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT16 val   - Pulse width 
 *                                units = 1ns for TDC and TRG
 *                 UINT16 type  - 1 to set TDC pulse width
 *                                2 to set TRG pulse width
 *                                3 to set both TDC and TRG 
 *                                                    
 *   Returns -1 if Error, 0 if OK.                    
 *                                                    
 *******************************************************************/

int
vmeDSCSetPulseWidth(UINT32 id, UINT16 val, UINT16 type)
{
  UINT32 oldval=0, newval=0;
  CHECKID(id);
  
  if(type==0 || type>2)
    {
      printf("%s: ERROR: invalid type (%d)\n",__FUNCTION__,type);
      return ERROR;
    }

  if(val > DSC_PULSEWIDTH_TDC_MASK)
    {
      printf("%s: ERROR: val=%d greater than maximum allowed (%d).\n",
	     __FUNCTION__,val,DSC_PULSEWIDTH_TDC_MASK);
      return ERROR;
    }

  DSCLOCK;
  switch(type)
    {
    case 1:  /* TDC */
      /* Save the TRG Pulse Width value so it's not lost */
      oldval = vmeRead32(&dscp[id]->pulsewidth) & DSC_PULSEWIDTH_TRG_MASK;
      newval = oldval | val;
      break;

    case 2:  /* TRG */
      /* Save the TDC Pulse Width value so it's not lost */
      oldval = vmeRead32(&dscp[id]->pulsewidth) & DSC_PULSEWIDTH_TDC_MASK;
      newval = (oldval) | (val<<16);
      break;

    case 3:  /* both */
      newval = (val<<16) | val;
      break;
    }

  vmeWrite32(&dscp[id]->pulsewidth, newval);
  DSCUNLOCK;

  return OK;
}

/*******************************************************************
 *   Function : vmeDSCGetPulseWidth
 *                                                    
 *   Function : Get the pulse width
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT16 type  - 1 to get TDC pulse width
 *                                2 to get TRG pulse width
 *                                                    
 *   Returns -1 if Error, Value of width if OK.
 *                                                    
 *******************************************************************/

int
vmeDSCGetPulseWidth(UINT32 id, UINT16 type)
{
  UINT32 rval;
  CHECKID(id);

  if(type==0 || type>2)
    {
      printf("%s: ERROR: invalid type (%d)\n",__FUNCTION__,type);
      return ERROR;
    }


  DSCLOCK;
  rval = vmeRead32(&dscp[id]->pulsewidth);
  DSCUNLOCK;

  switch(type)
    {
    case 1:  /* TDC */
      rval = (rval&DSC_PULSEWIDTH_TDC_MASK);
      break;

    case 2:  /* TRG */
      rval = (rval&DSC_PULSEWIDTH_TRG_MASK)>>16;
      break;

    }
  
  return rval;
}

/*******************************************************************
 *   Function : vmeDSCEnableChannel
 *              vmeDSCDisableChannel
 *                      
 *   Function : Enable/Disable a channel
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT16 chan  - Channel to enable/disable
 *                 UINT16 type  - 1 to set TDC enable
 *                                2 to set TRG enable
 *                                0 to set Both TDC & TRG enable
 *                                                    
 *   Returns -1 if Error, 0 if OK.
 *                                                    
 *******************************************************************/
int
vmeDSCEnableChannel(UINT32 id, UINT16 chan, UINT16 type)
{
  int wval=0;
  CHECKID(id);

  if(chan>32)
    {
      printf("%s: ERROR: invalid chan=%d.\n",__FUNCTION__,chan);
      return ERROR;
    }

  if(type>2)
    {
      printf("%s: ERROR: invalid type=%d\n",__FUNCTION__,type);
      return ERROR;
    }

  wval = (1<<chan);
  DSCLOCK;
  switch(type)
    {
    case 1: /* TDC */
      vmeWrite32(&dscp[id]->chEnable,
	       vmeRead32(&dscp[id]->chEnable) | (wval));
      break;
    case 2: /* TRG */
      vmeWrite32(&dscp[id]->chEnable,
	       vmeRead32(&dscp[id]->chEnable) | (wval<<16));
      break;
    case 0: /* TDC and TRG */
    default:
      vmeWrite32(&dscp[id]->chEnable,
	       vmeRead32(&dscp[id]->chEnable) | ((wval) | (wval<<16)));
    }
  DSCUNLOCK;

  return OK;
}

int
vmeDSCDisableChannel(UINT32 id, UINT16 chan, UINT16 type)
{
  int wval;
  CHECKID(id);

  if(chan>32)
    {
      printf("%s: ERROR: invalid chan=%d.\n",__FUNCTION__,chan);
      return ERROR;
    }

  if(type>2)
    {
      printf("%s: ERROR: invalid type=%d\n",__FUNCTION__,type);
      return ERROR;
    }

  wval = (1<<chan);
  DSCLOCK;
  switch(type)
    {
    case 1: /* TDC */
      vmeWrite32(&dscp[id]->chEnable,
	       vmeRead32(&dscp[id]->chEnable) & ~(wval));
      break;
    case 2: /* TRG */
      vmeWrite32(&dscp[id]->chEnable,
	       vmeRead32(&dscp[id]->chEnable) & ~(wval<<16));
      break;
    case 0: /* TDC and TRG */
    default:
      vmeWrite32(&dscp[id]->chEnable,
	       vmeRead32(&dscp[id]->chEnable) & ~((wval) | (wval<<16)));
    }
  DSCUNLOCK;

  return OK;
}

/*******************************************************************
 *   Function : vmeDSCSetChannelMask
 *                      
 *   Function : Set the mask of enabled channels
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT16 chMask- Mask of channels to enable
 *                                e.g.
 *                                 enable ch0 and 13:
 *                                  mask = 0x2001
 *                 UINT16 type  - 1 to set TDC mask
 *                                2 to set TRG mask
 *                                0 to set Both TDC & TRG mask
 *                                                    
 *   Returns -1 if Error, 0 if OK.
 *                                                    
 *******************************************************************/

int
vmeDSCSetChannelMask(UINT32 id, UINT16 chMask, UINT16 type)
{
  CHECKID(id);

  if(type>2)
    {
      printf("%s: ERROR: invalid type=%d\n",__FUNCTION__,type);
      return ERROR;
    }

  DSCLOCK;
  switch(type)
    {
    case 1: /* TDC */
      vmeWrite32(&dscp[id]->chEnable,
	       (vmeRead32(&dscp[id]->chEnable)&DSC_CHENABLE_TRG_MASK) | (chMask));
      break;
    case 2: /* TRG */
      vmeWrite32(&dscp[id]->chEnable,
	       (vmeRead32(&dscp[id]->chEnable)&DSC_CHENABLE_TDC_MASK) | (chMask<<16));
      break;
    case 0: /* TDC and TRG */
    default:
      vmeWrite32(&dscp[id]->chEnable,
	       (chMask) | (chMask<<16));
    }
  DSCUNLOCK;

  return OK;
}

/*******************************************************************
 *   Function : vmeDSCGetChannelMask
 *                      
 *   Function : Get the mask of enabled channels
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT16 type  - 1 to get TDC mask
 *                                2 to get TRG mask
 *                                0 to get Both TDC & TRG mask
 *                                                    
 *   Returns -1 if Error, Enabled channel mask if OK.
 *                                                    
 *******************************************************************/
int
vmeDSCGetChannelMask(UINT32 id, UINT16 type)
{
  int rval;
  CHECKID(id);

  if(type>2)
    {
      printf("%s: ERROR: invalid type=%d\n",__FUNCTION__,type);
      return ERROR;
    }

  DSCLOCK;
  rval = (int)vmeRead32(&dscp[id]->chEnable);
  DSCUNLOCK;
  switch(type)
    {
    case 1: /* TDC */
      rval &= DSC_CHENABLE_TDC_MASK;
      break;
    case 2: /* TRG */
      rval = (rval&DSC_CHENABLE_TRG_MASK)>>16;
      break;
    case 0: /* TDC and TRG */
      break;
    }

  return rval;
}

/*******************************************************************
 *   Function : vmeDSCEnableChannelOR
 *              vmeDSCDisableChannelOR
 *                      
 *   Function : Enable/Disable a channel contributing to 
 *              the OR Output
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT16 chan  - Channel to enable/disable
 *                 UINT16 type  - 1 to set TDC enable
 *                                2 to set TRG enable
 *                                0 to set Both TDC & TRG
 *                                                    
 *   Returns -1 if Error, 0 if OK.
 *                                                    
 *******************************************************************/
int
vmeDSCEnableChannelOR(UINT32 id, UINT16 chan, UINT16 type)
{
  int wval=0;
  CHECKID(id);

  if(chan>32)
    {
      printf("%s: ERROR: invalid chan=%d.\n",__FUNCTION__,chan);
      return ERROR;
    }

  if(type>2)
    {
      printf("%s: ERROR: invalid type=%d\n",__FUNCTION__,type);
      return ERROR;
    }

  wval = (1<<chan);
  DSCLOCK;
  switch(type)
    {
    case 1: /* TDC */
      vmeWrite32(&dscp[id]->orMask,
	       vmeRead32(&dscp[id]->orMask) | (wval));
      break;
    case 2: /* TRG */
      vmeWrite32(&dscp[id]->orMask,
	       vmeRead32(&dscp[id]->orMask) | (wval<<16));
      break;
    case 0: /* TDC and TRG */
    default:
      vmeWrite32(&dscp[id]->orMask,
	       vmeRead32(&dscp[id]->orMask) | ((wval) | (wval<<16)));
    }
  DSCUNLOCK;

  return OK;
}

int
vmeDSCDisableChannelOR(UINT32 id, UINT16 chan, UINT16 type)
{
  int wval;
  CHECKID(id);

  if(chan>32)
    {
      printf("%s: ERROR: invalid chan=%d.\n",__FUNCTION__,chan);
      return ERROR;
    }

  if(type>2)
    {
      printf("%s: ERROR: invalid type=%d\n",__FUNCTION__,type);
      return ERROR;
    }

  wval = (1<<chan);
  DSCLOCK;
  switch(type)
    {
    case 1: /* TDC */
      vmeWrite32(&dscp[id]->orMask,
	       vmeRead32(&dscp[id]->orMask) & ~(wval));
      break;
    case 2: /* TRG */
      vmeWrite32(&dscp[id]->orMask,
	       vmeRead32(&dscp[id]->orMask) & ~(wval<<16));
      break;
    case 0: /* TDC and TRG */
    default:
      vmeWrite32(&dscp[id]->orMask,
	       vmeRead32(&dscp[id]->orMask) & ~((wval) | (wval<<16)));
    }
  DSCUNLOCK;

  return OK;
}


/*******************************************************************
 *   Function : vmeDSCSetChannelORMask
 *                      
 *   Function : Set the mask of enabled channels contributing
 *              to the OR Ouput
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT16 chMask- Mask of channels to enable
 *                                e.g.
 *                                 enable ch0 and 13:
 *                                  mask = 0x2001
 *                 UINT16 type  - 1 to set TDC OR mask
 *                                2 to set TRG OR mask
 *                                0 to set Both TDC & TRG OR mask
 *                                                    
 *   Returns -1 if Error, 0 if OK.
 *                                                    
 *******************************************************************/

int
vmeDSCSetChannelORMask(UINT32 id, UINT16 chMask, UINT16 type)
{
  CHECKID(id);

  if(type>2)
    {
      printf("%s: ERROR: invalid type=%d\n",__FUNCTION__,type);
      return ERROR;
    }

  DSCLOCK;
  switch(type)
    {
    case 1: /* TDC */
      vmeWrite32(&dscp[id]->orMask,
	       (vmeRead32(&dscp[id]->orMask)&DSC_ORMASK_TRG_MASK) | (chMask));
      break;
    case 2: /* TRG */
      vmeWrite32(&dscp[id]->orMask,
	       (vmeRead32(&dscp[id]->orMask)&DSC_ORMASK_TDC_MASK) | (chMask<<16));
      break;
    case 0: /* TDC and TRG */
    default:
      vmeWrite32(&dscp[id]->orMask,
	       ((chMask) | (chMask<<16)));
    }
  DSCUNLOCK;

  return OK;
}

/*******************************************************************
 *   Function : vmeDSCGetChannelORMask
 *                      
 *   Function : Get the mask of enabled channels contributing to
 *              the OR Output
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT16 type  - 1 to get TDC OR mask
 *                                2 to get TRG OR mask
 *                                0 to get Both TDC & TRG OR mask
 *                                                    
 *   Returns -1 if Error, Enabled channel mask if OK.
 *                                                    
 *******************************************************************/
int
vmeDSCGetChannelORMask(UINT32 id, UINT16 type)
{
  int rval;
  CHECKID(id);

  if(type>2)
    {
      printf("%s: ERROR: invalid type=%d\n",__FUNCTION__,type);
      return ERROR;
    }

  DSCLOCK;
  rval = (int)vmeRead32(&dscp[id]->orMask);
  DSCUNLOCK;
  switch(type)
    {
    case 1: /* TDC */
      rval &= DSC_ORMASK_TDC_MASK;
      break;
    case 2: /* TRG */
      rval = (rval&DSC_ORMASK_TRG_MASK)>>16;
      break;
    case 0: /* TDC and TRG */
      break;
    }

  return rval;
}

/*******************************************************************
 *   Function : vmeDSCSetDelay
 *                                                    
 *   Function : Set the input delays to the specified scaler group
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT16 val   - Delay
 *                                units = 8ns
 *                 UINT16 group - 1 to set grp1
 *                                2 to set grp2
 *                                3 to set both
 *                                                    
 *   Returns -1 if Error, 0 if OK.                    
 *                                                    
 *******************************************************************/

int
vmeDSCSetDelay(UINT32 id, UINT16 val, UINT16 group)
{
  UINT32 oldval=0, newval=0;
  CHECKID(id);
  
  if(group==0 || group>3)
    {
      printf("%s: ERROR: invalid group (%d)\n",__FUNCTION__,group);
      return ERROR;
    }

  if(val > DSC_DELAY_GRP1_MASK)
    {
      printf("%s: ERROR: val=%d greater than maximum allowed (%d).\n",
	     __FUNCTION__,val,DSC_DELAY_GRP1_MASK);
      return ERROR;
    }

  DSCLOCK;
  switch(group)
    {
    case 1:
      /* Save the group 2 delay value so it's not lost */
      oldval = vmeRead32(&dscp[id]->delay)&DSC_DELAY_GRP2_MASK;
      newval = val | oldval;
      vmeWrite32(&dscp[id]->delay,
	       (val) | oldval);
      break;
    case 2:
      /* Save the group 1 delay value so it's not lost */
      oldval = vmeRead32(&dscp[id]->delay)&DSC_DELAY_GRP1_MASK;
      newval = (val<<16) | oldval;
      break;
      
    case 3:
      newval = (val<<16) | val;
    }
  vmeWrite32(&dscp[id]->delay, newval);
  DSCUNLOCK;

  return OK;
}

/*******************************************************************
 *   Function : vmeDSCGetDelay
 *                                                    
 *   Function : Get the delay to the scaler group
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT16 group - 1 for group 1
 *                                2 for group 2
 *                                                    
 *   Returns -1 if Error, Value of delay if OK.
 *                                                    
 *******************************************************************/

int
vmeDSCGetDelay(UINT32 id, UINT16 group)
{
  UINT32 rval;
  CHECKID(id);

  if(group==0 || group>2)
    {
      printf("%s: ERROR: invalid group (%d)\n",__FUNCTION__,group);
      return ERROR;
    }


  DSCLOCK;
  rval = vmeRead32(&dscp[id]->delay);
  DSCUNLOCK;
  switch(group)
    {
    case 1:
      rval = rval&DSC_DELAY_GRP1_MASK;
      break;
    case 2:
    default:
      rval = (rval&DSC_DELAY_GRP2_MASK)>>16;
      break;
    }
  
  return rval;
}

/*******************************************************************
 *   Function : vmeDSCSetTestInput
 *                      
 *   Function : Enable/Disable front panel test input
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT32 flag  - 0 to disable all front panel inputs
 *                                1 to enable IN1 as test input
 *                                2 to enable IN2 as test input
 *                                3 to enable IN1 and IN2 as test inputs
 *                                                    
 *   Returns -1 if Error, 0 if OK.
 *                                                    
 *******************************************************************/

int
vmeDSCSetTestInput(UINT32 id, UINT32 flag)
{
  unsigned int wval=0;
  CHECKID(id);  

  DSCLOCK;
  switch(flag)
    {
    case 0: /* disable inputs */
      wval = 0;
      break;

    case 1: /* IN1 */
      wval = DSC_TESTCTRL_ROUTE_IN1;
      break;

    case 2: /* IN2 */
      wval = DSC_TESTCTRL_ROUTE_IN2;
      break;

    case 3:
      wval = DSC_TESTCTRL_ROUTE_IN1 | DSC_TESTCTRL_ROUTE_IN2;
      break;
    }
  vmeWrite32(&dscp[id]->testCtrl,wval);

  DSCUNLOCK;

  return OK;
}

/*******************************************************************
 *   Function : vmeDSCTestPulse
 *                      
 *   Description : Send test pulses to discriminator channels
 *                                                    
 *   Parameters :  UINT32 id      - Module slot number
 *                 UINT32 npulses - number of pulses to send
 *                                                    
 *   Returns -1 if Error, 0 if OK.
 *                                                    
 *******************************************************************/

int
vmeDSCTestPulse(UINT32 id, UINT32 npulses)
{
  UINT32 i=0, fpinputs=0;
  CHECKID(id);  

  DSCLOCK;
  /* First save the value of the fp input sources */
  fpinputs = vmeRead32(&dscp[id]->testCtrl);

  for(i=0; i<npulses; i++)
    vmeWrite32(&dscp[id]->testCtrl,fpinputs | DSC_TESTCTRL_SOFT_PULSE);

  DSCUNLOCK;

  return OK;
}



/*******************************************************************
 *   Function : vmeDSCLatchScalers
 *                      
 *   Function : Latch the scalers for readout.
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT32 group - 1 to latch the Grp1 scalers
 *                                2 to latch the Grp2 scalers
 *                                                    
 *   Returns -1 if Error, 0 if OK.
 *                                                    
 *******************************************************************/

int
vmeDSCLatchScalers(UINT32 id, UINT16 group)
{
  CHECKID(id);

  if((group<=0) || (group>2))
    {
      printf("%s: ERROR: invalid group (%d)\n",__FUNCTION__,group);
      return ERROR;
    }

  DSCLOCK;
  switch(group)
    {
    case 1: /* Grp1 scalers */
      vmeWrite32(&dscp[id]->ScalerLatchGrp1,1);
      break;
    case 2: /* Grp2 scalers */
      vmeWrite32(&dscp[id]->ScalerLatchGrp2,1);
    }
  DSCUNLOCK;

  return OK;
}

/*******************************************************************
 *   Function : vmeDSCSetGateSource
 *                      
 *   Function : Set the gate source(s) for the specified group.
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT32 srcMask - Mask of sources to enable:
 *                         bit 0 - Front Panel IN1
 *                         bit 1 - Front Panel IN2
 *                         bit 2 - Constant
 *                         bit 3 - Pulser output
 *                                                    
 *   Returns -1 if Error, 0 if OK.
 *                                                    
 *******************************************************************/

int
vmeDSCSetGateSource(UINT32 id, UINT16 group, UINT32 srcMask)
{
  CHECKID(id);

  if((group<=0) || (group>2))
    {
      printf("%s: ERROR: invalid group (%d)\n",__FUNCTION__,group);
      return ERROR;
    }

  if(srcMask>0xF)
    {
      printf("%s: ERROR: Invalid srcMask (0x%x)\n",
	     __FUNCTION__,srcMask);
      return ERROR;
    }

  DSCLOCK;
  switch(group)
    {
    case 1: /* Grp1 scalers */
      vmeWrite32(&dscp[id]->ScalerGateGrp1,srcMask);
      break;
    case 2: /* Grp2 scalers */
      vmeWrite32(&dscp[id]->ScalerGateGrp2,srcMask);

    }
  DSCUNLOCK;

  return OK;
  
}

/*******************************************************************
 *   Function : vmeDSCGetAdr32
 *                      
 *   Function : Get the A32 Base of the module.
 *                                                    
 *   Parameters :  UINT32 id      - Module slot number
 *                                                    
 *   Returns -1 if Error, A32 Address if OK.
 *                                                    
 *******************************************************************/

UINT32
vmeDSCGetAdr32(int id)
{
  unsigned int rval=0;
  CHECKID(id);
  
  DSCLOCK;
  rval = (vmeRead32(&dscp[id]->Adr32) & DSC_ADR32_BASE_MASK)<<16;
  DSCUNLOCK;

  return rval;
}


/*******************************************************************
 *   Function : vmeDSCSetAdr32
 *                      
 *   Function : Set the A32 Base of the module.
 *                                                    
 *   Parameters :  UINT32 id      - Module slot number
 *                 UINT32 a32base - A32 base to set
 *                 UINT16 enable  - Whether or not to enable when
 *                                  setting the base.
 *                                                    
 *   Returns -1 if Error, 0 if OK.
 *                                                    
 *******************************************************************/

int
vmeDSCSetAdr32(UINT32 id, UINT32 a32base, UINT16 enable)
{
  UINT32 a32addr=0, a32base_set=0, laddr=0;
  int res=0;
  
  CHECKID(id);

  a32addr = (a32base>>16) & DSC_ADR32_BASE_MASK;
  if(enable)
    a32base_set = a32addr | DSC_ADR32_ENABLE;

  DSCLOCK;
  vmeWrite32(&dscp[id]->Adr32, a32base_set);

#ifdef VXWORKS
  res = sysBusToLocalAdrs(0x09,(char *)a32addr,(char **)&laddr);
  if (res != 0) 
    {
      printf("%s: ERROR in sysBusToLocalAdrs(0x09,0x%x,&laddr) \n",
	     __FUNCTION__,a32addr);
      dscpd[id] = NULL;
      DSCUNLOCK;
      return(ERROR);
    }
#else
  res = vmeBusToLocalAdrs(0x09,(char *)a32addr,(char **)&laddr);
  if (res != 0) 
    {
      printf("%s: ERROR in vmeBusToLocalAdrs(0x09,0x%x,&laddr) \n",
	     __FUNCTION__,a32addr);
      dscpd[id] = NULL;
      DSCUNLOCK;
      return(ERROR);
    }
#endif
  dscpd[id] = (unsigned int *)(laddr);  /* Set a pointer to the FIFO */
  DSCUNLOCK;

  return OK;
}

/*******************************************************************
 *   Function : vmeDSCAdr32Enable / vmeDSCAdr32Disable
 *                      
 *   Function : Enable / Disable A32 addressing for readout
 *                                                    
 *   Parameters :  UINT32 id      - Module slot number
 *                                                    
 *   Returns -1 if Error, 0 if OK.
 *                                                    
 *******************************************************************/

int
vmeDSCAdr32Enable(UINT32 id)
{
  CHECKID(id);

  DSCLOCK;
  vmeWrite32(&dscp[id]->Adr32,
	     vmeRead32(&dscp[id]->Adr32) | DSC_ADR32_ENABLE);
  DSCUNLOCK;
  
  return OK;
}

int
vmeDSCAdr32Disable(UINT32 id)
{
  CHECKID(id);

  DSCLOCK;
  vmeWrite32(&dscp[id]->Adr32,
	     vmeRead32(&dscp[id]->Adr32) & ~DSC_ADR32_ENABLE);
  DSCUNLOCK;

  return OK;
}

/*******************************************************************
 *   Function : vmeDSCClear
 *                      
 *   Function : Clear the event buffer of all stored events.
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *
 *   Returns -1 if Error, OK if successful
 *                                                    
 *******************************************************************/

int
vmeDSCClear(UINT32 id)
{
  CHECKID(id);

  DSCLOCK;
  vmeWrite32(&dscp[id]->readoutClear,DSC_READOUTCLEAR_CLEAR);
  DSCUNLOCK;

  return OK;
}

/*******************************************************************
 *   Function : vmeDSCDReady
 *                      
 *   Function : Return the number of events ready for readout
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *
 *   Returns -1 if Error, number of events ready for readout 
 *    if successful
 *                                                    
 *******************************************************************/

int
vmeDSCDReady(UINT32 id)
{
  int rval=0;

  CHECKID(id);

  DSCLOCK;
  rval = (vmeRead32(&dscp[id]->readoutCfg) & DSC_READOUTCFG_EVENTS_READY_MASK)>>24;
  DSCUNLOCK;

  return rval;
}

/*******************************************************************
 *   Function : vmeDSCReadoutConfig
 *                      
 *   Function : Configure the scalers, latch behavior, and trigger
 *              source for the event building.
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT32 rconf - Bits indicating the scalers
 *                                and latches to use:
 *                          Bit 0 : TRG Group 1
 *                          Bit 1 : TDC Group 1
 *                          Bit 2 : TRG Group 2
 *                          Bit 3 : TDC Group 2
 *                          Bit 4 : Reference Group 1
 *                          Bit 5 : Reference Group 2
 *                          Bit 6 : Latch Group 2
 *                          Bit 7 : Latch Group 1
 *                 UINT32 rsrc  - Bits to enable trigger source
 *                          Bit 0 : Front Panel IN1
 *                          Bit 1 : Front Panel IN2
 *                          Bit 2 : Software
 *                          Bit 3 : Internal Pulser Roll-Over
 *
 *   Returns -1 if Error, OK if successful
 *                                                    
 *******************************************************************/

int
vmeDSCReadoutConfig(UINT32 id, UINT32 rconf, UINT32 rsrc)
{
  CHECKID(id);

  if(rconf > DSC_READOUT_MASK)
    {
      printf("%s: ERROR: Invalid Readout Configuration (0x%x)\n",
	     __FUNCTION__,rconf);
      return ERROR;
    }
  if(rsrc > DSC_READOUT_TRIGSRC_MASK)
    {
      printf("%s: ERROR: Invalid Readout Trigger Source (0x%x)\n",
	     __FUNCTION__,rsrc);
      return ERROR;
    }

  DSCLOCK;
  vmeWrite32(&dscp[id]->readoutStart, rconf | (rsrc<<16));
  DSCUNLOCK;

  return OK;
}

/*******************************************************************
 *   Function : vmeDSCSoftTrigger
 *                      
 *   Function : Trigger an event build via software trigger
 *              Calling this routine will also enable
 *              the software trigger source, if it was not
 *              already enabled.
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *
 *   Returns -1 if Error, OK if successful
 *                                                    
 *******************************************************************/

int
vmeDSCSoftTrigger(UINT32 id)
{
  CHECKID(id);

  DSCLOCK;
  vmeWrite32(&dscp[id]->readoutStart, 
	     vmeRead32(&dscp[id]->readoutStart) | 
	     DSC_READOUTSTART_SOURCE_SOFT |
	     DSC_READOUTSTART_SOFT_TRIG);
  DSCUNLOCK;

  return OK;
}

/*******************************************************************
 *   Function : vmeDSCReadBlock
 *                      
 *   Function : Read values from scalers.  Perform latch (before read)
 *              if requested. Event Builder configuration must
 *              be made prior to calling this routine
 *              (vmeDSCReadoutConfig(...)).
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT32 *data - local memory address to place data
 *                 UINT32 nwrds - Max number of words to transfer
 *                 UINT32 rmode - Readout mode
 *                                 0 - programmed I/O
 *                                 1 - Block transfer
 *
 *   Returns -1 if Error, Number of words transferred if OK.
 *                                                    
 *******************************************************************/

int
vmeDSCReadBlock(UINT32 id, volatile UINT32 *data, int nwrds, int rmode)
{
/*   int dCnt=0, ichan; */
  int dummy=0, retVal, xferCount;
  volatile unsigned int *laddr;
  unsigned int vmeAdr;

  CHECKID(id);

  if(data==NULL) 
    {
      logMsg("%s: ERROR: Invalid Destination address\n",__FUNCTION__,0,0,0,0,0);
      return(ERROR);
    }

  DSCLOCK;
      
  if(rmode==0)
    { /* Programmed I/O */
      logMsg("%s: ERROR: Mode (%d) not supported\n",__FUNCTION__,rmode,3,4,5,6);
      DSCUNLOCK;
    }
  else if (rmode==1)
    { /* Single Module Block Transfer */
      /* Assume that the DMA programming is already setup. */

      /* Check for 8 byte boundary for address - insert dummy word */
      if((unsigned long) (data)&0x7) 
	{
#ifdef VXWORKS
	  *data = DSC_DATA_FILLER;
#else
	  *data = LSWAP(DSC_DATA_FILLER);
#endif
	  dummy = 1;
	  laddr = (data + 1);
	} 
      else 
	{
	  dummy = 0;
	  laddr = data;
	}
      
      vmeAdr = ((unsigned int)(dscpd[id]) - dscA32Offset);
#ifdef VXWORKS
      retVal = sysVmeDmaSend((UINT32)laddr, vmeAdr, (nwrds<<2), 0);
#else
      retVal = vmeDmaSend((UINT32)laddr, vmeAdr, (nwrds<<2));
#endif
      if(retVal != 0) 
	{
	  logMsg("\n%s: ERROR in DMA transfer Initialization 0x%x\n",__FUNCTION__,retVal,0,0,0,0);
	  DSCUNLOCK;
	  return(retVal);
	}

      /* Wait until Done or Error */
#ifdef VXWORKS
      retVal = sysVmeDmaDone(10000,1);
#else
      retVal = vmeDmaDone();
#endif

      if(retVal > 0)
	{
#ifdef VXWORKS
	  xferCount = (nwrds - (retVal>>2) + dummy); /* Number of longwords transfered */
#else
	  xferCount = ((retVal>>2) + dummy); /* Number of longwords transfered */
#endif
	  DSCUNLOCK;
	  return(xferCount);
	}
      else if (retVal == 0) 
	{
#ifdef VXWORKS
	  logMsg("\n%s: WARN: DMA transfer terminated by word count (nwrds = %d)\n",
		 __FUNCTION__,nwrds,0,0,0,0,0);
#else
	  logMsg("\n%s: WARN: DMA transfer returned zero word count (nwrds = %d)\n",
		 __FUNCTION__,nwrds,0,0,0,0,0);
#endif
	  DSCUNLOCK;
	  return(retVal);
	}
      else 
	{  /* Error in DMA */
#ifdef VXWORKS
	  logMsg("\n%s: ERROR: sysVmeDmaDone returned an Error\n",
		 __FUNCTION__,0,0,0,0,0);
#else
	  logMsg("\n%s: ERROR: vmeDmaDone returned an Error\n",
		 __FUNCTION__,0,0,0,0,0);
#endif
	  DSCUNLOCK;
	  return(retVal>>2);
	  
	}

    }
  else
    {
      logMsg("%s: ERROR: Unsupported mode (%d)\n",__FUNCTION__,rmode);
    }

  return OK;

}

/*******************************************************************
 *   Function : vmeDSCReadScalers
 *                      
 *   Function : Read values from scalers.  Perform latch (before read)
 *              if requested.
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT32 *data - local memory address to place data
 *                 UINT32 nwrds - Max number of words to transfer
 *                 UINT32 rflag - Readout flag
 *                                 bit 0 - TRG Grp1 scalers
 *                                 bit 1 - TDC Grp1 scalers
 *                                 bit 2 - TRG Grp2 scalers
 *                                 bit 3 - TDC Grp2 scalers
 *                                 bit 4 - Reference Grp1 Scaler
 *                                 bit 5 - Reference Grp2 scaler
 *                                 bit 6 - Latch Grp2 before read
 *                                 bit 7 - Latch Grp1 before read
 *                 UINT32 rmode - Readout mode
 *                                 0 - programmed I/O
 *                                 1 - Block transfer
 *
 *   * 32bit words in *data will be written as they are received from
 *     the OS, with no attention paid to "endian-ness".  
 *     E.g. in vxWorks (PPC) - big endian
 *          in Linux (Intel) - little endian
 *                                                    
 *   Returns -1 if Error, Number of words transferred if OK.
 *                                                    
 *******************************************************************/

int
vmeDSCReadScalers(UINT32 id, volatile UINT32 *data, int nwrds, int rflag, int rmode)
{
/*   int dCnt=0, ichan; */
#ifndef NODMA
  int dummy=0, retVal, xferCount;
  volatile unsigned int *laddr;
  unsigned int vmeAdr;
#endif

  CHECKID(id);

  if(data==NULL) 
    {
      logMsg("%s: ERROR: Invalid Destination address\n",__FUNCTION__,0,0,0,0,0);
      return(ERROR);
    }

  if(rflag>DSC_READOUTSTART_MASK)
    {
      logMsg("%s: ERROR: Invalid Readout Flag (0x%x)\n",__FUNCTION__,rflag,3,4,5,6);
    }

  if(rmode==0)
    { /* Programmed I/O */
#ifdef OLDREV
  
      /* Latch, if requested */
      if(rflag & DSC_LATCH_GRP1_BEFORE_READOUT)
	{
	  vmeDSCLatchScalers(id,1);
	}
      if(rflag & DSC_LATCH_GRP2_BEFORE_READOUT)
	{
	  vmeDSCLatchScalers(id,0);
	}

      /* Insert DSC header word */
#ifdef VXWORKS
      data[dCnt] = DSC_SCT_HEADER | (id<<8) | rflag;
#else
      data[dCnt] = LSWAP(DSC_SCT_HEADER | (id<<8) | rflag);
#endif
      dCnt++;

      DSCLOCK;
      /* Readout each requested scaler channels */
      if(rflag & DSC_READOUT_TRG_GRP1)
	{
	  for(ichan=0; ichan<16; ichan++)
	    {
	      if(dCnt==nwrds)
		{
		  logMsg("%s: ERROR: More data than what was requested (nwrds = %d)",
			 __FUNCTION__,nwrds,3,4,5,6);
		  DSCUNLOCK;
		  return dCnt;
		}
	      data[dCnt] = dscp[id]->TrgScalerGrp1[ichan];
	      dCnt++;
	    }
	}
      if(rflag & DSC_READOUT_TDC_GRP1)
	{
	  for(ichan=0; ichan<16; ichan++)
	    {
	      if(dCnt==nwrds)
		{
		  logMsg("%s: ERROR: More data than what was requested (nwrds = %d)",
			 __FUNCTION__,nwrds,3,4,5,6);
		  DSCUNLOCK;
		  return dCnt;
		}
	      data[dCnt] = dscp[id]->TdcScalerGrp1[ichan];
	      dCnt++;
	    }
	}

      if(rflag & DSC_READOUT_TRG_GRP2)
	{
	  for(ichan=0; ichan<16; ichan++)
	    {
	      if(dCnt==nwrds)
		{
		  logMsg("%s: ERROR: More data than what was requested (nwrds = %d)",
			 __FUNCTION__,nwrds,3,4,5,6);
		  DSCUNLOCK;
		  return dCnt;
		}
	      data[dCnt] = dscp[id]->trgGrp2Scaler[ichan];
	      dCnt++;
	    }
	}

      if(rflag & DSC_READOUT_TDC_GRP2)
	{
	  for(ichan=0; ichan<16; ichan++)
	    {
	      if(dCnt==nwrds)
		{
		  logMsg("%s: ERROR: More data than what was requested (nwrds = %d)",
			 __FUNCTION__,nwrds,3,4,5,6);
		  DSCUNLOCK;
		  return dCnt;
		}
	      data[dCnt] = dscp[id]->tdcGrp2Scaler[ichan];
	      dCnt++;
	    }
	}

      if(rflag & DSC_READOUT_REF_GRP1)
	{
	  if(dCnt==nwrds)
	    {
	      logMsg("%s: ERROR: More data than what was requested (nwrds = %d)",
		     __FUNCTION__,nwrds,3,4,5,6);
	      DSCUNLOCK;
	      return dCnt;
	    }
	  data[dCnt] = dscp[id]->refGrp1Scaler;
	  dCnt++;
	}

      if(rflag & DSC_READOUT_REF_GRP2)
	{
	  if(dCnt==nwrds)
	    {
	      logMsg("%s: ERROR: More data than what was requested (nwrds = %d)",
		     __FUNCTION__,nwrds,3,4,5,6);
	      DSCUNLOCK;
	      return dCnt;
	    }
	  data[dCnt] = dscp[id]->refGrp2Scaler;
	  dCnt++;
	}

      DSCUNLOCK;

      return dCnt;
#endif /* OLDREV */
    }
  else if (rmode==1)
    { /* Single Module Block Transfer */
#ifdef NODMA
      // FIXME: Not supported until A32 is software configured 
      logMsg("%s: ERROR: Unsupported mode (%d)\n",__FUNCTION__,rmode);
#else /* NODMA */
      /* Fill the output FIFO with the requested data */
      DSCLOCK;
      rflag |= DSC_READOUTSTART_SOURCE_SOFT;
      vmeWrite32(&dscp[id]->readoutStart,rflag);
      rflag = DSC_READOUTSTART_SOFT_TRIG;
      vmeWrite32(&dscp[id]->readoutStart,rflag);
      int iwait=0, ready=0;
      while(!ready)
	{
	  if(iwait>1000) break;
	  ready = (vmeRead32(&dscp[id]->readoutCfg) &DSC_READOUTCFG_EVENTS_READY_MASK)>>24;
	  iwait++;
	}
      if(ready==0)
	{
	  printf("%s(%2d): data not ready... \n",__FUNCTION__,id);
	  DSCUNLOCK;
	  return -1;
	}
      else
	printf("%s(%2d): ready... \n",__FUNCTION__,id);
	

      /* Assume that the DMA programming is already setup. */

      /* Check for 8 byte boundary for address - insert dummy word */
      if((unsigned long) (data)&0x7) 
	{
#ifdef VXWORKS
	  *data = DSC_DATA_FILLER;
#else
	  *data = LSWAP(DSC_DATA_FILLER);
#endif
	  dummy = 1;
	  laddr = (data + 1);
	} 
      else 
	{
	  dummy = 0;
	  laddr = data;
	}
      
      vmeAdr = ((unsigned int)(dscpd[id]) - dscA32Offset);
      printf("%s: vmeAdr = 0x%08x\n",__FUNCTION__,vmeAdr);
#ifdef VXWORKS
      retVal = sysVmeDmaSend((UINT32)laddr, vmeAdr, (nwrds<<2), 0);
#else
      retVal = vmeDmaSend((UINT32)laddr, vmeAdr, (nwrds<<2));
#endif
      if(retVal != 0) 
	{
	  logMsg("\n%s: ERROR in DMA transfer Initialization 0x%x\n",__FUNCTION__,retVal,0,0,0,0);
	  DSCUNLOCK;
	  return(retVal);
	}

      /* Wait until Done or Error */
#ifdef VXWORKS
      retVal = sysVmeDmaDone(10000,1);
#else
      retVal = vmeDmaDone();
#endif

      if(retVal > 0)
	{
#ifdef VXWORKS
	  xferCount = (nwrds - (retVal>>2) + dummy); /* Number of longwords transfered */
#else
	  xferCount = ((retVal>>2) + dummy); /* Number of longwords transfered */
#endif
	  DSCUNLOCK;
	  return(xferCount);
	}
      else if (retVal == 0) 
	{
#ifdef VXWORKS
	  logMsg("\n%s: WARN: DMA transfer terminated by word count (nwrds = %d)\n",
		 __FUNCTION__,nwrds,0,0,0,0,0);
#else
	  logMsg("\n%s: WARN: DMA transfer returned zero word count (nwrds = %d)\n",
		 __FUNCTION__,nwrds,0,0,0,0,0);
#endif
	  DSCUNLOCK;
	  return(retVal);
	}
      else 
	{  /* Error in DMA */
#ifdef VXWORKS
	  logMsg("\n%s: ERROR: sysVmeDmaDone returned an Error\n",
		 __FUNCTION__,0,0,0,0,0);
#else
	  logMsg("\n%s: ERROR: vmeDmaDone returned an Error\n",
		 __FUNCTION__,0,0,0,0,0);
#endif
	  DSCUNLOCK;
	  return(retVal>>2);
	  
	}
#endif /* NODMA */

    }
  else
    {
      logMsg("%s: ERROR: Unsupported mode (%d)\n",__FUNCTION__,rmode);
    }

  return OK;

}

/*******************************************************************
 *   Function : vmeDSCPrintScalers
 *                      
 *   Function : Print values from scalers to standard out.  Perform 
 *              latch (before read) if requested.
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT32 rflag - Readout flag
 *                                 bit 0 - TRG Grp1 scalers
 *                                 bit 1 - TDC Grp1 scalers
 *                                 bit 2 - TRG Grp2 scalers
 *                                 bit 3 - TDC Grp2 scalers
 *                                 bit 4 - Reference Grp1 Scaler
 *                                 bit 5 - Reference Grp2scaler
 *                                 bit 6 - Latch Grp2 before read
 *                                 bit 7 - Latch Grp1 before read
 *                                                    
 *   Returns -1 if Error, Number of words transferred if OK.
 *                                                    
 *******************************************************************/

int
vmeDSCPrintScalers(UINT32 id, int rflag);
#ifdef OLDREV
{
  volatile UINT32 data[4*16+3];
  int nwrds=4*16+3;
  int dCnt, iword=0, ii;
  UINT32 header=0, header_rflag=0;
  CHECKID(id);

  printf("%s: Scalers read for DSC in slot %d:\n\n",__FUNCTION__,id);
  dCnt = vmeDSCReadScalers(id,(volatile UINT32 *)&data,nwrds,rflag,0);
  
  if(dCnt!=ERROR)
    {
#ifndef VXWORKS
      /* Byte Swap data[], if not using VXWORKS */
      for(ii=0; ii<dCnt; ii++)
	data[ii] = LSWAP(data[ii]);
#endif
      /* First word should be the header */
      header = data[0];
      if((header & 0xffff0000) == DSC_SCT_HEADER)
	{
	  printf("  HEADER WORD = 0x%08x\n",header);
	}
      else
	{
	  printf("  INVALID HEADER WORD (0x%08x)\n",header);
	  return ERROR;
	}
      header_rflag = header & 0xffff;
      iword++;
      if(header_rflag & DSC_READOUT_TRG_GRP1)
	{
	  printf("  Data from TRG grp1 scalers:\n");
	  for(ii=0; ii<16; ii++)
	    {
	      printf("     0x%08x",data[iword]);
	      if((ii-3)%4==0) printf("\n");
	      iword++;
	    }
	}
      if(header_rflag & DSC_READOUT_TDC_GRP1)
	{
	  printf("  Data from TDC grp1 scalers:\n");
	  for(ii=0; ii<16; ii++)
	    {
	      printf("     0x%08x",data[iword]);
	      if((ii-3)%4==0) printf("\n");
	      iword++;
	    }
	}
      if(header_rflag & DSC_READOUT_TRG_GRP2)
	{
	  printf("  Data from TRG grp2 scalers:\n");
	  for(ii=0; ii<16; ii++)
	    {
	      printf("     0x%08x",data[iword]);
	      if((ii-3)%4==0) printf("\n");
	      iword++;
	    }
	}
      if(header_rflag & DSC_READOUT_TDC_GRP2)
	{
	  printf("  Data from TDC grp2 scalers:\n");
	  for(ii=0; ii<16; ii++)
	    {
	      printf("     0x%08x",data[iword]);
	      if((ii-3)%4==0) printf("\n");
	      iword++;
	    }
	}
      if(header_rflag & DSC_READOUT_REF_GRP1)
	{
	  printf("  Data from Reference grp1 scaler:\n");
	  printf("     0x%08x\n",data[iword]);
	  iword++;
	}
      if(header_rflag & DSC_READOUT_REF_GRP2)
	{
	  printf("  Data from Reference grp2 scaler:\n");
	  printf("     0x%08x\n",data[iword]);
	  iword++;
	}
    }
  else
    {
      printf("%s: ERROR: vmeDSCReadScalers returned ERROR\n",__FUNCTION__);
      return ERROR;
    }

  return dCnt;
}
#endif
/*******************************************************************
 *   Function : vmeDSCPrintScalerRates
 *                      
 *   Function : Print rates from scalers to standard out.  Latch
 *              is performed based on chosen scalers (grp1 or 
 *              non-grp1) to print in rflag.
 *                                                    
 *   Parameters :  UINT32 id    - Module slot number
 *                 UINT32 rflag - Readout flag
 *                                 bit 0 - TRG Grp1 scalers
 *                                 bit 1 - TDC Grp1 scalers
 *                                 bit 2 - TRG Grp2 scalers
 *                                 bit 3 - TDC Grp2 scalers
 *                                                    
 *   Returns -1 if Error, 0 if OK.
 *                                                    
 *******************************************************************/

int
vmeDSCPrintScalerRates(UINT32 id, int rflag);
#ifdef OLDREV
{
  UINT32 latch_flag=0;
  volatile UINT32 data[4*16+3];
  int nwrds=4*16+3;
  int dCnt, iword=0, ii;
  unsigned int bitflag,bitcount;
  UINT32 header=0, header_rflag=0;
  UINT32 refGrp2Scaler=0, refGrp1Scaler=0;
  double refGrp2Time=0, refGrp1Time=0;

  CHECKID(id);

  if(rflag>0xf)
    {
      printf("%s: ERROR: Invalid rflag (0x%x)\n",
	     __FUNCTION__,rflag);
      return ERROR;
    }
  if(rflag==0)
    {
      printf("%s: WARN: Nothing to do with rflag=0x%x\n",
	     __FUNCTION__,rflag);
      return OK;
    }

  if((rflag&DSC_READOUT_TRG_GRP1) || (rflag&DSC_READOUT_TDC_GRP1))
    {
      latch_flag |= (DSC_LATCH_GRP1_BEFORE_READOUT | DSC_READOUT_REF_GRP1);
    }
  if((rflag&DSC_READOUT_TRG_GRP2) || (rflag&DSC_READOUT_TDC_GRP2))
    {
      latch_flag |= (DSC_LATCH_GRP2_BEFORE_READOUT | DSC_READOUT_REF_GRP2);
    }

  /* Count the bits in rflag */
  bitflag = rflag;
  for (bitcount = 0; bitflag; bitcount++)
      bitflag &= bitflag - 1; 

  dCnt = vmeDSCReadScalers(id,(volatile UINT32 *)&data,nwrds,rflag|latch_flag,0);

  if(dCnt!=ERROR)
    {
#ifndef VXWORKS
      /* Byte Swap data[], if not using VXWORKS */
      for(ii=0; ii<dCnt; ii++)
	data[ii] = LSWAP(data[ii]);
#endif
      printf("%s: Scaler rates read for DSC in slot %d:\n\n",__FUNCTION__,id);

      /* First word should be the header */
      header = data[0];
      if((header & 0xffff0000) == DSC_SCT_HEADER)
	{
	  printf("  HEADER WORD = 0x%08x\n",header);
	}
      else
	{
	  printf("  INVALID HEADER WORD (0x%08x)\n",header);
	  return ERROR;
	}
      header_rflag = header & 0xffff;

      /* Jump ahead in the data and get the reference counts */
      if(latch_flag & DSC_LATCH_GRP1_BEFORE_READOUT)
	{
	  refGrp2Scaler = data[16*bitcount+1];
	  if(refGrp2Scaler==0)
	    {
	      printf("%s: ERROR: Reference scaler returned 0 counts\n",
		     __FUNCTION__);
	      return ERROR;
	    }
	  else if(refGrp2Scaler==-1)
	    {
	      printf("%s: ERROR: Reference scaler count saturated\n",
		     __FUNCTION__);
	      return ERROR;
	    }
	  else
	    {
	      refGrp2Time = (double)refGrp2Scaler/DSC_REFERENCE_RATE;
	      printf("  Time since last latch = %lfs\n",refGrp2Time);
	    }
	  
	  if(latch_flag & DSC_LATCH_GRP2_BEFORE_READOUT)
	    {
	      refGrp1Scaler = data[16*bitcount+2];
	      if(refGrp1Scaler==0)
		{
		  printf("%s: ERROR: Reference Grp1 scaler returned 0 counts\n",
			 __FUNCTION__);
		  return ERROR;
		}
	      else if(refGrp1Scaler==-1)
		{
		  printf("%s: ERROR: Reference Grp1 scaler count saturated\n",
			 __FUNCTION__);
		  return ERROR;
		}
	      else
		{
		  refGrp1Time = (double)refGrp1Scaler/DSC_REFERENCE_RATE;
		  printf("  Time since last grp1 latch = %lfs\n",refGrp1Time);
		}
	    }
	}
      else
	{
	  refGrp1Scaler = data[16*bitcount+1];
	  if(refGrp1Scaler==0)
	    {
	      printf("%s: ERROR: Reference Grp1 scaler returned 0 counts\n",
		     __FUNCTION__);
	      return ERROR;
	    }
	  else if(refGrp1Scaler==-1)
	    {
	      printf("%s: ERROR: Reference Grp1 scaler count saturated\n",
		     __FUNCTION__);
	      return ERROR;
	    }
	  else
	    {
	      refGrp1Time = (double)refGrp1Scaler/DSC_REFERENCE_RATE;
	      printf("  Time since last grp1 latch = %lfs\n",refGrp1Time);
	    }
	}

      iword++;
      if(header_rflag & DSC_READOUT_TRG_GRP1)
	{
	  printf("  Rates from TRG grp1 scalers (Hz):\n");
	  for(ii=0; ii<16; ii++)
	    {
	      printf("  %12.3f",(double)data[iword]/refGrp1Time);
	      if((ii-3)%4==0) printf("\n");
	      iword++;
	    }
	}
      if(header_rflag & DSC_READOUT_TDC_GRP1)
	{
	  printf("  Rates from TDC grp1 scalers (Hz):\n");
	  for(ii=0; ii<16; ii++)
	    {
	      printf("  %12.3f",(double)data[iword]/refGrp1Time);
	      if((ii-3)%4==0) printf("\n");
	      iword++;
	    }
	}
      if(header_rflag & DSC_READOUT_TRG_GRP2)
	{
	  printf("  Rates from TRG grp2 scalers (Hz):\n");
	  for(ii=0; ii<16; ii++)
	    {
	      printf("  %12.3f",(double)data[iword]/refGrp2Time);
	      if((ii-3)%4==0) printf("\n");
	      iword++;
	    }
	}
      if(header_rflag & DSC_READOUT_TDC_GRP2)
	{
	  printf("  Rates from TDC grp2 scalers (Hz):\n");
	  for(ii=0; ii<16; ii++)
	    {
	      printf("  %12.3f",(double)data[iword]/refGrp2Time);
	      if((ii-3)%4==0) printf("\n");
	      iword++;
	    }
	}
    }
  else
    {
      printf("%s: ERROR: vmeDSCReadScalers returned ERROR\n",__FUNCTION__);
      return ERROR;
    }

  return dCnt;
}
#endif

static void
vmeDSCFlashChipSelect(UINT32 id, int select)
{
  if(select)
    vmeWrite32(&dscp[id]->calCmd,1);
  else
    vmeWrite32(&dscp[id]->calCmd,2);
  vmeWrite32(&dscp[id]->calExe,1);
  while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);
}

static int
vmeDSCFlashGetId(UINT32 id)
{
  //	return 0x20;
  vmeWrite32(&dscp[id]->calBuf[1],0x9F);
  vmeWrite32(&dscp[id]->calBuf[0],2);
  vmeWrite32(&dscp[id]->calCmd,3 | 0x30000);	// read flash id
  vmeWrite32(&dscp[id]->calExe,1);
  while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

  return vmeRead32(&dscp[id]->calBuf[2]) & 0xFF;
}

static int
vmeDSCFlashPollStatus(UINT32 id, UINT16 cmd)
{
  vmeWrite32(&dscp[id]->calBuf[1],cmd);
  vmeWrite32(&dscp[id]->calBuf[0],2);
  vmeWrite32(&dscp[id]->calCmd,3 | 0x30000);	// read flash status
  vmeWrite32(&dscp[id]->calExe,1);
  while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

  return vmeRead32(&dscp[id]->calBuf[2]) & 0xFF;
}

int
vmeDSCCalibrate(UINT32 id)
{
  vmeWrite32(&dscp[id]->calBuf[0], 0x5A0F1234); /* Calibration key */
  vmeWrite32(&dscp[id]->calCmd,0xFF);	        /* Run calibration */
  vmeWrite32(&dscp[id]->calExe,1);
  while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

  return OK;
}

int
vmeDSCCalibrateAll(void)
{
  int i;
	
  for(i = 0; i < Ndsc; i++)
    {
      printf("Calibrating unit %d...", i);
      vmeDSCCalibrate(i);
      printf("complete.\n");
    }
  return OK;
}

//dscSpiDACSet 8,8,0x40,2048
void
vmeDSCSpiDACSet(UINT32 id, int ch, int dac, int val)
{
  vmeWrite32(&dscp[id]->calBuf[2],val);
  vmeWrite32(&dscp[id]->calBuf[1],dac);
  vmeWrite32(&dscp[id]->calBuf[0],ch);
  vmeWrite32(&dscp[id]->calCmd,4);
  vmeWrite32(&dscp[id]->calExe,1);
  while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);
}

void 
vmeDSCDACLoop()
{
  int i, val = 0;
  while(1)
    {
      taskDelay(1);
      i = 8;
      //for(i = 0; i < 16; i++)
      {
	//			dscSpiDACSet(0, i, (0<<6), val);
	//			dscSpiDACSet(0, i, (1<<6), val);
	//			dscSpiDACSet(0, i, (2<<6), val);
	vmeDSCSpiDACSet(0, i, (3<<6), val);
      }
      val = (val + 1) & 0xFFF;
    }
}


static void
vmeDSCReloadFPGA(UINT32 id)
{
  vmeWrite32(&dscp[id]->calCmd,6);	// reload fpga
  vmeWrite32(&dscp[id]->calExe,1);
}

/*
  cd "../temp/vmeDSC"
  ld < vmeDSClib.o
  dscInit 0xbe0000
  vmeDSCUpdateFirmwareAll "vmeDSC_firmware_v1_9.bin"
*/
int
vmeDSCUpdateFirmware(UINT32 id, const char *filename)
{
  FILE *f;
  int i, flashId = 0;
  unsigned int addr = 0, page = 0;
  unsigned char buf[528];

  CHECKID(id);

  f = fopen(filename, "rb");
  if(!f)
    {
      printf("%s: ERROR: invalid file %s\n", 
	     __FUNCTION__, filename);
      return ERROR;
    }

  DSCLOCK;
	
  vmeDSCFlashChipSelect(id, 0);
  flashId = vmeDSCFlashGetId(id);
  
  if(flashId == 0x20)			// Numonyx flash
    {
      memset(buf, 0xff, 256);
      while(fread(buf, 1, 256, f) > 0)
	{
	  if(!(addr % 65536))		// sector erase
	    {
	      vmeWrite32(&dscp[id]->calBuf[1],0x06);	// write enable
	      vmeWrite32(&dscp[id]->calBuf[0],1);
	      vmeWrite32(&dscp[id]->calCmd,3 | 0x30000);
	      vmeWrite32(&dscp[id]->calExe,1);
	      while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

	      vmeWrite32(&dscp[id]->calCmd,3 | 0x30000);
	      vmeWrite32(&dscp[id]->calBuf[4],(addr)     & 0xFF);
	      vmeWrite32(&dscp[id]->calBuf[3],(addr>>8)  & 0xFF);
	      vmeWrite32(&dscp[id]->calBuf[2],(addr>>16) & 0xFF);
	      vmeWrite32(&dscp[id]->calBuf[1],0xD8);	// erase sector
	      vmeWrite32(&dscp[id]->calBuf[0],4);
	      vmeWrite32(&dscp[id]->calExe,1);
	      while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

	      i = 0;
	      while(1)
		{
		  if(!(vmeDSCFlashPollStatus(id,0x05) & 0x1))	// ~10us per poll
		    break;
		  if(!(i % 30000))
		    {
		      printf(".");
		      fflush(stdout);
		    }
		  if(i == 300000)	// max 3s sector erase time
		    {
		      DSCUNLOCK;
		      fclose(f);
		      printf("%s: ERROR: failed to erase flash\n", 
			     __FUNCTION__);
		      return ERROR;
		    }
		  i++;
		}
	    }

	  vmeWrite32(&dscp[id]->calBuf[1],0x06);	// write enable
	  vmeWrite32(&dscp[id]->calBuf[0],1);
	  vmeWrite32(&dscp[id]->calCmd,3 | 0x30000);
	  vmeWrite32(&dscp[id]->calExe,1);
	  while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

	  vmeWrite32(&dscp[id]->calBuf[4],(addr)     & 0xFF);
	  vmeWrite32(&dscp[id]->calBuf[3],(addr>>8)  & 0xFF);
	  vmeWrite32(&dscp[id]->calBuf[2],(addr>>16) & 0xFF);
	  vmeWrite32(&dscp[id]->calBuf[1],0x02);	// page write
	  vmeWrite32(&dscp[id]->calBuf[0],256+4);
	  vmeWrite32(&dscp[id]->calCmd,3 | 0x30000);
	  for(i = 255; i >= 0; i--)
	    vmeWrite32(&dscp[id]->calBuf[5+i], buf[i]);
	  vmeWrite32(&dscp[id]->calExe,1);
	  int iwait=0;
	  while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF)
	    {
	      iwait++;
	      if(iwait%1000==0)
		printf("X"); fflush(stdout);
	    }

	  i = 0;
	  while(1)
	    {
	      if(!(vmeDSCFlashPollStatus(id,0x05) & 0x1))	// ~10us per poll
		break;
	      if(i == 500)	// max 5ms page program time
		{
		  DSCUNLOCK;
		  fclose(f);
		  printf("%s: ERROR: failed to program flash\n", 
			 __FUNCTION__);
		  return ERROR;
		}
	      i++;
	    }

	  addr+= 256;
	}
    }
  else if(flashId == 0x1F)	// Atmel flash
    {
      memset(buf, 0xff, 528);
      while(fread(buf, 1, 528, f) > 0)
	{
	  // Buffer 1 write part 1
	  vmeWrite32(&dscp[id]->calCmd,3 | 0x10000);
	  vmeWrite32(&dscp[id]->calBuf[4],0x00);
	  vmeWrite32(&dscp[id]->calBuf[3],(page<<2)  & 0xFF);
	  vmeWrite32(&dscp[id]->calBuf[2],(page>>6) & 0xFF); // is this right??...
	  
	  vmeWrite32(&dscp[id]->calBuf[3],0x00);
	  vmeWrite32(&dscp[id]->calBuf[2],0x00);  // should be this...?
	  
	  vmeWrite32(&dscp[id]->calBuf[1],0x84);	// buffer 1 write
	  vmeWrite32(&dscp[id]->calBuf[0],4+264);
	  for(i = 263; i >= 0; i--)
	    vmeWrite32(&dscp[id]->calBuf[5+i], buf[i]);
	  vmeWrite32(&dscp[id]->calExe,1);
	  while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

	  // Buffer 1 write part 2
	  vmeWrite32(&dscp[id]->calCmd,3 | 0x20000);
	  vmeWrite32(&dscp[id]->calBuf[0],264);
	  for(i = 263; i >= 0; i--)
	    vmeWrite32(&dscp[id]->calBuf[1+i], buf[264+i]);
	  vmeWrite32(&dscp[id]->calExe,1);
	  while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

	  // Page erase & write			
	  vmeWrite32(&dscp[id]->calCmd,3 | 0x30000);
	  vmeWrite32(&dscp[id]->calBuf[4],0x00);
	  vmeWrite32(&dscp[id]->calBuf[3],(page<<2)  & 0xFF);
	  vmeWrite32(&dscp[id]->calBuf[2],(page>>6) & 0xFF);
	  vmeWrite32(&dscp[id]->calBuf[1],0x83);	// page erase+write
	  vmeWrite32(&dscp[id]->calBuf[0],4);
	  vmeWrite32(&dscp[id]->calExe,1);
	  while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

	  i = 0;
	  while(1)
	    {
	      if(vmeDSCFlashPollStatus(id,0xD7) & 0x80)	// ~10us per poll
		break;
	      if(i == 4000)	// max 40ms page erase+prog time
		{
		  DSCUNLOCK;
		  fclose(f);
		  printf("%s: ERROR: failed to erase flash\n", 
			 __FUNCTION__);
		  return ERROR;
		}
	      i++;
	    }
	  page++;
	  if(!(page % 10))
	    {
	      printf(".");
	      fflush(stdout);
	    }
	}
    }
  else
    {
      DSCUNLOCK;
      fclose(f);
      printf("%s: ERROR: failed to identify flash id 0x%02X\n", 
	     __FUNCTION__, flashId);
      return ERROR;
    }
  DSCUNLOCK;
  fclose(f);
  return OK;
}

int
vmeDSCVerifyFirmware(UINT32 id, const char *filename)
{
  FILE *f;
  int i,len, flashId = 0;
  unsigned int addr = 0;
  unsigned char buf[528];

  CHECKID(id);

  f = fopen(filename, "rb");
  if(!f)
    {
      printf("%s: ERROR: invalid file %s\n", 
	     __FUNCTION__, filename);
      return ERROR;
    }

  DSCLOCK;
	
  vmeDSCFlashChipSelect(id, 0);
  flashId = vmeDSCFlashGetId(id);

  if(flashId == 0x20)			// Numonyx flash
    {
      vmeWrite32(&dscp[id]->calBuf[4],0x00);
      vmeWrite32(&dscp[id]->calBuf[3],0x00);
      vmeWrite32(&dscp[id]->calBuf[2],0x00);
      vmeWrite32(&dscp[id]->calBuf[1],0x03);	// read array
      vmeWrite32(&dscp[id]->calBuf[0],4);
      vmeWrite32(&dscp[id]->calCmd,3 | 0x10000);
      vmeWrite32(&dscp[id]->calExe,1);
      while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

      while((len = fread(buf, 1, 256, f)) > 0)
	{
	  vmeWrite32(&dscp[id]->calBuf[0],256);
	  vmeWrite32(&dscp[id]->calCmd,3);
	  vmeWrite32(&dscp[id]->calExe,1);
	  while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

	  for(i = len-1; i >= 0; i--)
	    {
	      if(buf[i] != vmeRead32(&dscp[id]->calBuf[1+i]))
		{
		  vmeDSCFlashChipSelect(id, 0);
		  DSCUNLOCK;
		  fclose(f);					
		  printf("%s: ERROR: failed verify at addess 0x%08X[%02X,%02X]\n", 
			 __FUNCTION__, addr+i, 
			 buf[i], 
			 (int)vmeRead32(&dscp[id]->calBuf[1+i]));
		  return ERROR;
		}
	    }
	  addr+=256;
	}
      vmeDSCFlashChipSelect(id, 0);
    }
  else if(flashId == 0x1F)	// Atmel flash
    {
      vmeWrite32(&dscp[id]->calBuf[4],0x00);
      vmeWrite32(&dscp[id]->calBuf[3],0x00);
      vmeWrite32(&dscp[id]->calBuf[2],0x00);
      vmeWrite32(&dscp[id]->calBuf[1],0x03);	// read array
      vmeWrite32(&dscp[id]->calBuf[0],4);
      vmeWrite32(&dscp[id]->calCmd,3 | 0x10000);
      vmeWrite32(&dscp[id]->calExe,1);
      while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

      while((len = fread(buf, 1, 264, f)) > 0)
	{
	  vmeWrite32(&dscp[id]->calBuf[0],264);
	  vmeWrite32(&dscp[id]->calCmd,3);
	  vmeWrite32(&dscp[id]->calExe,1);
	  while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

	  for(i = len-1; i >= 0; i--)
	    {
	      if(buf[i] != vmeRead32(&dscp[id]->calBuf[1+i]))
		{
		  vmeDSCFlashChipSelect(id, 0);
		  DSCUNLOCK;
		  fclose(f);					
		  printf("%s: ERROR: failed verify at addess 0x%08X[%02X,%02X]\n", 
			 __FUNCTION__, addr+i, 
			 buf[i], 
			 (int)vmeRead32(&dscp[id]->calBuf[1+i]));
		  return ERROR;
		}
	    }
	  addr+=264;
	}
      vmeDSCFlashChipSelect(id, 0);
    }
  else
    {
      DSCUNLOCK;
      fclose(f);
      printf("%s: ERROR: failed to identify flash id 0x%02X\n", 
	     __FUNCTION__, flashId);
      return ERROR;
    }
  DSCUNLOCK;
  fclose(f);
  return OK;
}

int
vmeDSCUpdateFirmwareAll(const char *filename)
{
  int idsc, result;
	
  for(idsc = 0; idsc < Ndsc; idsc++)
    {
      printf("Updating firmware on unit %d...", idsc);
      fflush(stdout);
      result = vmeDSCUpdateFirmware(idsc, filename);
      if(result != OK)
	{
	  printf("failed.\n");
	  return result;
	}
      else
	printf("succeeded.");
		
      printf(" Verifying.");
      fflush(stdout);
      result = vmeDSCVerifyFirmware(idsc, filename);
      if(result != OK)
	{
	  printf("failed.\n");
	  return result;
	}
      else
	printf("ok.\n");

      vmeDSCReloadFPGA(idsc);
      sleep(2);
    }
  return OK;
}

int
vmeDSCReadFirmware(UINT32 id, const char *filename)
{
  FILE *f;
  int i, flashId = 0;
  unsigned int addr = 0;
  unsigned char buf[528];

  CHECKID(id);

  f = fopen(filename, "wb");
  if(!f)
    {
      printf("%s: ERROR: invalid file %s\n", 
	     __FUNCTION__, filename);
      return ERROR;
    }

  DSCLOCK;
	
  vmeDSCFlashChipSelect(id, 0);
  flashId = vmeDSCFlashGetId(id);

  if(flashId == 0x20)			// Numonyx flash
    {
      vmeWrite32(&dscp[id]->calBuf[4],0x00);
      vmeWrite32(&dscp[id]->calBuf[3],0x00);
      vmeWrite32(&dscp[id]->calBuf[2],0x00);
      vmeWrite32(&dscp[id]->calBuf[1],0x03);	// read array
      vmeWrite32(&dscp[id]->calBuf[0],4);
      vmeWrite32(&dscp[id]->calCmd,3 | 0x10000);
      vmeWrite32(&dscp[id]->calExe,1);
      while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

      while(addr < 2*1024*1024)
	{
	  printf("addr 0x%08X\n", addr);

	  vmeWrite32(&dscp[id]->calBuf[0],256);
	  vmeWrite32(&dscp[id]->calCmd,3);
	  vmeWrite32(&dscp[id]->calExe,1);
	  while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

	  for(i = 255; i >= 0; i--)
	    buf[i] = vmeRead32(&dscp[id]->calBuf[1+i]);

	  addr+= 256;
	  fwrite(buf, 1, 256, f);
	}
      vmeDSCFlashChipSelect(id, 0);
    }
  else if(flashId == 0x1F)	// Atmel flash
    {
      vmeWrite32(&dscp[id]->calBuf[4],0x00);
      vmeWrite32(&dscp[id]->calBuf[3],0x00);
      vmeWrite32(&dscp[id]->calBuf[2],0x00);
      vmeWrite32(&dscp[id]->calBuf[1],0x03);	// read array
      vmeWrite32(&dscp[id]->calBuf[0],4);
      vmeWrite32(&dscp[id]->calCmd,3 | 0x10000);
      vmeWrite32(&dscp[id]->calExe,1);
      while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

      while(addr < 4096*528)
	{
	  printf("addr 0x%08X\n", addr);

	  vmeWrite32(&dscp[id]->calBuf[0],264);
	  vmeWrite32(&dscp[id]->calCmd,3);
	  vmeWrite32(&dscp[id]->calExe,1);
	  while(vmeRead32(&dscp[id]->calCmd) != 0xFFFFFFFF);

	  for(i = 263; i >= 0; i--)
	    buf[i] = vmeRead32(&dscp[id]->calBuf[1+i]);

	  addr+= 264;
	  fwrite(buf, 1, 264, f);
	}
      vmeDSCFlashChipSelect(id, 0);
    }
  else
    {
      DSCUNLOCK;
      fclose(f);
      printf("%s: ERROR: failed to identify flash id 0x%02X\n", 
	     __FUNCTION__, flashId);
      return ERROR;
    }
  DSCUNLOCK;
  fclose(f);
  return OK;
}

#define CAL_ADDR_NUMONYX                0x1FE000
#define CAL_ADDR_ATMEL                  0x3FC000

int
vmeDSCFlashGetSerialInfo(UINT32 id, char *buf, int len)
{
  int i, flashId = vmeDSCFlashGetId(id);
  unsigned int addr;
        
  if(flashId == 0x20)                // Numonyx flash
    addr = CAL_ADDR_NUMONYX+0x1F00;
  else if(flashId == 0x1F)        // Atmel flash
    addr = CAL_ADDR_ATMEL+0x3C00;
  else
    {
      printf("%s ERROR: invalid flash id\n", __FUNCTION__);
      return ERROR;
    }
        
  vmeWrite32(&dscp[id]->calBuf[4], (addr>>0) & 0xFF);
  vmeWrite32(&dscp[id]->calBuf[3], (addr>>8) & 0xFF);
  vmeWrite32(&dscp[id]->calBuf[2], (addr>>16) & 0xFF);
  vmeWrite32(&dscp[id]->calBuf[1], 0x03);
  vmeWrite32(&dscp[id]->calBuf[0], len);
  vmeWrite32(&dscp[id]->calCmd, 3 | 0x30000);
  vmeWrite32(&dscp[id]->calExe, 0x01);
  taskDelay(2);
        
  for(i = 0; i < len; i++)
    buf[i] = vmeRead32(&dscp[id]->calBuf[5+i]) & 0xFF;
        
  return OK;
}

int
vmeDSCFlashPrintSerialInfo(UINT32 id)
{
  char buf[100];
  memset(buf, 0, sizeof(buf));
  vmeDSCFlashGetSerialInfo(id, buf, sizeof(buf)-1);
  printf("%s = %s\n", 
	 __FUNCTION__,buf);
        
  return OK;
}

int
vmeDSCFlashSetSerialInfo(UINT32 id, char AssyRev, int SerialNum, char Mfg[4], char *testDate)
{
  unsigned int addr;
  char buf[264], buf2[264];
  int i, flashId;
        
  memset(buf, 0xFF, sizeof(buf));
  //ex: "DSC2,Assy Rev: C,Serial Num: ACDI-26,Test Date: Fri Oct 14 10:01:05 2011"
  //    AssyRev = 'C'
  //    SerialNumber = 26
  //    Mfg[0] = 'A'
  //    Mfg[1] = 'C'
  //    Mfg[2] = 'D'
  //    Mfg[3] = 'I'
  //    testData = "Fri Oct 14 10:01:05 2011";
  sprintf(buf, "DSC2,Assy Rev: %c,Serial Num: %c%c%c%c-%d,Test Date: %s",
	  AssyRev, Mfg[0], Mfg[1], Mfg[2], Mfg[3], SerialNum, testDate);
        
  flashId = vmeDSCFlashGetId(id);
        
  if(flashId == 0x20)                // Numonyx flash
    {
      addr = CAL_ADDR_NUMONYX+0x1F00;
                
      vmeWrite32(&dscp[id]->calBuf[1], 0x06);
      vmeWrite32(&dscp[id]->calBuf[0], 1);
      vmeWrite32(&dscp[id]->calCmd, 3 | 0x30000);
      vmeWrite32(&dscp[id]->calExe, 0x01);
      taskDelay(2);

      vmeWrite32(&dscp[id]->calBuf[4], (addr>>0) & 0xFF);
      vmeWrite32(&dscp[id]->calBuf[3], (addr>>8) & 0xFF);
      vmeWrite32(&dscp[id]->calBuf[2], (addr>>16) & 0xFF);
      vmeWrite32(&dscp[id]->calBuf[1], 0x02);
      vmeWrite32(&dscp[id]->calBuf[0], 256+4);
      vmeWrite32(&dscp[id]->calCmd, 3 | 0x30000);

      for(i = 255; i >= 0; i--)
	vmeWrite32(&dscp[id]->calBuf[5+i], (unsigned int)buf[i]);

      vmeWrite32(&dscp[id]->calExe, 0x01);
      taskDelay(2);
    }
  else if(flashId == 0x1F)        // Atmel flash
    {
      addr = CAL_ADDR_ATMEL+0x3C00;

      // Write Buffer 1
      vmeWrite32(&dscp[id]->calBuf[4], 0x00);
      vmeWrite32(&dscp[id]->calBuf[3], 0x00);
      vmeWrite32(&dscp[id]->calBuf[2], 0x00);
      vmeWrite32(&dscp[id]->calBuf[1], 0x84);
      vmeWrite32(&dscp[id]->calBuf[0], 256+4);
      vmeWrite32(&dscp[id]->calCmd, 3 | 0x10000);
      for(i = 255; i >= 0; i--)
	vmeWrite32(&dscp[id]->calBuf[5+i], (unsigned int)buf[i]);
      vmeWrite32(&dscp[id]->calExe, 0x01);
      taskDelay(2);

      vmeWrite32(&dscp[id]->calBuf[0], 256+16);
      vmeWrite32(&dscp[id]->calCmd, 3 | 0x20000);
      for(i = 256+16-1; i >= 0; i--)
	vmeWrite32(&dscp[id]->calBuf[i], (unsigned int)buf[i]);
      vmeWrite32(&dscp[id]->calExe, 0x01);
      taskDelay(2);

      // Buffer 1 -> Flash (w/o erase)
      vmeWrite32(&dscp[id]->calCmd, 3 | 0x30000);
      vmeWrite32(&dscp[id]->calBuf[4], (addr>>0) & 0xFF);
      vmeWrite32(&dscp[id]->calBuf[3], (addr>>8) & 0xFF);
      vmeWrite32(&dscp[id]->calBuf[2], (addr>>16) & 0xFF);
      vmeWrite32(&dscp[id]->calBuf[1], 0x88);
      vmeWrite32(&dscp[id]->calBuf[0], 4);
      vmeWrite32(&dscp[id]->calExe, 0x01);
      taskDelay(2);
    }
  else
    {
      printf("%s ERROR: invalid flash id\n", __FUNCTION__);
      return ERROR;
    }
        
  vmeDSCFlashGetSerialInfo(id, buf2, strlen(buf)+1);
        
  if(!strncmp(buf, buf2, strlen(buf)+1))
    {
      printf("%s ERROR: write verify of serial info failed\n", __FUNCTION__);
      printf("   wrote: %s\n", buf);
      printf("   read: %s\n", buf2);
      return ERROR;
    }
  return OK;
}


int
vmeDSCSetPulseWidthAll(UINT16 tdcVal, UINT16 trgVal, UINT16 trgoutVal)
{
  int idsc, errStat=OK;

  for(idsc = 0; idsc < Ndsc; idsc++)
    {
      errStat |= vmeDSCSetPulseWidth(dscID[idsc], tdcVal, 1);
      errStat |= vmeDSCSetPulseWidth(dscID[idsc], trgVal, 2);
      errStat |= vmeDSCSetPulseWidth(dscID[idsc], trgoutVal, 3);
    }

  return errStat;
}

int
vmeDSCSetThresholdAll(UINT16 tdcVal, UINT16 trgVal)
{
  int idsc, ichan, errStat=OK;

  for(idsc = 0; idsc < Ndsc; idsc++)
    {
      for(ichan = 0; ichan < 16; ichan++)
	{
	  errStat |= vmeDSCSetThreshold(dscID[idsc], ichan, tdcVal, 1);
	  errStat |= vmeDSCSetThreshold(dscID[idsc], ichan, trgVal, 2);
	}
    }

  return errStat;
}

int
vmeDSCSetBipolarThresholdAll(INT16 tdcVal, INT16 trgVal)
{
  int idsc, ichan, errStat=OK;

  for(idsc = 0; idsc < Ndsc; idsc++)
    {
      for(ichan = 0; ichan < 16; ichan++)
	{
	  errStat |= vmeDSCSetBipolarThreshold(dscID[idsc], ichan, tdcVal, 1);
	  errStat |= vmeDSCSetBipolarThreshold(dscID[idsc], ichan, trgVal, 2);
	}
    }

  return errStat;
}
