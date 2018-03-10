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
 *     Header file for the TJNAF VME 16 Channel Discriminator/Scaler 
 *
 * SVN: $Rev$
 *
 *----------------------------------------------------------------------------*/

#ifndef __VMEDSC__
#define __VMEDSC__

#define DSC_BOARD_ID       0x44534332
#define DSC_MAX_BOARDS             20
#define DSC_MAX_SLOTS              21
#define DSC_MAX_A32_MEM      0x800000   /* 8 Meg */
/* Reference channel clock frequency - 125 MHz */
#define DSC_REFERENCE_RATE  125000000.0

/* Some definitions for "type" argument in routines */
#define TDCTRG      0  /* Both TDC and TRG */
#define TDC         1
#define TRG         2
#define TRGOUTPUT   3

/* Define DSC Memory structure */
struct dsc_struct 
{
  /* 0x0000 */ volatile unsigned int threshold[16];
  /* 0x0040 */ volatile unsigned int trgOut[16];
  /* 0x0080 */ volatile unsigned int pulsewidth;
  /* 0x0084 */ volatile unsigned int reserved0[(0x0088-0x0084)/4];
  /* 0x0088 */ volatile unsigned int chEnable;
  /* 0x008C */ volatile unsigned int orMask;
  /* 0x0090 */ volatile unsigned int delay;
  /* 0x0094 */ volatile unsigned int testCtrl;
  /* 0x0098 */ volatile unsigned int ScalerLatchGrp2;
  /* 0x009C */ volatile unsigned int ScalerLatchGrp1;
  /* 0x00A0 */ volatile unsigned int trgOutSrc;
  /* 0x00A4 */ volatile unsigned int Adr32;
  /* 0x00A8 */ volatile unsigned int Interrupt;
  /* 0x00AC */ volatile unsigned int InterruptAck;
  /* 0x00B0 */ volatile unsigned int Geo;
  /* 0x00B4 */ volatile unsigned int IO;
  /* 0x00B8 */ volatile unsigned int ScalerGateGrp2;
  /* 0x00BC */ volatile unsigned int ScalerGateGrp1;
  /* 0x00C0 */ volatile unsigned int PulserPeriod;
  /* 0x00C4 */ volatile unsigned int PulserLow;
  /* 0x00C8 */ volatile unsigned int PulserNPulses;
  /* 0x00CC */ volatile unsigned int PulserStart;
  /* 0x00D0 */ volatile unsigned int PulserStatus;
  /* 0x00D4 */ volatile unsigned int reserved1[(0x00F8-0x00D4)/4];
  /* 0x00F8 */ volatile unsigned int reserved2[(0x0100-0x00F8)/4];
  /* 0x0100 */ volatile unsigned int TrgScalerGrp1[16];
  /* 0x0140 */ volatile unsigned int TdcScalerGrp1[16];
  /* 0x0180 */ volatile unsigned int TrgScalerGrp2[16];
  /* 0x01C0 */ volatile unsigned int TdcScalerGrp2[16];
  /* 0x0200 */ volatile unsigned int refScalerGrp2;
  /* 0x0204 */ volatile unsigned int refScalerGrp1;
  /* 0x0208 */ volatile unsigned int reserved3[(0x400-0x208)/4];
  /* 0x0400 */ volatile unsigned int firmwareRev;
  /* 0x0404 */ volatile unsigned int boardID;
  /* 0x0408 */ volatile unsigned int SerialNum;
  /* 0x040C */ volatile unsigned int SerialMfg;
  /* 0x0410 */ volatile unsigned int reserved4[(0x0500-0x410)/4];
  /* 0x0500 */ volatile unsigned int readoutClear;
  /* 0x0504 */ volatile unsigned int readoutStart;
  /* 0x0508 */ volatile unsigned int readoutCfg;
  /* 0x050C */ volatile unsigned int reserved5[(0x8000-0x50C)/4];
  /* 0x8000 */ volatile unsigned int calCmd;
  /* 0x8004 */ volatile unsigned int calBuf[(0x8204-0x8004)/4];
  /* 0x8204 */ volatile unsigned int reserved6[(0x9000-0x8204)/4];
  /* 0x9000 */ volatile unsigned int calExe;
};

#define DSC_SUPPORTED_FIRMWARE 0x10E

/* 0x0000 threshold register Masks */
#define DSC_THRESHOLD_TRG_MASK          0x0fff0000
#define DSC_THRESHOLD_TDC_MASK          0x00000fff

/* 0x0040 trgOut register Masks */
#define DSC_TRIGOUT_DELAY_MASK          0x000000FF
#define DSC_TRIGOUT_WIDTH_MASK          0x0000FF00

/* 0x0080 pulsewidth register Masks */
#define DSC_PULSEWIDTH_TRG_MASK         0x003f0000
#define DSC_PULSEWIDTH_TDC_MASK         0x0000003f

/* 0x0088 chEnable register Masks */
#define DSC_CHENABLE_TRG_MASK           0xffff0000
#define DSC_CHENABLE_TDC_MASK           0x0000ffff

