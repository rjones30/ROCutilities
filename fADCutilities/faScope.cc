//
// faScope - a virtual oscilloscope utility for interactive display
//           of traces collected by the Jlab fadc250 vme module.
//           Traces are collected on a frontend roc are saved in a
//           root tree, and also made available via a network socket.
//
// author: richard.t.jones at uconn.edu
// version: november 27, 2019
//
// Usage:
//        $ ./faScope 3 5 1000  # collects 1000 traces from slot 3, channel 5
//        $ ./faScope 3 5       # starts a server and listens on port 47000
//
// Notes:
//  1. If run without a third argument (the number of events requested),
//     faScope listens for an incoming connection on port 47000 and
//     responds to requests for raw mode data from the client.
//  2. faScopeDisplay.C is a special client that runs in an interactive
//     root session on another machine, and connects back to faScope over
//     the network to acquire and display traces received from the frontend.
//

#include <iostream>
#include <sstream>
#include <fadc250.hh>

#include <byteswap.h>
#include <stdlib.h>

#include <TFile.h>
#include <TTree.h>
#include <TH1I.h>
#include <TServerSocket.h>
#include <TMessage.h>

fadc250 factrl;

TTree *tree1 = 0;
TH1I *trace[16] = {0};
TH1I *pulse[16] = {0};

TServerSocket *sock = 0;

struct tree1_t {
    int nhit;
    int hcrate[16];
    int hslot[16];
    int hch[16];
    int nsamp[16];
    int samp[16][500];
    int invalid[16];
    int overflow[16];
} trow;

void usage()
{
    printf("Usage: faScope <slot> <channel> [<nevents>]\n");
    printf("   slot = 3-10\n");
    printf("   channel = 0-15\n");
    printf("   nevents = 1-inf (0 or omit for interactive loop)\n");
    exit(1);
}

void build_tree1(TTree *tree1)
{
    tree1->Branch("nhit",&trow.nhit,"nhit/I");
    tree1->Branch("hcrate",trow.hcrate,"hcrate[nhit]/I");
    tree1->Branch("hslot",trow.hslot,"hslot[nhit]/I");
    tree1->Branch("hch",trow.hch,"hch[nhit]/I");
    tree1->Branch("nsamp",trow.nsamp,"nsamp[nhit]/I");
    tree1->Branch("samp",trow.samp,"samp[nhit][500]/I");
    tree1->Branch("invalid",trow.invalid,"invalid[nhit]/I");
    tree1->Branch("overflow",trow.overflow,"overflow[nhit]/I");
}

