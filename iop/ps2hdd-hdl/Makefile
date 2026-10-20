# ps2hdd-hdl.irx: o driver APA do PS2SDK (codigo sem modificacao, ver
# PS2SDK-COMMIT e LICENSE-PS2SDK) compilado com suporte a HDLoader:
#
#   APA_SUPPORT_HDL                criar particoes do tipo "HDL" (0x1337)
#   APA_SUPPORT_IOCTL_GETPARTSTART ioctl2 HIOCGETPARTSTART (setor inicial
#                                  de cada subparticao)
#   APA_STAT_RETURN_PART_LBA       stat/dread devolvem o LBA da particao
#                                  em private_5 (o OPL usa isso)
#
# No PS2SDK essas opcoes so existem na versao POSIX (ferramentas de PC).

IOP_BIN = ps2hdd-hdl.irx

IOP_SRC_DIR = src/
IOP_OBJS_DIR = obj/

LIBAPA_SRC_DIR = libapa/src/

IOP_INCS += -Ilibapa/include -Isrc

IOP_CFLAGS += -DAPA_USE_DEV9 -DAPA_USE_ATAD \
	-DIOMANX_OLD_NAME_COMPATIBILITY=0 -DIOMANX_OLD_NAME_ADDDELDRV=0 \
	-DAPA_SUPPORT_HDL=1 \
	-DAPA_SUPPORT_IOCTL_GETPARTSTART=1 \
	-DAPA_STAT_RETURN_PART_LBA=1

IOP_OBJS = hdd_fio.o hdd_blkio.o hdd.o imports.o \
	password.o misc.o cache.o apa.o journal.o free.o

all: $(IOP_BIN)

clean:
	rm -rf $(IOP_OBJS_DIR) $(IOP_BIN) *.elf

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal

$(IOP_OBJS_DIR)%.o: $(LIBAPA_SRC_DIR)%.c
	$(DIR_GUARD)
	$(IOP_C_COMPILE) -c $< -o $@
