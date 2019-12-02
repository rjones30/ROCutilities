//
// faScope - a virtual oscilloscope utility for interactive display
//           of traces collected by the Jlab fadc250 vme module.
//           Traces are collected on a frontend roc are saved in a
//           root tree for immediate display on one of the gluon
//           machines.
//
// author: richard.t.jones at uconn.edu
// version: november 27, 2019
//

#include <iostream>
#include <sstream>
#include <fadc250.hh>

#include <byteswap.h>
#include <stdlib.h>

#include <TFile.h>
#include <TTree.h>
#include <TH1D.h>
#include <TServerSocket.h>
#include <TMessage.h>

fadc250 factrl;

TTree *tree1 = 0;
TH1D *trace[16] = {0};

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
    int events=0;
    int event=0;
    int crate=0;
    int slot=0;
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
        }
        else if ((hdr & 0xc0000000) == 0x40000000) {
            // pulse parameter 2
        }
        else if ((hdr & 0xc0000000) == 0x00000000) {
            // pulse parameter 3
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
        else if ((hdr & 0xf8000000) == 0xa0000000 ||
                 (hdr & 0xf8000000) == 0xf0000000) {
            chan = ((hdr & 0x7800000) >> 23);
            int nsamples = (hdr & 0xfff);
            int n = trow.nhit++;
            trow.hcrate[n] = crate;
            trow.hslot[n] = slot;
            trow.hch[n] = chan;
            trace[chan]->Reset();
            trow.nsamp[n] = nsamples;
            int istart = i+1;
            int istop = istart + nsamples/2;
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
        else {
            printf("skipping %x:", hdr);
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

    factrl.set_dac_levels(slot, 100);

    TFile fout("faScope.root", "recreate");
    tree1 = new TTree("tree1", "TAGM FADC tree");
    build_tree1(tree1);

    for (int i=0; i<16; ++i) {
        std::stringstream name;
        name << "t" << i;
        std::stringstream title;
        title << "slot " << slot << " channel " << channel;
        trace[i] = new TH1D(name.str().c_str(), title.str().c_str(),
                            500, 0, 2000);
        trace[i]->SetStats(0);
        trace[i]->GetXaxis()->SetTitle("t (ns)");
        trace[i]->GetYaxis()->SetTitle("V (adc)");
        trace[i]->GetYaxis()->SetTitleOffset(1.4);
    }

    int events = 28;
    int bufsize = events * 4100;
    unsigned int *data = (unsigned int *)malloc(bufsize * sizeof(int));
    for (int i=0; i<nevents || nevents==0; i+=events) {
        int nrec = factrl.acquire(slot, events, bufsize, data);
        decode(nrec, data);
        if (nevents == 0) {
            if (sock == 0)
                sock = new TServerSocket("mbus", kTRUE);
            TSocket *conn = sock->Accept();
            TMessage msg(kMESS_OBJECT);
            msg.WriteObject(trace[channel]);
            conn->Send(msg);
        }
    }

    tree1->Write();
    return 0;
}