int decode(int dlen, unsigned int *data)
{
    for (int i=0; i<16; ++i) {
        trace[i]->Reset();
        pulse[i]->Reset();
    }

    // parameters stored in the pulse "histogram" are:
    // bin 1: event number (1-255)
    // bin 2: channel number (consistency check)
    // bin 3: quality factor (0 for good, 1 for questionable)
    // bin 4: pedestal sum
    // bin 5: number of pulses found (1-4)
    // bin 6: pulse 1 integral (0-256k)
    // bin 7: pulse 1 late (1=overlaps with end of window, 0=no overlap)
    // bin 8: pulse 1 overflows (1=contains overflows, 0=no overflows)
    // bin 9: pulse 1 underflows (1=contains underflows, 0=no underflows)
    // bin 10: pulse 1 width (samples, 1-511)
    // bin 11: pulse 1 leading edge time (units of 62.5 ps)
    // bin 12: pulse 1 peak value (0-4095)
    // bin 13: pulse 1 peak outside window (1=outside, 0=inside)
    // bin 14: pulse 1 peak not found (1=failed, 0=ok)
    // bin 15: pulse 1 baseline is high (1=above TET or MAXPED, 0=ok)
    // bin 16...: repetitions of bins 6-14 for pulses 2..npulses

    int events=0;
    int event=0;
    int crate=0;
    int slot=0;
    int eseq=0;
    int chan=0;
    int tA, tB, tC, tD, tE, tF;
    trow.nhit = 0;
    for (int i=0; i<dlen; ++i) {
        int hdr = bswap_32(data[i]);
        if ((hdr & 0xf8000000) == 0x80000000) {
            // block header
            slot = ((hdr & 0x7800000) >> 23);
            event = ((hdr & 0x3ff00) >> 8);
        }
        else if ((hdr & 0xf8000000) == 0x98000000) {
            // trigger time word 1
            tD = ((hdr & 0xff0000) >> 16);
            tE = ((hdr & 0xff00) >> 8);
            tF = (hdr & 0xff);
        }
        else if ((hdr & 0xf8000000) == 0x00000000) {
            // trigger time word 2
            tA = ((hdr & 0xff0000) >> 16);
            tB = ((hdr & 0xff00) >> 8);
            tC = (hdr & 0xff);
        }
        else if ((hdr & 0xf8000000) == 0x90000000) {
            // event header
            slot = ((hdr & 0x7800000) >> 23);
            event = (hdr & 0x3fffff);
            trow.nhit = 0;
        }
        else if ((hdr & 0xf8000000) == 0xc8000000) {
            // pulse parameter 1
            eseq = ((hdr & 0x7f80000) >> 19);
            chan = ((hdr & 0x78000) >> 15);
            int qf = ((hdr & 0x4000) >> 14);
            int pedsum = (hdr & 0x3fff);
            pulse[chan]->SetBinContent(1, eseq);
            pulse[chan]->SetBinContent(2, chan);
            pulse[chan]->SetBinContent(3, qf);
            pulse[chan]->SetBinContent(4, pedsum);
            pulse[chan]->SetBinContent(5, 0);
        }
        else if ((hdr & 0xc0000000) == 0x40000000) {
            // pulse parameter 2
            int pint = ((hdr & 0x3ffff000) >> 12);
            int late = ((hdr & 0x800) >> 11);
            int oflow = ((hdr & 0x400) >> 10);
            int uflow = ((hdr & 0x200) >> 9);
            int pwidth = (hdr & 0x1ff);
            int npeak = int(pulse[chan]->GetBinContent(5));
            pulse[chan]->SetBinContent(6 + 10*npeak, pint);
            pulse[chan]->SetBinContent(7 + 10*npeak, late);
            pulse[chan]->SetBinContent(8 + 10*npeak, oflow);
            pulse[chan]->SetBinContent(9 + 10*npeak, uflow);
            pulse[chan]->SetBinContent(10 + 10*npeak, pwidth);
 
            // pulse parameter 3
            int ppar3 = bswap_32(data[++i]);
            int pt = ((ppar3 & 0x3fff8000) >> 15);
            int pv = ((ppar3 & 0x00007ff8) >> 3);
            int pout = ((ppar3 & 0x00000004) >> 2);
            int pbad = ((ppar3 & 0x00000002) >> 1);
            int phigh = (ppar3 & 0x00000001);
            pulse[chan]->SetBinContent(11 + 10*npeak, pt);
            pulse[chan]->SetBinContent(12 + 10*npeak, pv);
            pulse[chan]->SetBinContent(13 + 10*npeak, pout);
            pulse[chan]->SetBinContent(14 + 10*npeak, pbad);
            pulse[chan]->SetBinContent(15 + 10*npeak, phigh);
            pulse[chan]->SetBinContent(5, ++npeak);
        }
        else if ((hdr & 0xf8000000) == 0xb0000000) {
            // raw pulse window
            while ((bswap_32(data[i+1]) & 0xc0000000) == 0) {
                ++i;
            }
        }
        else if ((hdr & 0xf8000000) == 0xf8000000) {
            // filler word
        }
        else if ((hdr & 0xf8000000) == 0xa0000000) {
            chan = ((hdr & 0x7800000) >> 23);
            int nsamples = (hdr & 0x1ff) - 1;
            int n = trow.nhit++;
            trow.hcrate[n] = crate;
            trow.hslot[n] = slot;
            trow.hch[n] = chan;
            trow.nsamp[n] = nsamples;
            int istart = i+1;
            int istop = istart + (nsamples+1)/2;
            int invalid = 0;
            int overflow = 0;
            for (i=istart; i<istop; ++i) {
                int code = bswap_32(data[i]);
                int adc0 = ((code & 0xffff0000) >> 16);
                int adc1 = (code & 0xffff);
                trow.samp[n][2*(i-istart)] = adc0;
                trow.samp[n][2*(i-istart)+1] = adc1;
                invalid += ((adc0 & 0x2000) >> 13);
                invalid += ((adc1 & 0x2000) >> 13);
                overflow += ((adc0 & 0x1000) >> 12);
                overflow += ((adc1 & 0x1000) >> 12);
                trace[chan]->Fill(8*(i-istart), adc0);
                trace[chan]->Fill(8*(i-istart)+4, adc1);
            }
            --i;
            trow.invalid[n] = invalid;
            trow.overflow[n] = overflow;
            if (trow.nhit == 16) {
                tree1->Fill();
                trow.nhit = 0;
                events++;
            }
        }
        else if ((hdr & 0xf8000000) == 0xd0000000) {
            // scaler block
            int scalers = (hdr & 0x3f);
            i += scalers;
        }
        else if ((hdr & 0xf8000000) == 0xe8000000) {
            // event trailer
        }
        else if ((hdr & 0xf8000000) == 0x88000000) {
            // block trailer
        }
        else {
            printf("skipping %x followed by %x:", hdr, bswap_32(data[i+1]));
            factrl.decode(hdr);
        }
    }
    return events;
}

