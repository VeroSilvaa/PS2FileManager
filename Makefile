EE_BIN = PS2FileManager.elf

EE_OBJS = src/main.o src/memorycard.o src/filemanager.o src/clipboard.o src/fm_fileio.o src/network.o src/smb.o src/dev9.o src/hdd.o src/usb.o src/iso9660.o src/hdl.o src/osk.o

# Modulos IOP do PS2SDK embutidos no ELF (mesmo modelo do wLaunchELF)
IRX_OBJS = irx/iomanX_irx.o irx/fileXio_irx.o irx/mcman_irx.o irx/mcserv_irx.o

# Rede + SMB (mesmos modulos e nomes de simbolo do PS2DVDPlayer)
IRX_OBJS += irx/DEV9_irx.o irx/NETMAN_irx.o irx/SMAP_irx.o irx/PS2IP_irx.o irx/PS2IPS_irx.o irx/SMBMAN_irx.o

# HDD interno: ATAD + APA + PFS
IRX_OBJS += irx/PS2ATAD_irx.o irx/PS2HDD_irx.o irx/PS2FS_irx.o

# HDLoader: gravacao dos dados do jogo (modulo proprio, iop/hdlwrite)
IRX_OBJS += irx/HDLWRITE_irx.o

# USB: pilha USB + BDM + FAT (massN:)
IRX_OBJS += irx/USBD_irx.o irx/BDM_irx.o irx/BDMFS_FATFS_irx.o irx/USBMASS_BD_irx.o
EE_OBJS += $(IRX_OBJS)

EE_INCS = -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include -Iinclude

EE_LIBS = -lpad -lmc -lfileXio -lnetman -lps2ips -lpatches -ldebug -lkernel

EE_CFLAGS = -O2 -G0 -Wall

all: $(EE_BIN)

irx/%_irx.c: $(PS2SDK)/iop/irx/%.irx
	@mkdir -p irx
	bin2c $< $@ $*_irx

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

irx/SMBMAN_irx.c: $(PS2SDK)/iop/irx/smbman.irx
	@mkdir -p irx
	bin2c $< $@ SMBMAN_irx

irx/PS2ATAD_irx.c: $(PS2SDK)/iop/irx/ps2atad.irx
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

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
