#
# File:
#    Makefile
#
# Description:
#    Makefile for Linux programs testing the vmeDSC Library
#
#
# $Date$
# $Rev$
#

#LINUXVME_LIB	?= ${CODA}/extensions/linuxvme/libs
#LINUXVME_INC	?= ${CODA}/extensions/linuxvme/include

ifndef LINUXVME_LIB
	LINUXVME_LIB	= $(CODA)/linuxvme/libs
endif
ifndef LINUXVME_INC
	LINUXVME_INC	= $(CODA)/linuxvme/include
endif

CODALIB = $(CODA)/$(OSNAME)/lib

CROSS_COMPILE		=
CC			= $(CROSS_COMPILE)gcc
AR                      = ar
RANLIB                  = ranlib
CFLAGS			= -Wall -O2 -I${LINUXVME_INC} -I. -I/usr/include -I../vmeDSC \
                          -L${LINUXVME_LIB} -L.

PROGS			= vmeDSCLibTest vmeDSCSetSerialInfo \
			vmeDSCReadoutTest vmeDSCSetThresholds vmeDSCGetThresholds

all: $(PROGS)

clean distclean:
	@rm -f $(PROGS) *~ *.so

%: %.c
	echo "Making $@"
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) $(LIBS_$@) -lrt -L$(CODA)/linuxvme/jvme -ljvme -L../vmeDSC -lvmeDSC

.PHONY: all clean distclean
