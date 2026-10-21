EE_BIN = PS2FileManager.elf

EE_OBJS = src/main.o src/memorycard.o src/filemanager.o src/clipboard.o src/fm_fileio.o src/network.o src/smb.o src/dev9.o src/hdd.o src/usb.o src/iso9660.o src/hdl.o src/osk.o src/config.o src/settings.o src/usbfmt.o src/bdmcore.o src/id3.o src/mp3dec.o src/mp3player.o src/elfrun.o src/console.o src/gfx.o src/pngload.o

# Modulos IOP do PS2SDK embutidos no ELF (mesmo modelo do wLaunchELF)
IRX_OBJS = irx/iomanX_irx.o irx/fileXio_irx.o irx/mcman_irx.o irx/mcserv_irx.o

# Rede + SMB (mesmos modulos e nomes de simbolo do PS2DVDPlayer)
IRX_OBJS += irx/DEV9_irx.o irx/NETMAN_irx.o irx/SMAP_irx.o irx/PS2IP_irx.o irx/PS2IPS_irx.o irx/SMBMAN_irx.o

# HDD interno: ATAD (ata_bd) + APA + PFS
IRX_OBJS += irx/PS2ATAD_irx.o irx/PS2HDD_irx.o irx/PS2FS_irx.o

# HDLoader: gravacao dos dados do jogo (modulo proprio, iop/hdlwrite)
IRX_OBJS += irx/HDLWRITE_irx.o

# USB: pilha USB + BDM + FAT (massN:)
IRX_OBJS += irx/USBD_irx.o irx/BDM_irx.o irx/BDMFS_FATFS_irx.o irx/USBMASS_BD_irx.o

# Formatacao FAT32/exFAT e tabela MBR/GPT (modulo proprio, iop/fatfmt)
IRX_OBJS += irx/FATFMT_irx.o

# MP3 player: saida de audio (o LIBSD vem da ROM do console)
IRX_OBJS += irx/AUDSRV_irx.o
EE_OBJS += $(IRX_OBJS)

# Interface grafica: PNGs de assets/ (tools/make_assets.sh) embutidos
ASSET_OBJS = irx/bg_png.o irx/font_mono_png.o irx/font_sans_png.o irx/logo_png.o irx/buttons_png.o
EE_OBJS += $(ASSET_OBJS)

EE_INCS = -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include -I$(PS2SDK)/ports/include \
          -I$(GSKIT)/include -Iinclude -Iassets -Ithird_party/minimp3

EE_LDFLAGS += -L$(GSKIT)/lib -L$(PS2SDK)/ports/lib

# Sem -ldebug: src/console.c substitui a debug screen (mesmas funcoes)
EE_LIBS = -lgskit -ldmakit -lpng -lz -lelf-loader-nocolour -laudsrv -lpad -lmc -lfileXio -lnetman -lps2ips -lpatches -lkernel

EE_CFLAGS = -O2 -G0 -Wall

all: $(EE_BIN)

# minimp3: constantes como float (o EE nao tem FPU de precisao dupla)
src/mp3dec.o: EE_CFLAGS += -fsingle-precision-constant

irx/%_irx.c: $(PS2SDK)/iop/irx/%.irx
	@mkdir -p irx
	bin2c $< $@ $*_irx

irx/bg_png.c: assets/bg_640x448.png
	@mkdir -p irx
	bin2c $< $@ bg_png

irx/font_mono_png.c: assets/font_mono_8x16.png
	@mkdir -p irx
	bin2c $< $@ font_mono_png

irx/font_sans_png.c: assets/font_sans_14.png
	@mkdir -p irx
	bin2c $< $@ font_sans_png

irx/logo_png.c: assets/logo.png
	@mkdir -p irx
	bin2c $< $@ logo_png

# Botoes: recortes do "Kit de Botões de Controle PlayStation.png"
irx/buttons_png.c: assets/buttons.png
	@mkdir -p irx
	bin2c $< $@ buttons_png

src/gfx.o: assets/font_sans_14.h assets/buttons.h

# Nome do arquivo diferente do simbolo (ps2ip-nm tem hifen)
irx/DEV9_irx.c: $(PS2SDK)/iop/irx/ps2dev9.irx
	@mkdir -p irx
	bin2c $< $@ DEV9_irx

irx/NETMAN_irx.c: $(PS2SDK)/iop/irx/netman.irx
	@mkdir -p irx
	bin2c $< $@ NETMAN_irx

