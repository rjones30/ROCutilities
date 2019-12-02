//
// fadc250 - utility class for initialziation and readout of
//           the Jlab fadc250 vme module on a frontend roc.
//
// author: richard.t.jones at uconn.edu
// version: november 27, 2019
//


#include <iostream>
#include <stdexcept>
#include <sstream>
#include <unistd.h>

#include "fadc250.hh"

fadc250::fadc250()
{
    vmeSetQuietFlag(1);
    if (vmeOpenDefaultWindows() != OK) {
        throw std::runtime_error("failed to access VME bridge");
    }

    int iFlag = 0;
    //iFlag = FA_INIT_SKIP;
    //iFlag |= FA_INIT_SKIP_FIRMWARE_CHECK;
    iFlag |= FA_INIT_SOFT_TRIG;

    vmeBusLock();

    faInit((3<<19), (1<<19), 8, iFlag);
    fadcCount = 0;
    for (int ifa=0; ifa < 8; ifa++) {
        int slot = faSlot(ifa);
        if (slot > 0)
           fadcSlot[fadcCount++] = slot;
        else
           break;
    }
}

fadc250::~fadc250()
{
    vmeBusUnlock();
}

int fadc250::set_dac_levels(int slot, int baseline)
{
    int step = 1024;
    short int dac[16];
    for (int i=0; i<16; i++)
        dac[i] = 2047;
    float level[16];
    while (step > 0) {
        for (int i=0; i<16; ++i)
            level[i] = 0;
        for (int a=0; a<100; a++) {
            unsigned int data[8];
            void *dptr = (void*) data;
            unsigned short *levels = (unsigned short*)dptr;
            faReadAllChannelSamples(slot, data);
            for (int i=0; i<16; ++i)
                level[i] += 0.01 * (levels[i] & 0xfff);
        }
        for (int i=0; i<16; ++i) {
            dac[i] += (level[i] > baseline)? step : -step;
            faSetDAC(slot, dac[i], (1<<i));
        }
        step = int(step * 0.7);
    }
    return 0;
}

int fadc250::acquire(int slot, int events, int bufsize, unsigned int *data)
{
    int pmode = 10;
    unsigned int PL = 900;
    unsigned int PTW = 500;
    unsigned int NSB = 3;
    unsigned int NSA = 15;
    unsigned int NP = 1;
    unsigned int NPED = 5;
    unsigned int MAXPED = 600;
    unsigned int NSAT = 2;
    faSetProcMode(slot, pmode, PL, PTW, NSB, NSA, NP, NPED, MAXPED, NSAT);
    faSetTriggerStopCondition(slot, 99);
    faSetTriggerBusyCondition(slot, 99);
    faSetBlockLevel(slot, events);
    faEnable(slot, 1, 0);
    for (int i=0; i<events; i++) {
        faTrig(slot);
        usleep(10);
    }
    return faReadBlock(slot, data, bufsize, 0);
}

void fadc250::decode(unsigned int data)
{
    faDataDecode(data);
}

void fadc250::dumpconfig()
{
    std::cout << "fadc250 crate configuration:" << std::endl;
    for (int ifa=0; ifa < fadcCount; ++ifa) {
        std::cout << "   " << ifa << ": slot "
                  << fadcSlot[ifa] << std::endl;
    }
    std::cout << "total of " << fadcCount << " modules."
              << std::endl;
}
