#ifndef PS2FILEMANAGER_USB_H
#define PS2FILEMANAGER_USB_H

/*
 * USB (Etapa 6): pendrives FAT pelo BDM
 * (usbd -> bdm -> bdmfs_fatfs -> usbmass_bd).
 *
 * - "mass0:/", "mass1:/" uma unidade por particao FAT, na
 *                        ordem em que o BDM as encontra
 */

#define USB_UNITS 2

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

const char *usb_error(void);

#endif
