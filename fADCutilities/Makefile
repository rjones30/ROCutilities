
# Description:  Makefile for fadcLib.o
#   This driver is specific to VxWorks BSPs and must be compiled
#   with access to vxWorks headers.
#
ARCH=Linux


#Check Operating system we are using
ifndef OSNAME
  OSNAME := $(subst -,_,$(shell uname))
endif

ifndef ARCH
  ARCH = VXWORKSPPC
endif

ifndef WIND_BASE
  WIND_BASE = /site/vxworks/5.5/ppc
endif

ROOTSYS = /apps/root/PRO
ROOTLIBS = $(shell $(ROOTSYS)/bin/root-config --libs) -lXMLIO

ifeq ($(OSNAME),SunOS)
CODALIB = $(CODA)/$(OSNAME)/lib
CODAINC = $(CODA)/common/include
CFLAGS = -O $(DEFS)
endif

ifeq ($(OSNAME),Linux)

ifndef LINUXVME_LIB
	LINUXVME_LIB	= $(CODA)/linuxvme/libs
endif
ifndef LINUXVME_INC
	LINUXVME_INC	= $(CODA)/linuxvme/include
endif

CODALIB = $(CODA)/$(OSNAME)/lib
CODAINC = $(CODA)/common/include

USE_NEW_CODA = 1
ifndef USE_NEW_CODA
	CFLAGS = -O $(DEFS) -Wall -O2 \
		-I. \
		-I$(CODA)/linuxvme/include \
		-I/apps/root/PRO/root/include \
		-L. -g \
		-L$(CODA)/linuxvme/lib
else
	CFLAGS = -O $(DEFS) -Wall -O2 \
		-g -I. \
		-I../vme/jvme \
		-I../vme/fadc \
		-I/apps/root/PRO/root/include \
		-L. \
		-L../vme/jvme \
		-L../vme/fadc
endif
AR = ar
RANLIB = ranlib
endif

ifeq ($(ARCH),VXWORKSPPC)
INCDIR=$(WIND_BASE)/target/h -I../universeDma
CC = ccppc
LD = ldppc
DEFS = -mcpu=604 -DCPU=PPC604 -DVXWORKS -D_GNU_TOOL -DVXWORKSPPC
INCS = -fno-for-scope -fno-builtin -fvolatile -fstrength-reduce -mlongcall -I. -I$(CODAINC) -I$(INCDIR)
endif

ifeq ($(ARCH),VXWORKS68K51)
INCDIR=/site/vxworks/5.3/68k/target/h
CC = cc68k
DEFS = -DCPU=MC68040 -DVXWORKS -DVXWORKS68K51
INCS = -Wall -mc68020 -fvolatile -fstrength-reduce -nostdinc -I. -I$(INCDIR)
endif

PROGS = faSetThresholds faPrintThresholds faSetDAC faPrintDAC faCalibPedestals faCheckPedestals faTweakPedestals faPrintScalers faPrintScalerRates faMapRates \
        faPrintScalerRate1 faDoThresholdScan faPrintStatus faSet1Threshold faScope

all: echoarch $(PROGS)

faCalibPedestals: faCalibPedestals.c fadcLib_extensions.c
	$(CC) $(CFLAGS) -o $@ $^ -lrt -ljvme -lfadc

faCheckPedestals: faCheckPedestals.c fadcLib_extensions.c
	$(CC) $(CFLAGS) -o $@ $^ -lrt -ljvme -lfadc

faTweakPedestals: faTweakPedestals.c fadcLib_extensions.c
	$(CC) $(CFLAGS) -o $@ $^ -lrt -ljvme -lfadc

faMapRates: faMapRates.c fadcLib_extensions.c
	$(CC) $(CFLAGS) -o $@ $^ -lrt -ljvme -lfadc

fadcLibTest: fadcLibTest.c
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) $(LIBS_$@) -lrt -ljvme -lfadc

fadcReadoutTest: fadcReadoutTest.c
	#$(CC) $(CFLAGS) -o $@ $(@:%=%.c) $(LIBS_$@) -lrt -ljvme -lfadc -lti -lsd -lts
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) $(LIBS_$@) -lrt -ljvme -lfadc

historyBufferTest: historyBufferTest.c
	#$(CC) $(CFLAGS) -o $@ $(@:%=%.c) $(LIBS_$@) -lrt -ljvme -lfadc -lti -lsd -lts
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) $(LIBS_$@) -lrt -ljvme -lfadc

faScope: faScope.cc fadc250.o
	$(CXX) $(CFLAGS) -o $@ $^ -lrt -ljvme -lfadc -L$(ROOTSYS)/lib $(ROOTLIBS)

fadc250.o: fadc250.cc fadc250.hh
	$(CXX) -c $(CFLAGS) $<

%: %.c
	echo "Making $@"
	#$(CC) $(CFLAGS) -o $@ $(@:%=%.c) -lrt -ljvme -lti -lfadc
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) -lrt -ljvme -lfadc

clean:
	rm -f *~ $(PROGS)

echoarch:
	echo "Make for $(ARCH)"
