#-----------------------------------------------------------------------------
#  Copyright (c) 2010 Southeastern Universities Research Association,
#                          Continuous Electron Beam Accelerator Facility
# 
#  This software was developed under a United States Government license
#  described in the NOTICE file included as part of this distribution.
# 
#  CEBAF Data Acquisition Group, 12000 Jefferson Ave., Newport News, VA 23606
#  Email: coda@cebaf.gov  Tel: (804) 249-7101  Fax: (804) 249-7363
# -----------------------------------------------------------------------------
#  
#  Description:  Makefile for JLAB 16 Channel Discriminator/Scaler VME Module
# 	
# 	
#  Author:  Bryan Moffit, TJANF Data Acquisition Group
# 
#  SVN: $Rev$
#
#
DEBUG=1

ifndef ARCH
	ifdef LINUXVME_LIB
		ARCH=Linux
	else
		ARCH=VXWORKSPPC
	endif
endif

ifndef OSTYPE
  OSTYPE := $(subst -,_,$(shell uname))
endif


ifeq ($(OSTYPE),SunOS)
LIBS = 
EXTRA =
endif

ifndef ARCH
  ARCH=VXWORKSPPC
endif

# Defs and build for VxWorks
ifeq ($(ARCH),VXWORKSPPC)

VXWORKS_ROOT = /site/vxworks/5.5/ppc/target

DEFS   = -w -Wall -mcpu=604 -DCPU=PPC604 -DVXWORKS -D_GNU_TOOL -mlongcall -fno-for-scope -fno-builtin -fvolatile -DVXWORKSPPC
INCS   = -I. -I$(VXWORKS_ROOT)/h -I$(VXWORKS_ROOT)/h/rpc -I$(VXWORKS_ROOT)/h/net
CC     = ccppc $(INCS) $(DEFS)
LD     = ldppc

# explicit targets

all: vmeDSClib.o

clean:
	rm -f vmeDSClib.o *~

vmeDSClib.o: vmeDSClib.c vmeDSClib.h
	$(CC) -c vmeDSClib.c

endif

# Defs and build for Linux
ifeq ($(ARCH),Linux)

LINUXVME_LIB		?= ${CODA}/linuxvme/libs
LINUXVME_INC		?= ${CODA}/linuxvme/include

CROSS_COMPILE           = 
CC			= $(CROSS_COMPILE)gcc
AR                      = ar
RANLIB                  = ranlib
CFLAGS			= -I. -I${LINUXVME_INC} -I/usr/include \
			  -L${LINUXVME_LIB} -L.
ifdef DEBUG
CFLAGS			+= -Wall -g
else
CFLAGS			+= -O2
endif

OBJS			= vmeDSClib.o

LIBS			= libvmeDSC.a

all: $(LIBS) links

libvmeDSC.a: vmeDSClib.o
	$(CC) -fpic -shared $(CFLAGS) -o libvmeDSC.so vmeDSClib.c
	$(AR) ruv libvmeDSC.a vmeDSClib.o
	$(RANLIB) libvmeDSC.a

clean distclean:
	@rm -f $(OBJS) $(LIBS) *.so *~

links: libvmeDSC.a
	ln -sf $(PWD)/libvmeDSC.a $(LINUXVME_LIB)/libvmeDSC.a
	ln -sf $(PWD)/libvmeDSC.so $(LINUXVME_LIB)/libvmeDSC.so
	ln -sf $(PWD)/vmeDSClib.h $(LINUXVME_INC)/vmeDSClib.h

vmeDSClib.o: vmeDSClib.c vmeDSClib.h Makefile
	$(CC) $(CFLAGS) -c -o $@ $(@:%.o=%.c) $(LIBS_$@) 

rol:
	make -f Makefile-rol

rolclean:
	make -f Makefile-rol clean

.PHONY: all clean distclean

endif