irx/SMAP_irx.c: $(PS2SDK)/iop/irx/smap.irx
	@mkdir -p irx
	bin2c $< $@ SMAP_irx

irx/PS2IP_irx.c: $(PS2SDK)/iop/irx/ps2ip-nm.irx
	@mkdir -p irx
	bin2c $< $@ PS2IP_irx

irx/PS2IPS_irx.c: $(PS2SDK)/iop/irx/ps2ips.irx
	@mkdir -p irx
	bin2c $< $@ PS2IPS_irx

# SMBMAN proprio: o do PS2SDK com nomes em UTF-8 (iop/smbman-utf8)
SMBMAN_DIR = iop/smbman-utf8

$(SMBMAN_DIR)/smbman-utf8.irx: $(wildcard $(SMBMAN_DIR)/src/*) $(SMBMAN_DIR)/Makefile
	$(MAKE) -C $(SMBMAN_DIR)

irx/SMBMAN_irx.c: $(SMBMAN_DIR)/smbman-utf8.irx
	@mkdir -p irx
	bin2c $< $@ SMBMAN_irx

# ATAD com BDM (o HDD tambem aparece como disco "ata" no BDM): permite
# conferir os setores 0-1 antes de carregar o driver APA, como o OPL.
irx/PS2ATAD_irx.c: $(PS2SDK)/iop/irx/ata_bd.irx
	@mkdir -p irx
	bin2c $< $@ PS2ATAD_irx

# Driver APA proprio: o do PS2SDK compilado com suporte a HDLoader
# (iop/ps2hdd-hdl/Makefile).
HDD_HDL_DIR = iop/ps2hdd-hdl

$(HDD_HDL_DIR)/ps2hdd-hdl.irx: $(wildcard $(HDD_HDL_DIR)/src/* $(HDD_HDL_DIR)/libapa/*/*) $(HDD_HDL_DIR)/Makefile
	$(MAKE) -C $(HDD_HDL_DIR)

irx/PS2HDD_irx.c: $(HDD_HDL_DIR)/ps2hdd-hdl.irx
	@mkdir -p irx
	bin2c $< $@ PS2HDD_irx

HDLWRITE_DIR = iop/hdlwrite

$(HDLWRITE_DIR)/hdlwrite.irx: $(wildcard $(HDLWRITE_DIR)/src/*) $(HDLWRITE_DIR)/Makefile
	$(MAKE) -C $(HDLWRITE_DIR)

irx/HDLWRITE_irx.c: $(HDLWRITE_DIR)/hdlwrite.irx
	@mkdir -p irx
	bin2c $< $@ HDLWRITE_irx

FATFMT_DIR = iop/fatfmt

$(FATFMT_DIR)/fatfmt.irx: $(wildcard $(FATFMT_DIR)/src/* $(FATFMT_DIR)/fatfs/*) include/fatfmt_devctl.h $(FATFMT_DIR)/Makefile
	$(MAKE) -C $(FATFMT_DIR)

irx/FATFMT_irx.c: $(FATFMT_DIR)/fatfmt.irx
	@mkdir -p irx
	bin2c $< $@ FATFMT_irx

irx/AUDSRV_irx.c: $(PS2SDK)/iop/irx/audsrv.irx
	@mkdir -p irx
	bin2c $< $@ AUDSRV_irx

irx/PS2FS_irx.c: $(PS2SDK)/iop/irx/ps2fs.irx
	@mkdir -p irx
	bin2c $< $@ PS2FS_irx

irx/USBD_irx.c: $(PS2SDK)/iop/irx/usbd.irx
	@mkdir -p irx
	bin2c $< $@ USBD_irx

irx/BDM_irx.c: $(PS2SDK)/iop/irx/bdm.irx
	@mkdir -p irx
	bin2c $< $@ BDM_irx

irx/BDMFS_FATFS_irx.c: $(PS2SDK)/iop/irx/bdmfs_fatfs.irx
	@mkdir -p irx
	bin2c $< $@ BDMFS_FATFS_irx

irx/USBMASS_BD_irx.c: $(PS2SDK)/iop/irx/usbmass_bd.irx
	@mkdir -p irx
	bin2c $< $@ USBMASS_BD_irx

clean:
	rm -f $(EE_BIN) $(EE_OBJS)
	rm -rf irx
	$(MAKE) -C $(HDD_HDL_DIR) clean
	$(MAKE) -C $(HDLWRITE_DIR) clean
	$(MAKE) -C $(FATFMT_DIR) clean
	$(MAKE) -C $(SMBMAN_DIR) clean

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
