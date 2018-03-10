#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <stdio.h>
#include <time.h>

#include "hbook.h" 
#include "bor_roc.h"

#define SEM_ID1	0xDF1	
#define SHM_ID1	0xDF2
#define PERMS	00666	


#define LFname 128

#define SEMAPHORE_2 1
int sem_wait(int semid);
int sem_post(int semid);
int sem_get(void);

#define LINFO 128

#define LLogBuffer 128000

typedef struct
{  
  int32_t wr_ptr;
  int32_t rd_ptr;
  int32_t loop_ptr;
  int32_t level_ptr;
  char buf[LLogBuffer];
}  LogBufferShm;


//---------- FADC 250 [16] chan / 125 [72] chan. Configuration  --------------

#define MAX_CONF 21*72
typedef  struct  { 
  int32_t Flag; 
  int32_t DAC_set; 
  int32_t PED;  //-- target ped: 100/200..
  int32_t Mode;
  int32_t Nchan;
  int32_t Nerror;
  int32_t DAC[MAX_CONF];
  int32_t THR[MAX_CONF];
} __attribute__((__packed__)) FADC_Config;
//--------------------- PED monitor  ---------------------
#define MAX_PED 21*72
typedef struct {
  int32_t status;
  int64_t updated;
  double mean;
  double sigma;
  int32_t noent;
} __attribute__((__packed__)) PedMon;

//---------- FADC 250 Scalers --------------

#define MAX_SLOT  21
#define MAX_CHAN  16

typedef  struct  {
  uint32_t   counters[MAX_SLOT][MAX_CHAN+1];  //-- last is timer 
  double     rates[MAX_SLOT][MAX_CHAN+1];     //-- last is timer
  int32_t    Nslots;
  int32_t    slots[MAX_SLOT];        //-- installed --
  uint32_t   update;
} __attribute__((__packed__)) F250_Scalers;

//---------- vmeDSC  Scalers --------------

typedef  struct  {
  //
  int32_t  threshold_flag2;
  uint32_t thresholds2[MAX_SLOT+1][MAX_CHAN+1];
  uint32_t counters2[MAX_SLOT+1][MAX_CHAN+1];  //-- last is timer 
  double   rates2[MAX_SLOT+1][MAX_CHAN+1];     //-- last is timer
  //
  int32_t  threshold_flag;
  uint32_t thresholds[MAX_SLOT+1][MAX_CHAN+1];
  uint32_t counters[MAX_SLOT+1][MAX_CHAN+1];  //-- last is timer 
  double   rates[MAX_SLOT+1][MAX_CHAN+1];     //-- last is timer
  //
  int32_t  Nslots;
  int32_t  slots[MAX_SLOT+1];        //-- installed --
  uint32_t tv_sec;
  uint32_t tv_usec;
  uint32_t update;
} __attribute__((__packed__)) vmeDSC_Scalers;

//--------------------------------------------------------------
//                       SHARED Memory 
//--------------------------------------------------------------
typedef struct
{  
    int32_t  RUN_STATUS;
    uint32_t counter;
    int64_t  Time_mark;
    int32_t  READY;

    int64_t Time_Now; 

//-- RUN info ---

    int32_t RUN_Flag;
    int64_t VME_Time;
    int64_t VME_Size;
    char   Run_Status[128];
    int32_t Message_Flag;
    int32_t shmem_users;  //-- only for debug shmem access :: #ifdef DEBUG_SHMEM
    int32_t shmem_error;  //-- only for debug shmem access :: #ifdef DEBUG_SHMEM
    int32_t RUN_Number;

#define LMessage 8192
  char Message[LMessage];

//---------- Log Buffer 65k --------------
  LogBufferShm LogBuf;



//-----------------  ROL 1/2  vars  -------------------------


//----------  Event Buffer  --------------

  int32_t rol2_hist;

#define LEventBuffer 2000000  
  //  20000 x 100 
  int32_t EventBufferFLAG;
  int32_t EventLength;
  int32_t EventMaxLength;
  int32_t EventSkipCounter;
  int32_t EventBuffer[LEventBuffer];


//---------- FADC 250 Scalers --------------
  F250_Scalers f250_scalers;
  
//---------- vmeDSC   Scalers --------------
  vmeDSC_Scalers discr_scalers;

//---------- f125/f250 configuratuon  ------
#define MAX_FADC_CONFIG 5
  int32_t FADC_TYPE;
  FADC_Config fadc_config[MAX_FADC_CONFIG]; //---  0=F125 , 1=F250

//--------------------- PED monitor  ---------------------
  PedMon pedmon[MAX_PED];


//---------- Modules configuration for BOR --------------

	ModulesConfigBOR BOR;  


//---------- Book Hist --------------

#define USE_HIST 
#define      h_shm_rol1_L1 512
#define      h_shm_rol1_L2 5
#define      h_shm_rol1_L h_shm_rol1_L1*h_shm_rol1_L2
  uint32_t h_shm_rol1[h_shm_rol1_L2][h_shm_rol1_L1];
    
#define      h_shm_rol2_L1 512
#define      h_shm_rol2_L2 5
#define      h_shm_rol2_L  h_shm_rol2_L1*h_shm_rol2_L2
  uint32_t h_shm_rol2[h_shm_rol2_L2][h_shm_rol2_L1];
    
#define      h_shm_evb_L1 512
#define      h_shm_evb_L2  5
#define      h_shm_evb_L   h_shm_evb_L1*h_shm_evb_L2
  uint32_t h_shm_evb[h_shm_evb_L2][h_shm_evb_L1];
    
  //-- error counter for each module ID (max ID=25)
  //-- L2=0-timed_out-send(revcd) L2=1-timed_out-evb(revcd)   
  //-- L2=2-extra_mod  L2=3-(all recvd)   L2=4-recv-wrong-trigID
#define      h_shm_err_L1  25
#define      h_shm_err_L2  15
#define      h_shm_err_L   h_shm_evb_L1*h_shm_evb_L2
  uint32_t h_shm_err[h_shm_evb_L2][h_shm_evb_L1];

//------------------------------------------    
//   hist 
//------------------------------------------    

#define H_rol1_NH 100
  SHM_HIST H_rol1[H_rol1_NH];
#define H_rol2_NH 100
  SHM_HIST H_rol2[H_rol2_NH];
#define H_err_NH 100
  SHM_HIST H_err[H_err_NH];

//------------------------------------------
  
  
} __attribute__((__packed__))
roc_shmem;

