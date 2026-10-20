#ifndef PS2FILEMANAGER_USB_H
#define PS2FILEMANAGER_USB_H

/*
 * USB (Etapa 6): pendrives FAT pelo BDM
 * (usbd -> bdm -> bdmfs_fatfs -> usbmass_bd).
 *
 * - "usb0:/", "usb1:/"   uma unidade por particao FAT de pendrive,
 *                        na ordem em que o BDM as encontra
 *
 * "usbN:" (e nao "massN:"): o bdmfs_fatfs registra um nome por tipo
 * de disco. O "mass" numera todos os volumes, entao um HDD em exFAT
 * (ata_bd) viraria mass0: e empurraria os pendrives.
 */

#define USB_UNITS 2
#define USB_PREFIX "usb"

/*
 * Carrega os modulos (uma unica vez) e espera os pendrives
 * serem detectados. Negativo se os modulos falharam.
 */
int usb_init(void);

/*
 * 1 se "massN:/" abre (pendrive presente e montado).
 */
int usb_available(int unit);

int usb_ready(void);

/*
 * Carrega o fatfmt.irx (formatacao) na primeira chamada, com a
 * pilha USB ja carregada. 1 = disponivel.
 */
int usb_format_loaded(void);

/*
 * Depois de mudar tabela/formato: espera o BDM montar de novo
 * (ate USB_WAIT_SECONDS). Devolve quantas unidades montaram.
 */
int usb_wait_mount(void);

const char *usb_error(void);

#endif
