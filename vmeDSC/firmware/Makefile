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

LINUXVME_LIB	?= ${CODA}/extensions/linuxvme/libs
LINUXVME_INC	?= ${CODA}/extensions/linuxvme/include

CROSS_COMPILE		=
CC			= $(CROSS_COMPILE)gcc
AR                      = ar
RANLIB                  = ranlib
CFLAGS			= -Wall -O2 -I. -I${LINUXVME_INC} \
			  -L. -L${LINUXVME_LIB}

PROGS			= vmeDSCUpdateFirmware

all: $(PROGS)

clean distclean:
	@rm -f $(PROGS) *~ *.so

%: %.c
	echo "Making $@"
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) $(LIBS_$@) -lrt -ljvme -lvmeDSC

.PHONY: all clean distclean
