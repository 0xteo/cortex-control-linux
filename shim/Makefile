MINGW_CC ?= x86_64-w64-mingw32-gcc
CFLAGS ?= -O2 -Wall -Wno-attributes

all: cfgmgr32.dll test-enum.exe

cfgmgr32.dll: cfgmgr32.c cfgmgr32.def
	$(MINGW_CC) $(CFLAGS) -shared -o $@ cfgmgr32.c cfgmgr32.def -lsetupapi -static-libgcc

test-enum.exe: test-enum.c
	$(MINGW_CC) $(CFLAGS) -o $@ test-enum.c

clean:
	rm -f cfgmgr32.dll test-enum.exe

.PHONY: all clean
