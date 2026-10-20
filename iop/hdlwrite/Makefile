# hdlwrite.irx: grava/le os dados de um jogo HDLoader nas fatias de
# uma particao APA (ver src/hdlwrite.c).

IOP_BIN = hdlwrite.irx

IOP_SRC_DIR = src/
IOP_OBJS_DIR = obj/

IOP_INCS += -Isrc

IOP_CFLAGS += -DIOMANX_OLD_NAME_COMPATIBILITY=0 -DIOMANX_OLD_NAME_ADDDELDRV=0

IOP_OBJS = hdlwrite.o imports.o

all: $(IOP_BIN)

clean:
	rm -rf $(IOP_OBJS_DIR) $(IOP_BIN) *.elf

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal
