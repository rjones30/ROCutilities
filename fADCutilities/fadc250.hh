//
// fadc250 - utility class for initialziation and readout of
//           the Jlab fadc250 vme module on a frontend roc.
//
// author: richard.t.jones at uconn.edu
// version: november 27, 2019
//

extern "C" {
    #include "jvme.h"
    #include "fadcLib.h"
}

class fadc250
{
  public:
    fadc250();
    ~fadc250();

    int acquire(int slot, int events, int bufsize, unsigned int *data, int threshold);
    int set_dac_levels(int id, int baseline);
    void decode(unsigned int data);
    void dumpconfig();

  protected:
    int fadcCount;
    int fadcSlot[32];
};
