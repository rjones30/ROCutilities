#include <iostream>

#include <TROOT.h>
#include <TH1D.h>
#include <TSocket.h>
#include <TMessage.h>
#include <TButton.h>
#include <TCanvas.h>

TH1D *histo=0;
TButton *next=0;
TSocket *sock=0;
std::string crate("roctagm1");

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
        histo = (TH1D*)msg->ReadObject(msg->GetClass());
        if (histo) {
            histo->GetXaxis()->SetRange(firstbin, lastbin);
            std::string title(histo->GetTitle());
            histo->SetTitle((crate + "" + title).c_str());
            histo->Draw();
        }
        if (next == 0) {
            next = new TButton("next","faScopeNext()",.80,.92,.95,.98);
            next->Draw();
        }
        TCanvas *c1 = (TCanvas*)gROOT->FindObject("c1");
        if (c1)
            c1->Update();
        delete msg;
    }
    if (sock) {
       delete sock;
       sock = 0;
    }
}

void faScopeNext()
{
    if (next) {
       delete next;
       next = 0;
    }
    int firstbin = histo->GetXaxis()->GetFirst();
    int lastbin = histo->GetXaxis()->GetLast();
    faScopeDisplay(firstbin, lastbin);
}