int main(int argc, char *argv[])
{
    int slot = 3;
    int channel = 0;
    int nevents = 0;
    if (argc > 1)
        slot = std::atoi(argv[1]);
    if (argc > 2)
        channel = std::atoi(argv[2]);
    if (argc > 3)
        nevents = std::atoi(argv[3]);

    for (int s=3; s<11; ++s) {
       factrl.set_dac_levels(s, 400);
       printf("slot %d initialized\n", s);
    }
    exit(0);

    TFile fout("faScope.root", "recreate");
    tree1 = new TTree("tree1", "TAGM FADC tree");
    build_tree1(tree1);

    for (int i=0; i<16; ++i) {
        std::stringstream name;
        name << "t" << i;
        std::stringstream title;
        title << "trace from slot " << slot << " channel " << channel;
        trace[i] = new TH1I(name.str().c_str(), title.str().c_str(),
                            500, 0, 2000);
        trace[i]->SetStats(0);
        trace[i]->GetXaxis()->SetTitle("t (ns)");
        trace[i]->GetYaxis()->SetTitle("V (adc)");
        trace[i]->GetYaxis()->SetTitleOffset(1.4);
        title << "pulse parameters from slot " 
              << slot << " channel " << channel;
        pulse[i] = new TH1I(name.str().c_str(), title.str().c_str(),
                            100, 0, 100);
        pulse[i]->SetStats(0);
        pulse[i]->GetXaxis()->SetTitle("parameter (see faScope.cc)");
        pulse[i]->GetYaxis()->SetTitle("value");
        pulse[i]->GetYaxis()->SetTitleOffset(1.4);
    }

    int tet = 0;
    int events = 1;
    int bufsize = events * 4100;
    unsigned int *data = (unsigned int *)malloc(bufsize * sizeof(int));
    for (int i=0; i<nevents || nevents==0; i+=events) {
        int nrec = factrl.acquire(slot, events, bufsize, data, tet);
        decode(nrec, data);
        if (nevents == 0) {
            if (sock == 0)
                sock = new TServerSocket("mbus", kTRUE);
            TSocket *conn = sock->Accept();
            TMessage msg(kMESS_OBJECT);
            msg.WriteObject(trace[channel]);
            msg.WriteObject(pulse[channel]);
            conn->Send(msg);
        }
    }

    tree1->Write();
    return 0;
}
