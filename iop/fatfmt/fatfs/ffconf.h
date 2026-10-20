/*---------------------------------------------------------------------------/
/  Configuracao do FatFs para o fatfmt.irx (PS2FileManager)
/
/  Baseada no ffconf.h do fork usado pelo PS2SDK (fjtrujy/FatFs, iop-r0.16).
/  Diferencas: so formatacao (f_mkfs) num volume por vez, sem relogio,
/  sem reentrancia. A leitura/gravacao normal continua no bdmfs_fatfs.
/---------------------------------------------------------------------------*/

#define FFCONF_DEF	80386	/* Revision ID */

/* Funcoes */
#define FF_FS_READONLY	0
#define FF_FS_MINIMIZE	0
#define FF_USE_FIND		0
#define FF_USE_MKFS		1	/* f_mkfs(): o motivo deste modulo */
#define FF_USE_FASTSEEK	0
#define FF_USE_EXPAND	0
#define FF_USE_CHMOD	0
#define FF_USE_LABEL	0
#define FF_USE_FORWARD	0
#define FF_USE_STRFUNC	0
#define FF_PRINT_LLI	0
#define FF_PRINT_FLOAT	0
#define FF_STRF_ENCODE	0

/* Locale: exFAT exige LFN; buffer estatico (um volume por vez) */
#define FF_CODE_PAGE	437
#define FF_USE_LFN		1
#define FF_MAX_LFN		255
#define FF_LFN_UNICODE	0
#define FF_LFN_BUF		255
#define FF_SFN_BUF		12
#define FF_FS_RPATH		0
#define FF_PATH_DEPTH	10

/* Volume: "0:" = a particao sendo formatada (diskio em fatfmt.c) */
#define FF_VOLUMES		1
#define FF_STR_VOLUME_ID	0
#define FF_VOLUME_STRS		"RAM","NAND","CF","SD","SD2","USB","USB2","USB3"
#define FF_MULTI_PARTITION	0
#define FF_MIN_SS		512
#define FF_MAX_SS		512
#define FF_LBA64		1
#define FF_MIN_GPT		0x10000000
#define FF_USE_TRIM		0

/* Sistema */
#define FF_FS_TINY		1
#define FF_FS_EXFAT		1
#define FF_FS_NORTC		1
#define FF_NORTC_MON	1
#define FF_NORTC_MDAY	1
#define FF_NORTC_YEAR	2026
#define FF_FS_CRTIME	0
#define FF_FS_NOFSINFO	0
#define FF_FS_LOCK		0
#define FF_FS_REENTRANT	0
#define FF_FS_TIMEOUT	1000
#define FF_FS_MBR 0

/*--- End of configuration options ---*/
