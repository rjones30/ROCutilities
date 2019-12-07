//
//  Mark Dalton  April 2015
//
//  This program writes the scaler rates as a function of threshold to a file for each channel.
//
//  The scaler rates are read from the shared memory buffer and writes them to a text 
//
//
//  Compile and run with the following:
//
//  gcc ratevsthreshold_allchan.c -I ../rcm/monitor/  -o ratevsthreshold_allchan.exe
// ./ratevsthreshold_allchan.exe
//
//  The executable needs access to programs that will change the threshold in the current directory.
//  Run the following commands.
//  
//  ln -s /gluonfs1/home/dalton/work/DAQ/new/daq_dev_vers/daq/vme/src/vmefa/test/faSetThresholds
//  ln -s /gluonfs1/home/dalton/work/DAQ/new/daq_dev_vers/daq/vme/src/vmeDSC/test/vmeDSCSetThresholds
//
#include <sys/prctl.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

int min(int a, int b) { return (a < b)? a : b; }
int max(int a, int b) { return (a > b)? a : b; }

//--------------------------------------------
#include "shmem_roc.h"
static void shmem_get();
static int  semid,shmid;
static roc_shmem *shmem_ptr;
void get_rates (int, int, int);
//--------------------------------------------

// Structure for general system command
char SystemCallString[2000];           // global array of strings to store system calls
void *SystemCall( void *argptr);       // program to preform a system call (run in a thread)
//--------------------------------------------



int main(int argc, char *argv[], char *envp[]) {
   if (argc<4) {
      printf("\nUseage:\n\n\tratevsthreshold_allchan  thresh_min  thresh_max  thresh_step\n\n\n");

   } else {
      shmem_get();
      
      int thresh_min = atof(argv[1]);
      int thresh_max = atof(argv[2]);
      int thresh_step = atof(argv[3]);
      printf("Stepping through thresholds from %i to %i in steps of %i\n", thresh_min, thresh_max, thresh_step);
      get_rates( thresh_min, thresh_max, thresh_step);
      pthread_exit(NULL);


   }
   return 0;
}

//-----------------------------------------------------------------------------------
//          get rates
//-----------------------------------------------------------------------------------
void get_rates(int thresh_min, int thresh_max, int thresh_step) {
   int slot,ichan;
   unsigned int update_f_old=0, update_d_old=0;
   pthread_t thread;

   //printf("Stepping through thresholds from %i to %i in steps of %i\n", thresh_min, thresh_max, thresh_step);

   int doADC=0;  // Variable to record if we have ADCs
   int doDSC=1;  // Variable to record if we have Discriminators
   FILE *outfile[21][16];      // have a potential file handle for each channel
   int fileisopen[21][16];     // record if file handle is open for each channel
   int i,j;
   for (i=0; i<21; i++) {
      for (j=0; j<16; j++) {
         fileisopen[i][j]=0;
      }
   }

   char *host;
   host = getenv("HOSTNAME");
   printf("Running on host %s\n",host);


   // Make a date time label
   time_t result = time(NULL); 
   char *timestring = ctime(&result);
   char datetimelabel[255];
   int count=0;
   printf("time %i  '%s'\n",(int)result,ctime(&result));
   char *pch;
   pch = strtok(timestring," :");
   while (pch != NULL) {
      if (count==0)
         sprintf(datetimelabel,"%s",pch);
      else if (count==3)
         sprintf(datetimelabel,"%s_%s",datetimelabel,pch);
      else if (count==4)
         sprintf(datetimelabel,"%sh%s",datetimelabel,pch);
      else if (count<5)
         sprintf(datetimelabel,"%s%s",datetimelabel,pch);
      count++;
      pch = strtok (NULL, " :");
   }
   printf("datetimelabel '%s'\n",datetimelabel);

   // step through the thresholds
   int threshold;
   int loopmax, loopmin;
   loopmax = max(thresh_min,thresh_max);
   loopmin = min(thresh_min,thresh_max);

   for (threshold=thresh_min; threshold<=thresh_max; threshold+=thresh_step) {

      // set the tresholds on the crate and wait.
      printf("Setting a threshold of %i\n",threshold);
      if (doADC==1) {
         sprintf(SystemCallString,"echo faSetThresholds %i",threshold);
         system(SystemCallString);
      }
      if (doDSC==1) {
         sprintf(SystemCallString,"./vmeDSCSetThresholds %i",threshold);
         system(SystemCallString);
      }
      // read the current update number
      if (doADC==1) {
         unsigned int update_f_atchange=shmem_ptr->f250_scalers.update;
         // wait for 2 updates to make sure that there is no stale data
         printf("Waiting for scaler update (%u) clear stale data\n",update_f_atchange);
         while(1) {
            unsigned int update_f=shmem_ptr->f250_scalers.update;
            if (update_f == update_f_atchange)
               usleep(100000);
            else {
               update_f_atchange = update_f;
               break;
            }
            //printf("%i\n",update_f);
         }
         printf("Waiting for scaler update (%u) get new rates\n",update_f_atchange);
         while(1) {
            unsigned int update_f=shmem_ptr->f250_scalers.update;
            if (update_f == update_f_atchange) usleep(100000);
            else {
               update_f_atchange = update_f;
               break;
            }
            //printf("%i\n",update_f);
         }
      }
      if (doDSC==1) {
         unsigned int update_f_atchange=shmem_ptr->discr_scalers.update;
         // wait for 2 updates to make sure that there is no stale data
         printf("Waiting for scaler update (%u) clear stale data\n",update_f_atchange);
         while(1) {
            unsigned int update_f=shmem_ptr->discr_scalers.update;
            if (update_f == update_f_atchange)
               usleep(100000);
            else {
               update_f_atchange = update_f;
               break;
            }
            //printf("%i\n",update_f);
         }
         printf("Waiting for scaler update (%u) get new rates\n",update_f_atchange);
         while(1) {
            unsigned int update_f=shmem_ptr->discr_scalers.update;
            if (update_f == update_f_atchange) usleep(100000);
            else {
               update_f_atchange = update_f;
               break;
            }
            //printf("%i\n",update_f);
         }
      }

      // Write data to file
      char filename[255];
      //---------------------------------------------------------------------------
      //            FA250 rates
      //---------------------------------------------------------------------------
      if (doADC==1) {
         int numADCchan=0;
         for(slot=0; slot<21; slot++) {
            if (shmem_ptr->f250_scalers.counters[slot][16]==0) continue;
            //printf("  Fill slot=%d \n",slot);
            for(ichan=0; ichan<16; ichan++) {
               //printf("  chan=%d \n",ichan);
               if (shmem_ptr->f250_scalers.counters[slot][ichan] > 0) {
                  numADCchan++;
                  if (fileisopen[slot][ichan]==0) {
                     sprintf(filename,"ADC_ratevthresh_%s_%s_s%02ic%02i.txt",host,datetimelabel,slot,ichan);
                     //printf("opening file: %s\n",filename);
                     outfile[slot][ichan] = fopen(filename, "a");
                  fileisopen[slot][ichan]=1;
                  }
                  fprintf(outfile[slot][ichan],"%4i %10.3f\n",threshold,shmem_ptr->f250_scalers.rates[slot][ichan]);
               }
            }               
         }
         if (numADCchan==0) {
            doADC=0;
            printf("Ignoring ADCs in future.\n");
         }
         printf("Wrote %i ADC channels\n",numADCchan);
      }
      //---------------------------------------------------------------------------------------------------
      //             DISCR rates 
      //---------------------------------------------------------------------------------------------------
      if (doDSC==1) {
         int numDSCchan=0;
         for(slot=0; slot<21; slot++) {
            if (shmem_ptr->discr_scalers.counters[slot][16]==0) continue;
            //printf("  Fill slot=%d \n",slot);
            for(ichan=0; ichan<16; ichan++) {
               //if (shmem_ptr->discr_scalers.counters[slot][ichan] > 0) {
                  numDSCchan++;
                  if (fileisopen[slot][ichan]==0) {
                     sprintf(filename,"DSC_ratevthresh_%s_%s_s%02ic%02i.txt",host,datetimelabel,slot,ichan);
                     outfile[slot][ichan] = fopen(filename, "a");
                     fileisopen[slot][ichan]=1;
                  }
                  fprintf(outfile[slot][ichan],"%4i %10.3f\n",
                        threshold,shmem_ptr->discr_scalers.rates[slot][ichan]);
                  //}
            }               
         }
         if (numDSCchan==0) {
            //doDSC=0;
            printf("Ignoring discriminators in future.\n");
         }
         printf("Wrote %i Discriminator channels\n",numDSCchan);
      }
   }

   printf("Closing the text files\n");
   for (i=0; i<21; i++) {
      for (j=0; j<15; j++) {
         if (fileisopen[i][j]==1) {
            fclose(outfile[i][j]);
         }
      }
   }
}
//-----------------------------------------------------------------------------------
//                   Shared memory         
//-----------------------------------------------------------------------------------
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
    
   //printf("===== wait semaphore  ======  \n"); 
   //fflush(stdout); 
   //-- access to shmem --
   //sem_wait(semid);    
   // if (shmem_ptr) shmem_ptr->rol2_hist=1;
   //sem_post(semid);

}
//-----------------------------------------------------------------------------------

//-----------------------------------------------------------------------------------
//                   General system command        
//-----------------------------------------------------------------------------------
void *SystemCall( void *argptr) {
   char *mySystemCallStringArray;
   mySystemCallStringArray = (char *) argptr;
   char command[2000];
   sprintf(command,"%s",mySystemCallStringArray);
   printf("   %s\n",command);
   //system(command);
   pthread_exit(NULL);
}
//-----------------------------------------------------------------------------------
