setenv OSNAME Linux
#setenv CODA /gluex/coda/3.07_new
setenv CODA /gluex/coda/3.09
if (`uname -a` !~ *"i386"*) then
    setenv ROOTSYS /apps/root/5.34.21
else
    setenv ROOTSYS /apps/root/PRO
endif
setenv LD_LIBRARY_PATH /home/jonesrt/online/ROCutilities/vme/jvme':'/home/jonesrt/online/ROCutilities/vme/fadc:/home/jonesrt/online/ROCutilities/vmeDSC:$ROOTSYS/lib
setenv PATH /home/jonesrt/bin:/usr/local/bin:/bin:/usr/bin:$ROOTSYS/bin
