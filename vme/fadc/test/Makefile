
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


ifeq ($(OSNAME),SunOS)
CODALIB = $(CODA)/$(OSNAME)/lib
CODAINC = $(CODA)/common/include
CFLAGS = -O $(DEFS)
endif

ifeq ($(OSNAME),Linux)

ifndef LINUXVME_LIB
	LINUXVME_LIB	= $CODA/extensions/linuxvme/libs
endif
ifndef LINUXVME_INC
	LINUXVME_INC	= $CODA/extensions/linuxvme/include
endif

CODALIB = $(CODA)/$(OSNAME)/lib
CODAINC = $(CODA)/common/include
CFLAGS = -O $(DEFS) -Wall -O2 \
		-I${LINUXVME_INC} -I. \
		-L${LINUXVME_LIB} -L. -DJLAB -g
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

PROGS =  fadcLibTest fadcReadoutTest historyBufferTest faInternalTrig

all: echoarch $(PROGS)

fadcLibTest: fadcLibTest.c
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) $(LIBS_$@) -lrt -ljvme -lfadc

faInternalTrig: faInternalTrig.c
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) $(LIBS_$@) -lrt -ljvme -lfadc -lflexio

fadcReadoutTest: fadcReadoutTest.c
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) $(LIBS_$@) -lrt -ljvme -lfadc -lti -lsd -lts

historyBufferTest: historyBufferTest.c
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) $(LIBS_$@) -lrt -ljvme -lfadc -lti -lsd -lts

%: %.c
	echo "Making $@"
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) -lrt -ljvme -lti -lfadc

clean:
	rm -f *~ $(PROGS)

echoarch:
	echo "Make for $(ARCH)"
