//
// faScopeDisplay.C - root macro to display raw-mode (mode 10) traces 
//                    from a fadc250 module in a frontend vme crate,
//                    acting as a client communicating with a faScope
//                    server running on a frontend roc.
//
// author: richard.t.jones at uconn.edu
// version: december 5, 2019
//
// Usage:
//        $ root -l
//        [root] .x faScopeDisplay.C++
//        ... see faScope traces appear in the graphcs window,
//        ... press the NEXT button in the upper left corner of
//        ... the graphics window to advance to the next trace,
//        ... then double click anywhere in the graphics window
//        ... (not on the NEXT button) to exit the loop.
//
// Notes:
// 1. faScopeDisplay needs to run in an interactive root session running
//    a version of root that is binary compatible with what was used to
//    build and run the faScope frontend server. It was tested using
//    root v5.34, which is the most recent build of root that I could find
//    on the roc. If there is a mismatch between the versions of root used
//    to build faScope and to run faScopeDisplay, the transfer of objects
//    over the network connection will not work!!
//

#include <iostream>

#include <TROOT.h>
#include <TH1I.h>
#include <TSocket.h>
#include <TMessage.h>
#include <TButton.h>
#include <TCanvas.h>
#include <TLine.h>

TH1I *histo=0;
TH1I *pulse=0;
TButton *nextbut=0;
TSocket *sock=0;
std::string crate("roctagm1");

unsigned int PTW = 500;
unsigned int NSB = 3;
unsigned int NSA = 15;
unsigned int NP = 4;
unsigned int NPED = 5;
unsigned int MAXPED = 600;
unsigned int NSAT = 2;

TCanvas *c1 = new TCanvas("c1", "faScope", 10, 50, 600, 500);
//TCanvas *c2 = new TCanvas("c2", "pulse parameters", 10, 650, 600, 500);
TCanvas *c2 = 0;

void faScopeNext();

void faScopeDisplay(int firstbin=1, int lastbin=500)
{
    if (sock == 0) {
        sock = new TSocket(crate.c_str(), 47000);
        if (sock == 0)
            return;
    }
    else
        return;
    TMessage *msg=0;
    sock->Recv(msg);
    if (msg) {
        histo = (TH1I*)msg->ReadObject(msg->GetClass());
        pulse = (TH1I*)msg->ReadObject(msg->GetClass());
        if (histo) {
            histo->GetXaxis()->SetRange(firstbin, lastbin);
            std::string title(histo->GetTitle());
            histo->SetTitle((crate + "" + title).c_str());
            histo->Draw();
            c1->Update();
        }
        if (pulse) {
            int event = pulse->GetBinContent(1);
            int chan = pulse->GetBinContent(2);
            int qf = pulse->GetBinContent(3);
            int pedsum = pulse->GetBinContent(4);
            int np = pulse->GetBinContent(5);
            for (int p=0; p<np; ++p) {
                int pi = int(pulse->GetBinContent(6+p*10));
                int late = int(pulse->GetBinContent(7+p*10));
                int oflow = int(pulse->GetBinContent(8+p*10));
                int uflow = int(pulse->GetBinContent(9+p*10));
                int width = int(pulse->GetBinContent(10+p*10));
                int time = int(pulse->GetBinContent(11+p*10));
                int peak = int(pulse->GetBinContent(12+p*10));
                int tout = int(pulse->GetBinContent(13+p*10));
                int bogus = int(pulse->GetBinContent(14+p*10));
                int pedbad = int(pulse->GetBinContent(15+p*10));
                static TH1I *hpulse[4] = {0,0,0,0};
                if (hpulse[p])
                    delete hpulse[p];
                hpulse[p] = (TH1I*)histo->Clone();
                hpulse[p]->SetFillColor(kRed-10);
                hpulse[p]->GetXaxis()->SetRangeUser(time/16.-NSB*4, time/16.+NSA*4);
                hpulse[p]->Draw("same");
                static TLine *line1[4] = {0,0,0,0};
                if (line1[p])
                    delete line1[p];
                double ymin = histo->GetMinimum();
                line1[p] = new TLine(time/16., ymin, time/16., peak);
                line1[p]->SetLineColor(kRed);
                line1[p]->Draw();
                static TLine *line2[4] = {0,0,0,0};
                if (line2[p])
                    delete line2[p];
                line2[p] = new TLine(time/16.-36, pedsum/(NPED+1e-99),
                                     time/16., pedsum/(NPED+1e-99));
                if (pedbad == 0)
                    line2[p]->SetLineColor(kRed);
                line2[p]->SetLineWidth(5);
                line2[p]->Draw();
            }
            if (c2) {
                c2->cd();
                pulse->Draw();
                c2->Update();
                c1->cd();
                c1->Update();
            }
        }
        nextbut = new TButton("next","faScopeNext()",.02,.92,.12,.98);
        nextbut->Draw();
        delete msg;
    }
    if (sock) {
       delete sock;
       sock = 0;
    }
    c1->Update();
    gPad->WaitPrimitive();
    firstbin = histo->GetXaxis()->GetFirst();
    lastbin = histo->GetXaxis()->GetLast();
    if (nextbut == 0) {
        return faScopeDisplay(firstbin, lastbin);
    }
}

void faScopeNext()
{
    if (nextbut) {
        delete nextbut;
        nextbut = 0;
    }
    //faScopeDisplay(firstbin, lastbin);
}