/* 0x008C orMask register Masks */
#define DSC_ORMASK_TRG_MASK             0xffff0000
#define DSC_ORMASK_TDC_MASK             0x0000ffff

/* 0x0090 delay register Masks */
#define DSC_DELAY_GRP2_MASK             0x03ff0000
#define DSC_DELAY_GRP1_MASK             0x000003ff

/* 0x0094 testCtrl register bits and masks */
#define DSC_TESTCTRL_FP_MASK        0x00000003
#define DSC_TESTCTRL_ROUTE_IN1      (1<<0)
#define DSC_TESTCTRL_ROUTE_IN2      (1<<1)
#define DSC_TESTCTRL_SOFT_PULSE     (1<<31)

/* 0x00A0 trgOutSrc register masks */
#define DSC_TRIGOUTSRC_SOURCE_MASK          0x0000ffff
#define DSC_TRIGOUTSRC_BYPASS_RESHAPER_MASK 0xffff0000

/* 0x00A4 Adr32 register bits and masks */
#define DSC_ADR32_ENABLE    (1<<0)
#define DSC_ADR32_BASE_MASK 0x0000FF80  // FIXME: Check with Ben.

/* 0x00A8 Interrupt bits and masks */
#define DSC_INTERRUPT_VECTOR_MASK 0x000000FF
#define DSC_INTERRUPT_LEVEL_MASK  0x00000700
#define DSC_INTERRUPT_ENABLE      (1<<31)

/* 0x00B0 Geo register bits and masks */
#define DSC_GEO_SLOTID_MASK      0x0000001F
#define DSC_GEO_VME_ADDRESS_MASK 0xFFFF0000

/* 0x00B4 IO register bits */
#define DSC_IO_FP_OR_OUT_HI      (1<<0)
#define DSC_IO_FP_IN1_HI         (1<<1)
#define DSC_IO_FP_IN2_HI         (1<<2)

/* 0x00B8-0x00BC ScalerGateGrp* register masks */
#define DSC_SCALERGATEGRP_SOURCE_MASK     0x0000000F
#define DSC_SCALERGATEGRP_SOURCE_IN1      (1<<0)
#define DSC_SCALERGATEGRP_SOURCE_IN2      (1<<1)
#define DSC_SCALERGATEGRP_SOURCE_CONSTANT (1<<2)
#define DSC_SCALERGATEGRP_SOURCE_PULSER   (1<<3)

/* Define bits ofr vmeDSCSetGateSource */
#define DSC_GATESRC_IN1      (1<<0)
#define DSC_GATESRC_IN2      (1<<1)
#define DSC_GATESRC_CONSTANT (1<<2)
#define DSC_GATESRC_PULSER   (1<<3)

/* 0x00D0 PulserStatus register bits */
#define DSC_PULSERSTATUS_INACTIVE         (1<<0)

/* 0x0400 firmwareRev register Masks */
#define DSC_FIRMWAREREV_MASK            0x0000ffff
#define DSC_FIRMWAREREV_MAJOR_MASK      0x0000ff00
#define DSC_FIRMWAREREV_MINOR_MASK      0x000000ff

/* 0x0500 readoutClear registers bits */
#define DSC_READOUTCLEAR_CLEAR          (1<<0)

/* 0x0504 readoutStart registers bits */
#define DSC_READOUTSTART_TRG_GRP1               (1<<0)
#define DSC_READOUTSTART_TDC_GRP1               (1<<1)
#define DSC_READOUTSTART_TRG_GRP2               (1<<2)
#define DSC_READOUTSTART_TDC_GRP2               (1<<3)
#define DSC_READOUTSTART_REF_GRP1               (1<<4)
#define DSC_READOUTSTART_REF_GRP2               (1<<5)
#define DSC_READOUTSTART_LATCH_GRP2             (1<<6)
#define DSC_READOUTSTART_LATCH_GRP1             (1<<7)
#define DSC_READOUTSTART_MASK                   0x000000FF
#define DSC_READOUTSTART_SOURCE_IN1             (1<<16)
#define DSC_READOUTSTART_SOURCE_IN2             (1<<17)
#define DSC_READOUTSTART_SOURCE_SOFT            (1<<18)
#define DSC_READOUTSTART_SOURCE_PULSER_ROLLOVER (1<<19)
#define DSC_READOUTSTART_SOURCE_MASK            0x000F0000
#define DSC_READOUTSTART_SOFT_TRIG              (1<<31)

/* Bits for vmeDSCReadoutConfig arguments */
#define DSC_READOUT_TRG_GRP1   (1<<0)
#define DSC_READOUT_TDC_GRP1   (1<<1)
#define DSC_READOUT_TRG_GRP2   (1<<2)
#define DSC_READOUT_TDC_GRP2   (1<<3)
#define DSC_READOUT_REF_GRP1   (1<<4)
#define DSC_READOUT_REF_GRP2   (1<<5)
#define DSC_READOUT_LATCH_GRP2 (1<<6)
#define DSC_READOUT_LATCH_GRP1 (1<<7)
#define DSC_READOUT_MASK       0xFF

