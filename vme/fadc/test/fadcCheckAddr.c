/*
 * File:
 *    fadcCheckAddr.c
 *
 * Description:
 *    Check the memory map of the FADC V2
 *
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jvme.h"
#include "fadcLib.h"

#define FADC_ADDR 0xed0000

DMA_MEM_ID vmeIN, vmeIN2, vmeOUT;
#define MAX_NUM_EVENTS    400
#define MAX_SIZE_EVENTS   1024*10      /* Size in Bytes */

extern int fadcA32Base;
extern volatile struct fadc_struct *FAp[(FA_MAX_BOARDS+1)];
extern int fadcA24Offset;

int 
main(int argc, char *argv[]) {

    GEF_STATUS status;

    printf("\nJLAB fadc Lib Tests\n");
    printf("----------------------------\n");

    status = vmeOpenDefaultWindows();

    fadcA32Base=0x09000000;
    /* Set the FADC structure pointer */
    faInit((unsigned int)(FADC_ADDR),0x0,1,0x25);
    
    faStatus(0,0);

    unsigned int base, addr1, addr2;
    base = (unsigned int)FAp[11];

    printf("\n\n");
    printf("base = 0x%08x\n",base-fadcA24Offset);
    
    printf("  blk_level        = 0x%x\n",
	   (unsigned int)&(FAp[11]->blk_level)-(unsigned int)FAp[11]);
    printf("  s_adr            = 0x%x\n",
	   (unsigned int)&(FAp[11]->s_adr)-(unsigned int)FAp[11]);
    printf("  trig_scal        = 0x%x\n",
	   (unsigned int)&(FAp[11]->trig_scal)-(unsigned int)FAp[11]);
    printf("  blk_wrdcnt_fifo  = 0x%x\n",
	   (unsigned int)&(FAp[11]->blk_wrdcnt_fifo)-(unsigned int)FAp[11]);
    printf("  dac[0]           = 0x%x\n",
	   (unsigned int)&(FAp[11]->dac[0])-(unsigned int)FAp[11]);
    printf("  dac[8]           = 0x%x\n",
	   (unsigned int)&(FAp[11]->dac[8])-(unsigned int)FAp[11]);
    printf("  status[0]        = 0x%x\n",
	   (unsigned int)&(FAp[11]->status[0])-(unsigned int)FAp[11]);
    printf("  aux[0]           = 0x%x\n",
	   (unsigned int)&(FAp[11]->aux[0])-(unsigned int)FAp[11]);
    printf("  ram_r_adr        = 0x%x\n",
	   (unsigned int)&(FAp[11]->ram_r_adr)-(unsigned int)FAp[11]);
    printf("  berr_module_scal = 0x%x\n",
	   (unsigned int)&(FAp[11]->berr_module_scal)-(unsigned int)FAp[11]);
    printf("  aux_scal[2]      = 0x%x\n",
	   (unsigned int)&(FAp[11]->aux_scal[2])-(unsigned int)FAp[11]);
    printf("  busy_level       = 0x%x\n",
	   (unsigned int)&(FAp[11]->busy_level)-(unsigned int)FAp[11]);
    printf("  mgt_status       = 0x%x\n",
	   (unsigned int)&(FAp[11]->mgt_status)-(unsigned int)FAp[11]);
    printf("  adc_status[0]    = 0x%x\n",
	   (unsigned int)&(FAp[11]->adc_status[0])-(unsigned int)FAp[11]);
    printf("  ptw_max_buf      = 0x%x\n",
	   (unsigned int)&(FAp[11]->ptw_max_buf)-(unsigned int)FAp[11]);
    printf("  hitsum_status    = 0x%x\n",
	   (unsigned int)&(FAp[11]->hitsum_status)-(unsigned int)FAp[11]);
    printf("  hitsum_pattern   = 0x%x\n",
	   (unsigned int)&(FAp[11]->hitsum_pattern)-(unsigned int)FAp[11]);

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

