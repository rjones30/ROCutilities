//
// vmeDSCPrintScalerRates.c
//  
// author: richard.t.jones at uconn.edu
// version: january 27, 2018
//
// Based on ratevsthreshold_allchan.c by Mark Dalton - April 2015
//

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "jvme.h"
#include "vmeDSClib.h"

#include "shmem_roc.h"
static void shmem_get();
static int  semid,shmid;
static roc_shmem *shmem_ptr;
void get_rates (int, int, int);

char *progName;

int main(int argc, char *argv[]) 
{
  printf("\nJLAB vmeDSC Scaler Rates\n");
  printf("----------------------------\n");

  progName = argv[0];

  shmem_get();
  while (1) {
    int idsc;
    int ndsc = shmem_ptr->discr_scalers.Nslots;
    printf(" Scaler type TDC:\n");
    for (idsc=0; idsc < ndsc; idsc++) {
      int chan;
      int slot = shmem_ptr->discr_scalers.slots[idsc];
      printf("slot %d:", slot);
      for (chan=0; chan < 16; chan++) {
        printf(" %8.0f", shmem_ptr->discr_scalers.rates[slot][chan]);
      }
      printf("\n");
    }
    printf(" Scaler type TRG:\n");
    for (idsc=0; idsc < ndsc; idsc++) {
      int chan;
      int slot = shmem_ptr->discr_scalers.slots[idsc];
      printf("slot %d:", slot);
      for (chan=0; chan < 16; chan++) {
        printf(" %8.0f", shmem_ptr->discr_scalers.rates2[slot][chan]);
      }
      printf("\n");
    }
    usleep(1000000);
  }
}

static void shmem_get() {
  if ( (semid=semget( SEM_ID1, 1, 0) ) < 0 ) {
    printf("shmem: can not get semaphore \n"); 
    fflush(stdout);
  }
  if ( (shmid=shmget(SHM_ID1,sizeof(roc_shmem), 0))<0) {
    printf("==> shmem: shared memory 0x%x, size=%d get error=%d\n",SHM_ID1,sizeof(roc_shmem),shmid);
    fflush(stdout);
    exit(1);
  }
  if ( (shmem_ptr=(roc_shmem *) shmat (shmid, 0, 0))<0 ) {
    printf("==> shmem: shared memory attach error\n");
    fflush(stdout);
  }
  printf("==> shmem: shared memory attached OK ptr=%p\n",shmem_ptr); 
  fflush(stdout);
}
