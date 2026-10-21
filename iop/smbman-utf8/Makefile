# smbman-utf8.irx: o SMBMAN do PS2SDK (commit em PS2SDK-COMMIT, licenca
# em LICENSE-PS2SDK) com uma unica mudanca, em src/smb.c: os nomes
# trocados com o servidor em UTF-16 viram UTF-8 no EE (o original so
# guardava ASCII e trocava o resto por '?', e as pastas com acento nao
# abriam). Substitui o smbman.irx (mesmo simbolo SMBMAN_irx).

IOP_BIN = smbman-utf8.irx

IOP_SRC_DIR = src/
IOP_OBJS_DIR = obj/

IOP_INCS += -Isrc

IOP_LIBS += -lgcc

IOP_OBJS = smbman.o smb_fio.o smb.o poll.o auth.o des.o md4.o imports.o

all: $(IOP_BIN)

clean:
	rm -rf $(IOP_OBJS_DIR) $(IOP_BIN) *.elf

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal
