/*
 * File: vmeDSCPrintScalerRates.c
 *
 * Description:
 *    Print scaler rates from all vme discriminators in the crate
 *
 * Richard Jones - January, 2018
 *
 * Warning: DO NOT execute while a CODA run is ongoing, as it will
 *          interfere with the shmem_srv process that is running in
 *          the background and updating scaler tables in shared
 *          memory. Use vmeDSCPrintShmemRates instead.
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include <vmeDSClib.h>

char *progName;
void Usage();

DMA_MEM_ID vmeIN,vmeOUT;
extern DMANODE *the_event;
extern unsigned int *dma_dabufp;

int main(int argc, char *argv[]) 
{
	int stat;
	unsigned int counter[2][21][16] = {{{0}}};

	printf("\nJLAB vmeDSC Print Scalers\n");
	printf("----------------------------\n");

	progName = argv[0];

	vmeSetQuietFlag(1);
	if (vmeOpenDefaultWindows() != OK) {
		printf(" Failed to access VME bridge\n");
		exit(1);
	}

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
	vmeIN  = dmaPCreate("vmeIN",10244,500,0);
	vmeOUT = dmaPCreate("vmeOUT",0,0,0);
	  
	dmaPStatsAll();
	dmaPReInitAll();

        unsigned int *dma_dabufp_saved = dma_dabufp;

	extern int Ndsc;
	int slot=0;
	int chan=0;
	int iFlag=(1<<16);  /* Do not attempt to initialize the module(s) */

	printf(" Locating DSC in the crate...\n");
	stat = vmeDSCInit((3<<19), (1<<19), 20, iFlag);
	if (stat != ERROR) {
		int i;
		int idsc;
		int iloop;
		unsigned int *p;
		GETEVENT(vmeIN,0);
		for (idsc=0; idsc < Ndsc; idsc++) {
			slot = vmeDSCSlot(idsc);
			vmeDSCClear(slot);
			vmeDSCReadoutConfig(slot, DSC_READOUT_TDC_GRP1 |
						  //DSC_READOUT_REF_GRP1 |
						  DSC_READOUT_LATCH_GRP1, 
						  DSC_READOUT_TRIGSRC_SOFT);
			vmeDSCStatus(slot, 0);
			vmeDSCSoftTrigger(slot);
			if (!vmeDSCDReady(slot)) {
				printf("waiting for ready...\n");
				usleep(100000);
			}
			dma_dabufp += vmeDSCReadBlock(slot, dma_dabufp, 100, 1);
		}
		PUTEVENT(vmeOUT);
		DMANODE *outEvent = dmaPGetItem(vmeOUT);
		p = counter[0][0];
		for (i=0; i < outEvent->length; i++) {
			if (outEvent->data[i] & 0x80)
				continue;
			*(p++) = (unsigned int)LSWAP(outEvent->data[i]);
		}
		printf("total count was %d\n", p - counter[0][0]);
		dmaPFreeItem(outEvent);
		for (iloop=1; iloop < 999999; iloop++) {
			usleep(1000000);
			dma_dabufp = dma_dabufp_saved;
			GETEVENT(vmeIN,0);
			for (idsc=0; idsc < Ndsc; idsc++) {
				slot = vmeDSCSlot(idsc);
				vmeDSCClear(slot);
				vmeDSCReadoutConfig(slot, DSC_READOUT_TDC_GRP1 |
							  //DSC_READOUT_REF_GRP1 |
							  DSC_READOUT_LATCH_GRP1, 
							  DSC_READOUT_TRIGSRC_SOFT);
				vmeDSCSoftTrigger(slot);
				if (!vmeDSCDReady(slot)) {
					printf("waiting for ready...\n");
					usleep(100000);
				}
				dma_dabufp += vmeDSCReadBlock(slot, dma_dabufp, 100, 1);
			}
			PUTEVENT(vmeOUT);
			DMANODE *outEvent = dmaPGetItem(vmeOUT);
			p = counter[i%2][0];
			for (i=0; i < outEvent->length; i++) {
				if (outEvent->data[i] & 0x80)
					continue;
				*(p++) = (unsigned int)LSWAP(outEvent->data[i]);
			}
			dmaPFreeItem(outEvent);
			for (idsc=0; idsc < Ndsc; idsc++) {
				slot = vmeDSCSlot(idsc);
				printf("slot %2i:", slot);
				for (chan=0; chan < 16; chan++) {
					double rate = counter[i%2][idsc][chan] - counter[(i+1)%2][idsc][chan];
					printf("%10.0f", rate);
				}
				printf("\n");
			}
			printf("total count was %d\n", p - counter[i%2][0]);
		}
	}
	vmeCloseDefaultWindows();
	exit(0);
}