#define DSC_READOUT_TRIGSRC_IN1    (1<<0)
#define DSC_READOUT_TRIGSRC_IN2    (1<<1)
#define DSC_READOUT_TRIGSRC_SOFT   (1<<2)
#define DSC_READOUT_TRIGSRC_PULSER (1<<3)
#define DSC_READOUT_TRIGSRC_MASK   0xF

/* 0x0508 readoutCfg register bits and masks */
#define DSC_READOUTCFG_BERR_ENABLE         (1<<0)
#define DSC_READOUTCFG_WORD_COUNT_INT_MASK 0x0000FFFE
#define DSC_READOUTCFG_EVT_COUNT_INT_MASK  0x00FF0000
#define DSC_READOUTCFG_EVENTS_READY_MASK   0xFF000000

/* Scaler readout definitions */
#define DSC_DATA_TYPE_DEFINING_WORD   (1<<31)
#define DSC_DATA_TYPE_BLOCKHEADER     0
#define DSC_DATA_TYPE_EOB             1
#define DSC_DATA_TYPE_EVENTHEADER     2
#define DSC_DATA_TYPE_SCALERHEADER    8
#define DSC_DATA_TYPE_DATA_NOT_VALID  14
#define DSC_DATA_TYPE_FILLER          15
#define DSC_DATA_FILLER               (DSC_DATA_TYPE_FILLER<<27)


/* Function Prototypes */
int  vmeDSCInit(unsigned int addr, unsigned int addr_incr, int ndsc, int iFlag);
int  vmeDSCSlot(unsigned int i);
int  vmeDSCStatus(UINT32 id, int pflag);
int  vmeDSCGStatus(int pflag);
int  vmeDSCGetFirmwareVersion(int id, int pflag);
int  vmeDSCGetSerialNumber(int id, char **rSN, int pflag);
int  vmeDSCSetThreshold(UINT32 id, UINT16 chan, UINT16 val, UINT16 type);
int  vmeDSCGetThreshold(UINT32 id, UINT16 chan, UINT16 type);
int  vmeDSCSetBipolarThreshold(UINT32 id, UINT16 chan, INT16 val, UINT16 type);
int  vmeDSCGetBipolarThreshold(UINT32 id, UINT16 chan, UINT16 type);
int  vmeDSCSetTRGOut(UINT32 id, UINT16 channel, UINT16 width, UINT16 delay);
int  vmeDSCSetTRGOutSource(UINT32 id, UINT16 selMask, UINT16 bypMask);
int  vmeDSCSetPulseWidth(UINT32 id, UINT16 val, UINT16 type);
int  vmeDSCGetPulseWidth(UINT32 id, UINT16 type);
int  vmeDSCEnableChannel(UINT32 id, UINT16 chan, UINT16 type);
int  vmeDSCDisableChannel(UINT32 id, UINT16 chan, UINT16 type);
int  vmeDSCSetChannelMask(UINT32 id, UINT16 chMask, UINT16 type);
int  vmeDSCGetChannelMask(UINT32 id, UINT16 type);
int  vmeDSCEnableChannelOR(UINT32 id, UINT16 chan, UINT16 type);
int  vmeDSCDisableChannelOR(UINT32 id, UINT16 chan, UINT16 type);
int  vmeDSCSetChannelORMask(UINT32 id, UINT16 chMask, UINT16 type);
int  vmeDSCGetChannelORMask(UINT32 id, UINT16 type);
int  vmeDSCSetDelay(UINT32 id, UINT16 val, UINT16 group);
int  vmeDSCGetDelay(UINT32 id, UINT16 type);
int  vmeDSCSetTestInput(UINT32 id, UINT32 flag);
int  vmeDSCTestPulse(UINT32 id, UINT32 npulses);
int  vmeDSCLatchScalers(UINT32 id, UINT16 type);
int  vmeDSCSetGateSource(UINT32 id, UINT16 group, UINT32 srcMask);
UINT32 vmeDSCGetAdr32(int id);
int  vmeDSCSetAdr32(UINT32 id, UINT32 a32base, UINT16 enable);
int  vmeDSCAdr32Enable(UINT32 id);
int  vmeDSCAdr32Disable(UINT32 id);
int  vmeDSCClear(UINT32 id);
int  vmeDSCDReady(UINT32 id);
int  vmeDSCReadoutConfig(UINT32 id, UINT32 rconf, UINT32 rsrc);
int  vmeDSCSoftTrigger(UINT32 id);
int  vmeDSCReadBlock(UINT32 id, volatile UINT32 *data, int nwrds, int rmode);
int  vmeDSCReadScalers(UINT32 id, volatile UINT32 *data, int nwrds, int rflag, int rmode);
int  vmeDSCPrintScalers(UINT32 id, int rflag);
int  vmeDSCPrintScalerRates(UINT32 id, int rflag);
int  vmeDSCSetPulseWidthAll(UINT16 tdcVal, UINT16 trgVal, UINT16 trgoutVal);
int  vmeDSCSetThresholdAll(UINT16 tdcVal, UINT16 trgVal);
int  vmeDSCSetBipolarThresholdAll(INT16 tdcVal, INT16 trgVal);
#endif /* __VMEDSC__ */
