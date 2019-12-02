#
# File:
#    Makefile
#
# Description:
#    Makefile for the fADC250 (v2) Library using a VME Controller running vxWorks or Linux
#
# SVN: $Rev$
#
# Uncomment DEBUG line, to include some debugging info ( -g and -Wall)
DEBUG=1
#
ARCH=Linux
ifndef ARCH
	ifdef LINUXVME_LIB
		ARCH=Linux
	else
		ARCH=VXWORKSPPC
	endif
endif

# Defs and build for VxWorks
ifeq ($(ARCH),VXWORKSPPC)
VXWORKS_ROOT = /site/vxworks/5.5/ppc/target
VME_INCLUDE             ?= -I$(LINUXVME_INC)

CC			= ccppc
LD			= ldppc
DEFS			= -mcpu=604 -DCPU=PPC604 -DVXWORKS -D_GNU_TOOL -mlongcall \
				-fno-for-scope -fno-builtin -fvolatile -DVXWORKSPPC
INCS			= -I. -I$(VXWORKS_ROOT)/h -I$(VXWORKS_ROOT)/h/rpc -I$(VXWORKS_ROOT)/h/net \
			$(VME_INCLUDE)
CFLAGS			= $(INCS) $(DEFS)

endif #ARCH=VXWORKSPPC#

# Defs and build for Linux
ifeq ($(ARCH),Linux)
LINUXVME_LIB		?= ../jvme
LINUXVME_INC		?= ../jvme

CC			= gcc
AR                      = ar
RANLIB                  = ranlib
CFLAGS			= -L. -L${LINUXVME_LIB}
INCS			= -I. -I${LINUXVME_INC} 

LIBS			= libfadc.a
endif #ARCH=Linux#

ifdef DEBUG
CFLAGS			+= -Wall -g
else
CFLAGS			+= -O2
endif
SRC			= fadcLib.c
HDRS			= $(SRC:.c=.h)
OBJ			= $(SRC:.c=.o)
DEPS			= $(SRC:.c=.d)

ifeq ($(ARCH),Linux)
all: echoarch $(LIBS)
else
all: echoarch $(OBJ)
endif

$(OBJ): $(SRC) $(HDRS)
	$(CC) $(CFLAGS) $(INCS) -c -o $@ $(SRC)

$(LIBS): $(OBJ)
	$(CC) -fpic -shared $(CFLAGS) $(INCS) -o $(@:%.a=%.so) $(SRC)
	$(AR) ruv $@ $<
	$(RANLIB) $@

ifeq ($(ARCH),Linux)
links: $(LIBS)
	@ln -vsf $(PWD)/$< $(LINUXVME_LIB)/$<
	@ln -vsf $(PWD)/$(<:%.a=%.so) $(LINUXVME_LIB)/$(<:%.a=%.so)
	@ln -vsf ${PWD}/*Lib.h $(LINUXVME_INC)

install: $(LIBS)
	@cp -v $(PWD)/$< $(LINUXVME_LIB)/$<
	@cp -v $(PWD)/$(<:%.a=%.so) $(LINUXVME_LIB)/$(<:%.a=%.so)
	@cp -v ${PWD}/fadcLib.h $(LINUXVME_INC)

%.d: %.c
	@echo "Building $@ from $<"
	@set -e; rm -f $@; \
	$(CC) -MM $(INCS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

-include $(DEPS)

endif

clean:
	@rm -vf fadcLib.{o,d} libfadc.{a,so}

echoarch:
	@echo "Make for $(ARCH)"

.PHONY: clean echoarch
