# fatfmt.irx: tabela MBR/GPT e formatacao FAT32/exFAT de discos do BDM
# (ver src/fatfmt.c). FatFs em fatfs/ (licenca FatFs, LICENSE-FatFs.txt).

IOP_BIN = fatfmt.irx

IOP_SRC_DIR = src/
IOP_OBJS_DIR = obj/

IOP_INCS += -Isrc -Ifatfs -I../../include

IOP_CFLAGS += -DIOMANX_OLD_NAME_COMPATIBILITY=0 -DIOMANX_OLD_NAME_ADDDELDRV=0

IOP_OBJS = fatfmt.o ff.o ffunicode.o imports.o

# Divisao de 64 bits (LBA) do FatFs, como o bdmfs_fatfs do PS2SDK
IOP_LIBS += -lgcc

all: $(IOP_BIN)

clean:
	rm -rf $(IOP_OBJS_DIR) $(IOP_BIN) *.elf

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal

# Fontes do FatFs (fora de src/)
$(IOP_OBJS_DIR)%.o: fatfs/%.c
	$(DIR_GUARD)
	$(IOP_C_COMPILE) -c $< -o $@
