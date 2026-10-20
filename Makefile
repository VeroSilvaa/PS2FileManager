EE_BIN = PS2FileManager.elf

EE_OBJS = src/main.o src/memorycard.o src/filemanager.o src/clipboard.o src/fm_fileio.o

# Modulos IOP do PS2SDK embutidos no ELF (mesmo modelo do wLaunchELF)
IRX_OBJS = irx/iomanX_irx.o irx/fileXio_irx.o irx/mcman_irx.o irx/mcserv_irx.o

EE_OBJS += $(IRX_OBJS)

EE_INCS = -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include -Iinclude

EE_LIBS = -lpad -lmc -lfileXio -lpatches -ldebug -lkernel

EE_CFLAGS = -O2 -G0 -Wall

all: $(EE_BIN)

irx/%_irx.c: $(PS2SDK)/iop/irx/%.irx
	@mkdir -p irx
	bin2c $< $@ $*_irx

clean:
	rm -f $(EE_BIN) $(EE_OBJS)
	rm -rf irx

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal