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
TButton *next=0;
TSocket *sock=0;
std::string crate("roctagm1");

TLine *line[16] = {0};
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
                if (line[p])
                    delete line[p];
                double ymin = histo->GetMinimum();
                line[p] = new TLine(time/16., ymin, time/16., peak);
                line[p]->SetLineColor(kRed);
                line[p]->Draw();
            }
            if (c2) {
                c2->cd();
                pulse->Draw();
                c2->Update();
                c1->cd();
                c1->Update();
            }
        }
        next = new TButton("next","faScopeNext()",.02,.92,.12,.98);
        next->Draw();
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
    if (next == 0) {
        return faScopeDisplay(firstbin, lastbin);
    }
}

void faScopeNext()
{
    if (next) {
        delete next;
        next = 0;
    }
    //faScopeDisplay(firstbin, lastbin);
}
