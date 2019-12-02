# File:
#    Makefile
#
# Description:  
#	Makefile for fADC250 firmware updating code (Linux ony)
#
# SVN: $Rev$
#
#
ARCH=Linux
DEBUG=1

#Check Operating system we are using
ifndef OSNAME
  OSNAME := $(subst -,_,$(shell uname))
endif

FWFILE_CTRL = $(shell cat .currentfw_ctrl)
FWFILE_PROC = $(shell cat .currentfw_proc)

FWFILES = ${FWFILE_CTRL} ${FWFILE_PROC}

LINUXVME_LIB	?= ${CODA}/linuxvme/lib
LINUXVME_INC	?= ${CODA}/linuxvme/include
LINUXVME_BIN    ?= ${CODA}/linuxvme/bin
LINUXVME_FW     ?= ${CODA}/linuxvme/firmware

CODALIB = $(CODA)/$(OSNAME)/lib
CODAINC = $(CODA)/common/include
CFLAGS	= -O $(DEFS) \
		-I${LINUXVME_INC} -I. \
		-L${LINUXVME_LIB} -L. -DJLAB -lrt -ljvme -lfadc 
ifdef DEBUG
CFLAGS	+= -Wall -g
else
CFLAGS	+= -O2
endif

PROGS	= fadcFirmwareUpdate fadcGFirmwareUpdate

all: echoarch $(PROGS)


fadcFirmwareUpdate: fadcFirmwareUpdate.c
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) -lrt -ljvme -lfadc

fadcGFirmwareUpdate: fadcGFirmwareUpdate.c
	$(CC) $(CFLAGS) -o $@ $(@:%=%.c) -lrt -ljvme -lfadc

install: $(PROGS)
	@echo "Installing $(PROGS)"
	@install -v -d $(LINUXVME_BIN)
	@install -v -p $(PROGS) -D $(LINUXVME_BIN)/

fwinstall:
	@ln -sf $(FWFILE_CTRL) fadc_ctrl_current_firmware.mcs
	@ln -sf $(FWFILE_PROC) fadc_proc_current_firmware.mcs
	@install -v -d $(LINUXVME_FW)/fadc/v2.0
	@install -p -m 664 -v $(FWFILES) \
		fadc_ctrl_current_firmware.mcs fadc_proc_current_firmware.mcs \
		-D $(LINUXVME_FW)/fadc/v2.0/

clean:
	rm -f *~ $(PROGS)

echoarch:
	@echo "Make for $(ARCH)"
