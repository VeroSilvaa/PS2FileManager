#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <debug.h>
#include <libpad.h>
#include <libmc.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
/*
 * fileXio so para fileXioInit(). Depois dele open/opendir/stat
 * do newlib passam pelo fileXio (regra do PS2SDK).
 */
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#undef NEWLIB_PORT_AWARE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

#include "memorycard.h"
#include "filemanager.h"
#include "clipboard.h"
#include "fm_fileio.h"
#include "network.h"
#include "smb.h"
#include "hdd.h"
#include "usb.h"
#include "iso9660.h"
#include "hdl.h"
#include "osk.h"
#include "config.h"
#include "settings.h"
#include "usbfmt.h"



/* ============================================================
 * Modulos IOP embutidos (PS2SDK, gerados por bin2c)
 * ============================================================ */

extern unsigned char iomanX_irx[];
extern unsigned int size_iomanX_irx;

extern unsigned char fileXio_irx[];
extern unsigned int size_fileXio_irx;

extern unsigned char mcman_irx[];
extern unsigned int size_mcman_irx;

extern unsigned char mcserv_irx[];
extern unsigned int size_mcserv_irx;


/* ============================================================
 * Configuração
 * ============================================================ */

#define SCREEN_WIDTH        640
#define SCREEN_HEIGHT       448

#define MAX_VISIBLE_FILES   18

#define MENU_ITEM_COUNT     10
#define MENU_HDL            8

/*
 * Nome de arquivo no Memory Card: ate 31 caracteres. Nos outros
 * dispositivos o limite e o que cabe numa linha da debug screen.
 */
#define MC_MAX_NAME         31
#define NAME_MAX_TYPED      60

/*
 * Instalacao HDLoader: jogos por vez.
 */
#define HDL_MAX_GAMES       16

/*
 * SMB (Etapa 8): servidor, share, usuario e senha vem da
 * configuracao (config.c, tela em settings.c).
 */
#define SMB_DHCP_TIMEOUT    20

#define DEVICE_COUNT        7
#define DEVICE_SMB          2
#define DEVICE_HDD          3
#define DEVICE_USB0         4   /* usb0:, DEVICE_USB0 + 1 = usb1: */
#define DEVICE_CONFIG       6   /* nao e dispositivo: tela de configuracao */


/* ============================================================
 * Estados da interface
 * ============================================================ */

typedef enum {
    SCREEN_DEVICES = 0,
    SCREEN_FILES,
    SCREEN_SETTINGS
} AppScreen;


/* ============================================================
 * Menu R1
 * ============================================================ */

static const char *operation_menu[MENU_ITEM_COUNT] = {
    "Copiar",
    "Recortar (mover)",
    "Colar",
    "Criar pasta",
    "Renomear",
    "Excluir",
    "Selecionar todos",
    "Desmarcar todos",
    "Instalar no HDD (HDLoader)",
    "Cancelar"
};


/* ============================================================
 * Variáveis globais
 * ============================================================ */

static AppScreen current_screen = SCREEN_DEVICES;

static FileManager file_manager;
static Clipboard clipboard;

static int device_cursor = 0;

static int mc0_available = 0;
static int mc1_available = 0;

static int usb_present[USB_UNITS];

/*
 * Formatacao (R1 na lista de dispositivos): HDD em APA; pendrive
 * em FAT32/exFAT com tabela MBR/GPT (fatfmt.irx).
 */
typedef enum {
    FORMAT_NONE = 0,
    FORMAT_DISKS,       /* USB: escolher o pendrive */
    FORMAT_MENU,
    FORMAT_CONFIRM,     /* 1a confirmacao: TRIANGULO */
    FORMAT_CONFIRM2     /* 2a confirmacao: START */
} FormatStep;

typedef enum {
    FORMAT_ACT_APA = 0,
    FORMAT_ACT_FAT32,
    FORMAT_ACT_EXFAT,
    FORMAT_ACT_TABLE_MBR,
    FORMAT_ACT_TABLE_GPT,
    FORMAT_ACT_SOON,
    FORMAT_ACT_CANCEL
} FormatAction;

#define FORMAT_MAX_OPTIONS  8

static FormatStep format_step = FORMAT_NONE;
static int format_cursor = 0;

static const char *format_labels[FORMAT_MAX_OPTIONS];
static FormatAction format_actions[FORMAT_MAX_OPTIONS];
static int format_option_count;
static FormatAction format_chosen;

/*
 * HDD
 */
static unsigned int format_total_mb;
static int format_formatted;
static int format_partitions;

/*
 * HDD em MBR/GPT (exFAT/FAT32): formatado pelo fatfmt, como o USB
 * (format_disks[0] = disco "ata").
 */
static int format_hdd_fat;

/*
 * USB (discos inteiros do BDM)
 */
static int format_usb;
static fatfmt_disk_t format_disks[FATFMT_MAX_DISKS];
static int format_disk_count;
static int format_disk;

static int operation_menu_open = 0;

/*
 * Confirmacao em tela cheia (Etapa 4).
 */
typedef enum {
    CONFIRM_NONE = 0,
    CONFIRM_DELETE,
    CONFIRM_MOVE,
    CONFIRM_RENAME,     /* so para arquivo de sistema do save */
    CONFIRM_PASTE       /* colar copia: conferir SIM/NAO */
} ConfirmAction;

/*
 * Conferencia byte a byte no Colar (copia). Comeca em SIM a cada
 * colagem; o mover sempre confere.
 */
static int paste_verify = 1;

static ConfirmAction confirm_action = CONFIRM_NONE;
static int operation_menu_cursor = 0;

/*
 * Teclado virtual aberto para (Etapa 4c).
 */
typedef enum {
    KEYBOARD_NONE = 0,
    KEYBOARD_MKDIR,
    KEYBOARD_RENAME,
    KEYBOARD_PARTITION
} KeyboardAction;

/*
 * Criar particao (R1 na lista de particoes do hdd0:).
 */
typedef enum {
    PARTITION_NONE = 0,
    PARTITION_MENU,
    PARTITION_SIZE,
    PARTITION_CONFIRM_DELETE
} PartitionStep;

#define PARTITION_MENU_CREATE   0
#define PARTITION_MENU_DELETE   1
#define PARTITION_MENU_COUNT    3

#define APA_MAX_NAME        31

static PartitionStep partition_step = PARTITION_NONE;
static int partition_cursor = 0;
static char partition_name[APA_MAX_NAME + 1];
static unsigned int partition_size_mb;
static unsigned int partition_free_mb;

static KeyboardAction keyboard_action = KEYBOARD_NONE;

/*
 * Item sendo renomeado (guardado ao abrir: a lista pode mudar).
 */
static char rename_path[FM_MAX_PATH];
static char rename_name[FM_MAX_NAME];

/*
 * Instalacao HDLoader: verificada ao abrir o menu R1.
 */
static IsoGameInfo hdl_games[HDL_MAX_GAMES];
static char hdl_names[HDL_MAX_GAMES][FM_MAX_NAME];
static char hdl_paths[HDL_MAX_GAMES][FM_MAX_PATH];
static int hdl_count = 0;
static int hdl_available = 0;
static int hdl_partitions = 0;
static int hdl_info_open = 0;
static char hdl_reason[96];

/*
 * Conferencia completa (rele tudo): opcional, dobra o tempo.
 * A conferencia "como o OPL" roda sempre.
 */
static int hdl_verify = 0;

static char status_message[256];


/* ============================================================
 * PAD
 * ============================================================ */

static unsigned char padBuf[256]
    __attribute__((aligned(64)));

static struct padButtonStatus buttons;

static u32 old_pad = 0;
static u32 new_pad = 0;


/* ============================================================
 * Funções auxiliares
 * ============================================================ */

static void set_status(const char *message)
{
    if (!message)
        return;

    strncpy(
        status_message,
        message,
        sizeof(status_message) - 1
    );

    status_message[
        sizeof(status_message) - 1
    ] = '\0';
}


/* ============================================================
 * Inicialização dos módulos
 * ============================================================ */

static void load_modules(void)
{
    int ret;
    int id;

    /*
     * Mesmo modelo do wLaunchELF (loadBasicModules):
     *
     * - reinicia o IOP e aplica os patches do SBV para
     *   permitir carregar modulos a partir da memoria do EE;
     * - iomanX + fileXio do PS2SDK (substituem o FILEIO da
     *   BIOS, cujo stat() devolvia dados da consulta anterior);
     * - MCMAN/MCSERV do PS2SDK (os da BIOS geravam erro -5).
     */
    while (!SifIopReset("", 0)) {
    }

    while (!SifIopSync()) {
    }

    SifInitRpc(0);
    SifLoadFileInit();

    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();


    id = SifExecModuleBuffer(iomanX_irx, size_iomanX_irx, 0, NULL, &ret);
    printf("IOMANX: id %d ret %d\n", id, ret);

    id = SifExecModuleBuffer(fileXio_irx, size_fileXio_irx, 0, NULL, &ret);
    printf("FILEXIO: id %d ret %d\n", id, ret);

    id = SifLoadModule("rom0:SIO2MAN", 0, NULL);
    printf("SIO2MAN: %d\n", id);

    id = SifExecModuleBuffer(mcman_irx, size_mcman_irx, 0, NULL, &ret);
    printf("MCMAN: id %d ret %d\n", id, ret);

    id = SifExecModuleBuffer(mcserv_irx, size_mcserv_irx, 0, NULL, &ret);
    printf("MCSERV: id %d ret %d\n", id, ret);

    id = SifLoadModule("rom0:PADMAN", 0, NULL);
    printf("PADMAN: %d\n", id);


    ret = fileXioInit();
    printf("fileXioInit: %d\n", ret);
}

/* ============================================================
 * Inicialização do controle
 * ============================================================ */

static int init_pad(void)
{
    int ret;

    padInit(0);

    ret = padPortOpen(
        0,
        0,
        padBuf
    );

    if (ret == 0) {
        printf(
            "Erro ao abrir controle.\n"
        );

        return 0;
    }

    return 1;
}


/* ============================================================
 * Leitura do controle
 * ============================================================ */

static int read_pad(void)
{
    int state;
    int ret;

    state = padGetState(
        0,
        0
    );

    if (state != PAD_STATE_STABLE &&
        state != PAD_STATE_FINDCTP1) {

        return 0;
    }

    ret = padRead(
        0,
        0,
        &buttons
    );

    if (ret == 0)
        return 0;

    old_pad = new_pad;

    new_pad =
        0xFFFF ^
        buttons.btns;

    return 1;
}


/* ============================================================
 * Botão pressionado nesta atualização
 * ============================================================ */

static int pressed(u32 button)
{
    return
        (new_pad & button) &&
        !(old_pad & button);
}


/* ============================================================
 * Memory Cards
 * ============================================================ */

static void refresh_devices(void)
{
    int i;

    mc0_available =
        memorycard_available(0);

    mc1_available =
        memorycard_available(1);

    /*
     * USB so depois de carregado (o primeiro X carrega).
     */
    for (i = 0; i < USB_UNITS; i++)
        usb_present[i] = usb_available(i);


    snprintf(
        status_message,
        sizeof(status_message),
        "MC0: %s | MC1: %s",
        mc0_available ? "OK" : "indisponivel",
        mc1_available ? "OK" : "indisponivel"
    );
}


/* ============================================================
 * SMB: rede + conexao (Etapa 8)
 * ============================================================ */

/*
 * Pilha de rede carregada (uma unica vez).
 */
static int network_ready = 0;


/*
 * Aplica o modo de IP da configuracao (DHCP ou estatico).
 */
static int network_apply_config(void)
{
    const AppConfig *config = config_get();

    if (config->dhcp)
        return network_start_dhcp();

    return network_apply_static(
        config->ip,
        config->netmask,
        config->gateway,
        config->dns
    );
}


static int smb_ensure_connected(void)
{
    const AppConfig *config;
    const NetworkInfo *net;
    const SMBInfo *smb;

    clock_t start;
    clock_t tick;
    int seconds;


    smb = smb_get_info();

    if (smb->connected)
        return 1;


    config = config_get();

    scr_clear();

    scr_printf("PS2FileManager\n");
    scr_printf("===============\n\n");
    scr_printf("Conectando ao SMB...\n\n");
    scr_printf("Servidor: %s\n", config->smb_server);
    scr_printf("Share   : %s\n", config->smb_share);
    scr_printf("Usuario : %s\n", config->smb_user);
    scr_printf("Rede    : %s\n\n", config->dhcp ? "DHCP" : config->ip);


    if (!network_ready) {

        scr_printf("Iniciando rede (DEV9/SMAP/PS2IP)...\n");

        if (network_init() < 0) {

            set_status(
                "ERRO rede: falha ao carregar modulos."
            );

            return 0;
        }

        if (network_apply_config() < 0) {

            set_status(
                config->dhcp
                    ? "ERRO rede: falha ao iniciar DHCP."
                    : "ERRO rede: IP estatico invalido."
            );

            return 0;
        }

        network_ready = 1;
    }


    scr_printf(config->dhcp ? "Aguardando DHCP" : "Aguardando link");

    start = clock();
    seconds = 0;

    while (1) {

        network_update();

        net = network_get_info();

        if (net->link_up &&
            (net->dhcp_bound || !config->dhcp) &&
            strcmp(net->ip, "0.0.0.0") != 0)
            break;

        if (clock() - start >
            (clock_t)SMB_DHCP_TIMEOUT * CLOCKS_PER_SEC) {

            snprintf(
                status_message,
                sizeof(status_message),
                "ERRO: %s em %ds (link %s)",
                config->dhcp ? "DHCP sem resposta" : "rede nao pronta",
                SMB_DHCP_TIMEOUT,
                net->link_up ? "OK" : "DESLIGADO"
            );

            printf(
                "FM SMB: DHCP timeout, link=%d\n",
                net->link_up
            );

            return 0;
        }

        /*
         * Espera ~250 ms entre consultas.
         */
        tick = clock();

        while (clock() - tick < CLOCKS_PER_SEC / 4) {
        }

        if ((clock() - start) / CLOCKS_PER_SEC > seconds) {

            seconds++;

            scr_printf(".");
        }
    }


    scr_printf("\nIP do PS2: %s\n\n", net->ip);

    printf("FM SMB: rede pronta, IP %s\n", net->ip);


    scr_printf("Abrindo compartilhamento...\n");

    if (smb_init() < 0 ||
        smb_connect(
            config->smb_server,
            config->smb_share,
            config->smb_user,
            config->smb_password) < 0) {

        snprintf(
            status_message,
            sizeof(status_message),
            "ERRO SMB: %s",
            smb->error
        );

        return 0;
    }


    return 1;
}


static void open_smb(void)
{
    if (!smb_ensure_connected())
        return;


    fm_init(
        &file_manager,
        "smb0:/"
    );


    current_screen =
        SCREEN_FILES;


    snprintf(
        status_message,
        sizeof(status_message),
        "Aberto: smb0:/ (%s em %s)",
        config_get()->smb_share,
        config_get()->smb_server
    );
}


/* ============================================================
 * HDD interno (Etapa 5)
 * ============================================================ */

static int is_partition_list(void)
{
    return strcmp(file_manager.current_path, "hdd0:") == 0;
}


/*
 * HDD em MBR/GPT + exFAT/FAT32: o bdmfs_fatfs monta a particao
 * como "ata0:" (de forma assincrona, logo depois do ata_bd).
 */
#define HDD_FAT_ROOT "ata0:/"

static int hdd_fat_mounted(void)
{
    clock_t start = clock();
    DIR *dir;

    do {
        dir = opendir(HDD_FAT_ROOT);

        if (dir) {
            closedir(dir);
            return 1;
        }

    } while (clock() - start < CLOCKS_PER_SEC * 3);

    return 0;
}


static void open_hdd_fat(void)
{
    if (!hdd_fat_mounted()) {

        set_status("HDD em MBR/GPT sem exFAT/FAT32 montavel: R1 para formatar.");

        return;
    }

    fm_init(&file_manager, HDD_FAT_ROOT);

    current_screen = SCREEN_FILES;

    set_status("Aberto: " HDD_FAT_ROOT " (HDD exFAT/FAT32)");
}


static void open_hdd(void)
{
    scr_clear();

    scr_printf("PS2FileManager\n");
    scr_printf("===============\n\n");
    scr_printf("Iniciando HDD (DEV9/ATAD/APA/PFS)...\n");


    if (hdd_init() < 0) {

        /*
         * HDD em MBR/GPT (exFAT/FAT32, como o OPL): pelo BDM.
         */
        if (hdd_layout_table() == FATFMT_TABLE_MBR ||
            hdd_layout_table() == FATFMT_TABLE_GPT) {

            open_hdd_fat();

            return;
        }

        snprintf(
            status_message,
            sizeof(status_message),
            "ERRO HDD: %s",
            hdd_error()
        );

        return;
    }


    fm_init(
        &file_manager,
        "hdd0:"
    );


    current_screen =
        SCREEN_FILES;


    snprintf(
        status_message,
        sizeof(status_message),
        "HDD: %d particao(oes). X monta a particao.",
        file_manager.count
    );
}


/*
 * Ponto de montagem ("pfsN:") usado pelo clipboard, que
 * nao pode ser desmontado. NULL se o clipboard nao usa HDD.
 */
static const char *clipboard_pfs_point(void)
{
    static char point[8];
    const char *colon;
    int length;


    if (clipboard.mode == CLIPBOARD_NONE ||
        clipboard.count <= 0 ||
        strncmp(clipboard.paths[0], "pfs", 3) != 0)
        return NULL;


    colon = strchr(clipboard.paths[0], ':');

    if (!colon)
        return NULL;

    length = (int)(colon - clipboard.paths[0]) + 1;

    if (length >= (int)sizeof(point))
        return NULL;

    memcpy(point, clipboard.paths[0], length);
    point[length] = '\0';

    return point;
}


static void enter_partition(void)
{
    FMEntry *entry;
    const char *point;
    char root[16];
    char name[64];


    if (file_manager.count <= 0)
        return;


    entry =
        &file_manager.entries[file_manager.cursor];


    /*
     * Copia o nome: fm_init() substitui a lista (e o entry).
     */
    snprintf(
        name,
        sizeof(name),
        "%s",
        entry->name
    );


    point =
        hdd_mount_partition(
            name,
            clipboard_pfs_point()
        );


    if (!point) {

        snprintf(
            status_message,
            sizeof(status_message),
            "ERRO: %s: %s",
            name,
            hdd_error()
        );

        return;
    }


    snprintf(
        root,
        sizeof(root),
        "%s/",
        point
    );


    fm_init(
        &file_manager,
        root
    );


    snprintf(
        status_message,
        sizeof(status_message),
        "Particao %s montada em %s",
        name,
        root
    );
}


/*
 * Sair da particao NAO desmonta: o clipboard pode apontar
 * para ela (copiar do HDD para outro dispositivo).
 */
static void leave_partition(void)
{
    const char *name;
    char saved[64];


    name =
        hdd_partition_of(
            file_manager.current_path
        );

    snprintf(
        saved,
        sizeof(saved),
        "%s",
        name ? name : "?"
    );


    hdd_sync_all();


    fm_init(
        &file_manager,
        "hdd0:"
    );


    snprintf(
        status_message,
        sizeof(status_message),
        "Particao %s continua montada.",
        saved
    );
}

/* ============================================================
 * USB (Etapa 6)
 * ============================================================ */

static void open_usb(int unit)
{
    char root[16];
    int i;


    if (!usb_ready()) {

        scr_clear();

        scr_printf("PS2FileManager\n");
        scr_printf("===============\n\n");
        scr_printf("Iniciando USB (USBD/BDM/FATFS)...\n");
        scr_printf("Procurando pendrives...\n");

        if (usb_init() < 0) {

            snprintf(
                status_message,
                sizeof(status_message),
                "ERRO USB: %s",
                usb_error()
            );

            return;
        }
    }


    for (i = 0; i < USB_UNITS; i++)
        usb_present[i] = usb_available(i);

    if (!usb_present[unit]) {

        snprintf(
            status_message,
            sizeof(status_message),
            "Pendrive " USB_PREFIX "%d: nao encontrado.",
            unit
        );

        return;
    }


    snprintf(
        root,
        sizeof(root),
        USB_PREFIX "%d:/",
        unit
    );


    fm_init(
        &file_manager,
        root
    );


    current_screen =
        SCREEN_FILES;


    snprintf(
        status_message,
        sizeof(status_message),
        "Aberto: %s",
        root
    );
}


/* ============================================================
 * Abre Memory Card
 * ============================================================ */

static void open_device(int device)
{
    char path[16];

    if (device == DEVICE_CONFIG) {

        settings_open();

        current_screen = SCREEN_SETTINGS;

        return;
    }

    if (device >= DEVICE_USB0) {

        open_usb(device - DEVICE_USB0);

        return;
    }

    if (device == DEVICE_SMB) {

        open_smb();

        return;
    }

    if (device == DEVICE_HDD) {

        open_hdd();

        return;
    }

    if (device == 0) {

        if (!mc0_available) {
            set_status(
                "Memory Card 0 indisponivel."
            );

            return;
        }

        strcpy(
            path,
            "mc0:/"
        );

    } else {

        if (!mc1_available) {
            set_status(
                "Memory Card 1 indisponivel."
            );

            return;
        }

        strcpy(
            path,
            "mc1:/"
        );
    }


    fm_init(
        &file_manager,
        path
    );


    current_screen =
        SCREEN_FILES;


    snprintf(
        status_message,
        sizeof(status_message),
        "Aberto: %s",
        path
    );
}


/* ============================================================
 * Nome do arquivo a partir do caminho
 * ============================================================ */

static const char *path_filename(
    const char *path)
{
    const char *slash;

    if (!path)
        return "";

    slash =
        strrchr(
            path,
            '/'
        );

    if (!slash)
        return path;

    return slash + 1;
}


/* ============================================================
 * Monta caminho de destino
 * ============================================================ */

static int build_destination_path(
    const char *directory,
    const char *source,
    char *destination,
    int destination_size)
{
    const char *filename;
    int length;

    if (!directory ||
        !source ||
        !destination ||
        destination_size <= 0) {

        return 0;
    }


    filename =
        path_filename(source);


    if (!filename[0])
        return 0;


    length =
        strlen(directory);


    if (length <= 0)
        return 0;


    if (directory[length - 1] == '/') {

        snprintf(
            destination,
            destination_size,
            "%s%s",
            directory,
            filename
        );

    } else {

        snprintf(
            destination,
            destination_size,
            "%s/%s",
            directory,
            filename
        );
    }


    destination[
        destination_size - 1
    ] = '\0';


    return 1;
}


/* ============================================================
 * Copiar seleção para clipboard
 * ============================================================ */

static void copy_selection(void)
{
    int count;

    count =
        clipboard_from_selection(
            &clipboard,
            &file_manager,
            CLIPBOARD_COPY
        );


    if (count <= 0) {

        set_status(
            "Nenhum item selecionado."
        );

        return;
    }


    snprintf(
        status_message,
        sizeof(status_message),
        "%d item(ns) no clipboard.",
        count
    );
}


/* ============================================================
 * Arquivos de sistema dos saves
 * ============================================================ */

/*
 * icon.sys / *.icn / *.ico formam o icone do save e
 * B?DATA-SYSTEM e criada pela BIOS. Apagar ou mover
 * esses itens pode deixar o save "corrompido".
 */
static int is_system_name(const char *name)
{
    size_t length;

    if (!name)
        return 0;

    length = strlen(name);

    if (strcasecmp(name, "icon.sys") == 0)
        return 1;

    if (length > 4 &&
        (strcasecmp(name + length - 4, ".icn") == 0 ||
         strcasecmp(name + length - 4, ".ico") == 0))
        return 1;

    if (length == 13 &&
        name[0] == 'B' &&
        strcmp(name + 2, "DATA-SYSTEM") == 0)
        return 1;

    return 0;
}


static int selection_has_system(void)
{
    int i;

    for (i = 0; i < file_manager.count; i++) {

        if (file_manager.entries[i].selected &&
            is_system_name(file_manager.entries[i].name))
            return 1;
    }

    return 0;
}


static int clipboard_has_system(void)
{
    int i;

    for (i = 0; i < clipboard.count; i++) {

        if (is_system_name(
                path_filename(clipboard.paths[i])))
            return 1;
    }

    return 0;
}


/* ============================================================
 * Executa Colar (copiar ou mover)
 * ============================================================ */

static void run_paste_items(void)
{
    int i;
    int result;
    int done;
    int length;
    int move;

    unsigned long long source_bytes;
    int source_files;

    char destination[
        FM_MAX_PATH
    ];

    const char *source;

    FileCopyStatus copy_status;
    FileDeleteStatus delete_status;


    move =
        clipboard.mode == CLIPBOARD_MOVE;

    done = 0;


    for (i = 0;
         i < clipboard.count;
         i++) {

        source =
            clipboard.paths[i];


        if (!build_destination_path(
                file_manager.current_path,
                source,
                destination,
                sizeof(destination))) {

            set_status(
                "Falha ao montar destino."
            );

            return;
        }


        /*
         * Pasta dentro dela mesma (ex.: mc0:/OPL ->
         * mc0:/OPL/OPL) geraria recursao sem fim.
         */
        length = strlen(source);

        if (strncmp(destination, source, length) == 0 &&
            destination[length] == '/') {

            snprintf(
                status_message,
                sizeof(status_message),
                "ERRO: destino dentro da origem (%s)",
                path_filename(source)
            );

            printf(
                "FM PASTE: recusado, %s dentro de %s\n",
                destination,
                source
            );

            fm_refresh(
                &file_manager
            );

            return;
        }


        memset(
            &copy_status,
            0,
            sizeof(copy_status)
        );


        result =
            fileio_copy_file(
                source,
                destination,
                &copy_status
            );


        /*
         * Registro no console (log do PCSX2).
         */
        printf(
            "FM COPY: %s -> %s | %s | err %d | sys %d | copiados %d | verificados %d | arquivos %d | pastas %d\n",
            source,
            destination,
            copy_status.stage,
            copy_status.error,
            copy_status.system_result,
            copy_status.bytes_copied,
            copy_status.bytes_verified,
            copy_status.files_copied,
            copy_status.directories_created
        );


        if (result !=
            FILEIO_COPY_OK) {

            snprintf(
                status_message,
                sizeof(status_message),
                "ERRO %s | err %d | sys %d | %s",
                copy_status.stage,
                copy_status.error,
                copy_status.system_result,
                fileio_error_string(
                    copy_status.error
                )
            );

            /*
             * Nao apagamos nada em caso de falha:
             * nem o destino parcial, nem a origem.
             */
            fm_refresh(
                &file_manager
            );

            return;
        }


        /*
         * MOVER: a origem so e excluida depois da
         * copia verificada byte a byte.
         */
        if (move) {

            /*
             * Confere se tudo da origem foi copiado: no SMB
             * uma listagem interrompida parece fim de pasta
             * (fm_fileio.c), e a exclusao apagaria arquivos
             * que nao chegaram ao destino.
             */
            source_bytes = 0;
            source_files = 0;

            fileio_measure(
                source,
                &source_bytes,
                &source_files
            );

            if (source_files != copy_status.files_copied ||
                source_bytes != (unsigned long long)(unsigned int)copy_status.bytes_copied) {

                printf(
                    "FM MOVE: %s | origem MANTIDA | origem %d arq %llu bytes | copiado %d arq %d bytes\n",
                    source,
                    source_files,
                    source_bytes,
                    copy_status.files_copied,
                    copy_status.bytes_copied
                );

                set_status(
                    "COPIADO, mas origem NAO excluida: totais diferentes."
                );

                fm_refresh(
                    &file_manager
                );

                return;
            }


            result =
                fileio_delete(
                    source,
                    &delete_status
                );


            printf(
                "FM MOVE: %s | origem %s | err %d | sys %d | arquivos %d | pastas %d\n",
                source,
                delete_status.stage,
                delete_status.error,
                delete_status.system_result,
                delete_status.files_deleted,
                delete_status.directories_deleted
            );


            if (result !=
                FILEIO_COPY_OK) {

                snprintf(
                    status_message,
                    sizeof(status_message),
                    "COPIADO, mas origem NAO excluida: %s",
                    fileio_error_string(
                        delete_status.error
                    )
                );

                fm_refresh(
                    &file_manager
                );

                return;
            }
        }


        done++;
    }


    fm_refresh(
        &file_manager
    );


    if (move) {

        /*
         * Os itens nao existem mais na origem.
         */
        clipboard_clear(
            &clipboard
        );

        snprintf(
            status_message,
            sizeof(status_message),
            "MOVIDO: %d item(ns)",
            done
        );

    } else {

        snprintf(
            status_message,
            sizeof(status_message),
            paste_verify
                ? "COPIA VERIFICADA: %d item(ns)"
                : "COPIA CONCLUIDA (sem conferencia): %d item(ns)",
            done
        );
    }
}


/* ============================================================
 * Progresso em tempo real
 * ============================================================ */

#define PROGRESS_BAR_WIDTH  30

static clock_t progress_start;
static clock_t progress_last_draw;

static unsigned long long progress_total;
static unsigned long long progress_done;

static int progress_files_total;
static int progress_file_index;


static void format_size(
    char *out,
    int size,
    unsigned long long bytes)
{
    if (bytes >= 1048576ULL) {

        snprintf(
            out,
            size,
            "%llu.%llu MB",
            bytes / 1048576ULL,
            (bytes % 1048576ULL) * 10ULL / 1048576ULL
        );

    } else if (bytes >= 1024ULL) {

        snprintf(
            out,
            size,
            "%llu KB",
            bytes / 1024ULL
        );

    } else {

        snprintf(
            out,
            size,
            "%llu B",
            bytes
        );
    }
}


static void format_time(
    char *out,
    int size,
    unsigned long seconds)
{
    snprintf(
        out,
        size,
        "%02lu:%02lu",
        seconds / 60,
        seconds % 60
    );
}


static void draw_progress_bar(
    int row,
    const char *label,
    unsigned long long done,
    unsigned long long total)
{
    char bar[PROGRESS_BAR_WIDTH + 1];
    int percent;
    int filled;
    int i;


    percent =
        total
            ? (int)(done * 100ULL / total)
            : 100;

    if (percent > 100)
        percent = 100;

    filled =
        percent * PROGRESS_BAR_WIDTH / 100;

    for (i = 0; i < PROGRESS_BAR_WIDTH; i++)
        bar[i] = (i < filled) ? '#' : '-';

    bar[PROGRESS_BAR_WIDTH] = '\0';


    scr_setXY(0, row);

    scr_printf(
        "%-8s [%s] %3d%%   \n",
        label,
        bar,
        percent
    );
}


static void paste_progress(
    int phase,
    const char *path,
    unsigned int file_done,
    unsigned int file_total,
    unsigned int step)
{
    clock_t now;
    unsigned long elapsed_ms;
    unsigned long long speed;
    unsigned long remaining;

    char text_done[24];
    char text_total[24];
    char text_elapsed[16];
    char text_remaining[16];
    char text_speed[24];


    now = clock();

    progress_done += step;


    if (phase == FILEIO_PHASE_COPY &&
        step == 0)
        progress_file_index++;


    /*
     * Ate ~5 atualizacoes por segundo; inicio e fim de
     * cada arquivo sempre aparecem.
     */
    if (step != 0 &&
        file_done < file_total &&
        now - progress_last_draw < CLOCKS_PER_SEC / 5)
        return;

    progress_last_draw = now;


    elapsed_ms =
        (unsigned long)((now - progress_start) * 1000ULL / CLOCKS_PER_SEC);

    speed =
        elapsed_ms
            ? progress_done * 1000ULL / elapsed_ms
            : 0;

    remaining =
        speed
            ? (unsigned long)((progress_total - progress_done) / speed)
            : 0;


    scr_setXY(0, 4);

    scr_printf(
        "Arquivo %d de %d: %-36.36s\n",
        progress_file_index,
        progress_files_total,
        path_filename(path)
    );

    scr_setXY(0, 5);

    scr_printf(
        "Fase: %-12s\n",
        phase == FILEIO_PHASE_COPY
            ? "copiando"
            : "verificando"
    );


    draw_progress_bar(
        7,
        "Arquivo",
        file_done,
        file_total
    );

    format_size(text_done, sizeof(text_done), file_done);
    format_size(text_total, sizeof(text_total), file_total);

    scr_setXY(0, 8);

    scr_printf(
        "         %s de %s            \n",
        text_done,
        text_total
    );


    /*
     * Total = bytes copiados + bytes verificados.
     */
    draw_progress_bar(
        10,
        "Total",
        progress_done,
        progress_total
    );

    format_time(text_elapsed, sizeof(text_elapsed), elapsed_ms / 1000);
    format_time(text_remaining, sizeof(text_remaining), remaining);
    format_size(text_speed, sizeof(text_speed), speed);

    scr_setXY(0, 11);

    scr_printf(
        "         Decorrido %s | Restante ~%s | %s/s      \n",
        text_elapsed,
        text_remaining,
        text_speed
    );
}


/*
 * Mede a selecao, abre a tela de progresso e executa.
 */
static void run_paste(void)
{
    unsigned long long total_bytes;
    unsigned long elapsed_ms;
    int total_files;
    int i;

    char text_total[24];


    scr_clear();

    scr_printf("PS2FileManager\n");
    scr_printf("===============\n");
    scr_printf(
        "%s %d item(ns) para %-30.30s\n",
        clipboard.mode == CLIPBOARD_MOVE
            ? "MOVENDO"
            : "COPIANDO",
        clipboard.count,
        file_manager.current_path
    );

    scr_setXY(0, 4);
    scr_printf("Calculando tamanho...\n");


    total_bytes = 0;
    total_files = 0;

    for (i = 0; i < clipboard.count; i++) {

        fileio_measure(
            clipboard.paths[i],
            &total_bytes,
            &total_files
        );
    }


    format_size(text_total, sizeof(text_total), total_bytes);

    scr_setXY(0, 13);

    scr_printf(
        "Selecao: %d arquivo(s), %s\n",
        total_files,
        text_total
    );


    /*
     * Mover sempre confere (a origem so e apagada depois).
     */
    if (clipboard.mode == CLIPBOARD_MOVE)
        paste_verify = 1;

    progress_total = paste_verify ? total_bytes * 2ULL : total_bytes;
    progress_done = 0;
    progress_files_total = total_files;
    progress_file_index = 0;
    progress_start = clock();
    progress_last_draw = 0;


    fileio_set_progress(paste_progress);
    fileio_set_verify(paste_verify);

    run_paste_items();

    fileio_set_verify(1);
    fileio_set_progress(NULL);

    /*
     * Particoes do HDD ficam montadas: grava ja no disco.
     */
    if (hdd_mounted_count() > 0)
        hdd_sync_all();


    elapsed_ms =
        (unsigned long)((clock() - progress_start) * 1000ULL / CLOCKS_PER_SEC);

    printf(
        "FM PASTE: %d arquivo(s), %llu bytes, %lu ms\n",
        total_files,
        total_bytes,
        elapsed_ms
    );
}


/* ============================================================
 * Colar clipboard
 * ============================================================ */
static void paste_clipboard(void)
{
    if (clipboard.mode ==
            CLIPBOARD_NONE ||
        clipboard.count <= 0) {

        set_status(
            "Clipboard vazio."
        );

        return;
    }


    /*
     * Mover exclui a origem: pede confirmacao.
     */
    if (clipboard.mode ==
        CLIPBOARD_MOVE) {

        confirm_action =
            CONFIRM_MOVE;

        set_status(
            "Confirme a movimentacao."
        );

        return;
    }


    /*
     * Copia: tela rapida com a escolha da conferencia.
     */
    paste_verify = 1;

    confirm_action = CONFIRM_PASTE;
}

/* ============================================================
 * Instalacao HDLoader (Etapa 1: so verificacao, nada e gravado)
 * ============================================================ */

/*
 * Nome do jogo a partir do arquivo: sem ".iso" e sem o ID
 * no inicio (padrao do OPL "SLUS_123.45.Nome.iso").
 */
static void hdl_game_name(
    char *out,
    int size,
    const char *file_name,
    const char *game_id)
{
    int id_length;
    int length;

    id_length = strlen(game_id);

    if (id_length > 0 &&
        strncasecmp(file_name, game_id, id_length) == 0 &&
        file_name[id_length] == '.')
        file_name += id_length + 1;

    snprintf(out, size, "%s", file_name);

    length = strlen(out);

    if (iso_has_extension(out))
        out[length - 4] = '\0';
}


/*
 * Opcao disponivel so se TODOS os selecionados forem ISO de
 * jogo de PS2 e houver HDD formatado em APA.
 */
static void hdl_check(void)
{
    FMEntry *entry;
    IsoGameInfo *game;
    DIR *dir;
    int selected;
    int i;


    hdl_count = 0;
    hdl_available = 0;
    hdl_partitions = 0;
    selected = 0;


    for (i = 0; i < file_manager.count; i++) {

        entry = &file_manager.entries[i];

        if (!entry->selected)
            continue;

        selected++;

        if (entry->is_dir ||
            !iso_has_extension(entry->name)) {

            snprintf(
                hdl_reason,
                sizeof(hdl_reason),
                "HDLoader: selecione so arquivos .iso (%.30s)",
                entry->name
            );

            return;
        }

        if (hdl_count >= HDL_MAX_GAMES) {

            snprintf(
                hdl_reason,
                sizeof(hdl_reason),
                "HDLoader: maximo de %d jogos por vez.",
                HDL_MAX_GAMES
            );

            return;
        }

        game = &hdl_games[hdl_count];

        if (!iso_probe_game(entry->path, game)) {

            printf(
                "FM HDL: %s | nao e jogo de PS2: %s\n",
                entry->path,
                game->reason
            );

            snprintf(
                hdl_reason,
                sizeof(hdl_reason),
                "HDLoader: %.30s: %s",
                entry->name,
                game->reason
            );

            return;
        }

        hdl_game_name(
            hdl_names[hdl_count],
            FM_MAX_NAME,
            entry->name,
            game->game_id
        );

        snprintf(
            hdl_paths[hdl_count],
            FM_MAX_PATH,
            "%s",
            entry->path
        );

        printf(
            "FM HDL: %s | jogo %s | %s | %u setores | volume '%s' | nome '%s'\n",
            entry->path,
            game->game_id,
            game->is_dvd ? "DVD" : "CD",
            game->sectors,
            game->volume_id,
            hdl_names[hdl_count]
        );

        hdl_count++;
    }


    if (selected == 0) {

        snprintf(
            hdl_reason,
            sizeof(hdl_reason),
            "HDLoader: selecione ISOs de jogos (QUADRADO)."
        );

        return;
    }


    /*
     * HDD so e iniciado quando a selecao e toda de jogos.
     * APA = "hdd0:" lista as particoes.
     */
    if (hdd_init() < 0) {

        snprintf(
            hdl_reason,
            sizeof(hdl_reason),
            "HDLoader: sem HDD (%s)",
            hdd_error()
        );

        return;
    }

    dir = opendir("hdd0:");

    if (!dir) {

        snprintf(
            hdl_reason,
            sizeof(hdl_reason),
            "HDLoader: HDD nao formatado em APA."
        );

        return;
    }

    while (readdir(dir) != NULL)
        hdl_partitions++;

    closedir(dir);


    hdl_available = 1;

    printf(
        "FM HDL: %d jogo(s), HDD APA com %d particao(oes)\n",
        hdl_count,
        hdl_partitions
    );
}


static void draw_hdl_info(void)
{
    char size_text[24];
    int i;


    scr_printf("PS2FileManager\n");
    scr_printf("===============\n\n");

    scr_printf("INSTALAR NO HDD (HDLoader)\n\n");

    scr_printf(
        "%d jogo(s) reconhecido(s):\n\n",
        hdl_count
    );


    /*
     * Debug screen: ~28 linhas, 2 por jogo.
     */
    for (i = 0; i < hdl_count && i < 8; i++) {

        format_size(
            size_text,
            sizeof(size_text),
            (unsigned long long)hdl_games[i].sectors * ISO_SECTOR_SIZE
        );

        scr_printf(
            "%-12s %-3s %10s  %s\n",
            hdl_games[i].game_id,
            hdl_games[i].is_dvd ? "DVD" : "CD",
            size_text,
            hdl_games[i].volume_id
        );

        scr_printf(
            "   %.60s\n",
            hdl_names[i]
        );
    }

    if (hdl_count > 8)
        scr_printf("... e mais %d\n", hdl_count - 8);


    scr_printf(
        "\nHDD: APA OK, %d particao(oes)\n",
        hdl_partitions
    );

    scr_printf("\nCada jogo vira uma particao PP.<ID>.HDL.<nome>\n");
    scr_printf("(sempre conferida como o OPL le).\n\n");

    scr_printf(
        "Conferir copia completa: %s\n",
        hdl_verify ? "SIM (dobra o tempo)" : "NAO"
    );

    scr_printf("\nX instalar | QUADRADO conferir sim/nao | O voltar\n");
}


/*
 * Progresso da gravacao HDL (mesmo estilo do Colar): barra da
 * fase e barra total (gravar + conferir).
 */
static char hdl_progress_title[64];
static clock_t hdl_progress_start;
static clock_t hdl_progress_last;

static void hdl_progress_draw(
    int phase,
    unsigned long long done,
    unsigned long long total)
{
    unsigned long long all_done;
    unsigned long long all_total;
    unsigned long long speed;
    unsigned long elapsed_ms;
    unsigned long remaining;
    clock_t now;

    char text_done[24];
    char text_total[24];
    char text_speed[24];
    char text_elapsed[16];
    char text_remaining[16];


    now = clock();

    if (phase == HDL_PHASE_WRITE && done == 0) {

        hdl_progress_start = now;

        scr_clear();

        scr_printf("PS2FileManager\n");
        scr_printf("===============\n");
        scr_printf("%-.60s\n", hdl_progress_title);

    } else if (done != 0 &&
               done < total &&
               now - hdl_progress_last < CLOCKS_PER_SEC / 5) {

        /*
         * Ate ~5 atualizacoes por segundo; inicio e fim de
         * cada fase sempre aparecem.
         */
        return;
    }

    hdl_progress_last = now;


    all_total = hdl_verify ? total * 2ULL : total;
    all_done = phase == HDL_PHASE_WRITE ? done : total + done;

    elapsed_ms =
        (unsigned long)((now - hdl_progress_start) * 1000ULL / CLOCKS_PER_SEC);

    speed =
        elapsed_ms
            ? all_done * 1000ULL / elapsed_ms
            : 0;

    remaining =
        speed
            ? (unsigned long)((all_total - all_done) / speed)
            : 0;


    scr_setXY(0, 5);

    scr_printf(
        "Fase: %-12s\n",
        phase == HDL_PHASE_WRITE
            ? "gravando"
            : "conferindo"
    );

    draw_progress_bar(
        7,
        phase == HDL_PHASE_WRITE ? "Gravar" : "Conferir",
        done,
        total
    );

    format_size(text_done, sizeof(text_done), done);
    format_size(text_total, sizeof(text_total), total);

    scr_setXY(0, 8);

    scr_printf(
        "         %s de %s            \n",
        text_done,
        text_total
    );

    draw_progress_bar(
        10,
        "Total",
        all_done,
        all_total
    );

    format_size(text_speed, sizeof(text_speed), speed);
    format_time(text_elapsed, sizeof(text_elapsed), elapsed_ms / 1000);
    format_time(text_remaining, sizeof(text_remaining), remaining);

    scr_setXY(0, 12);

    scr_printf(
        "Velocidade: %s/s          \n",
        text_speed
    );

    scr_printf(
        "Tempo: %s | Restante: %s   \n",
        text_elapsed,
        text_remaining
    );
}


/*
 * Instala os jogos reconhecidos, um por vez. Um erro nao impede
 * os seguintes (a particao com erro e apagada em hdl_install).
 */
static void hdl_install_all(void)
{
    char last_failure[96];
    int installed = 0;
    int failed = 0;
    int i;


    last_failure[0] = '\0';

    hdl_set_progress(hdl_progress_draw);

    for (i = 0; i < hdl_count; i++) {

        snprintf(
            hdl_progress_title,
            sizeof(hdl_progress_title),
            "Instalando %d de %d: %s %.30s",
            i + 1,
            hdl_count,
            hdl_games[i].game_id,
            hdl_names[i]
        );

        if (hdl_install(
                hdl_paths[i],
                &hdl_games[i],
                hdl_names[i],
                hdl_verify) == 0) {

            installed++;

        } else {

            failed++;

            snprintf(
                last_failure,
                sizeof(last_failure),
                "%.20s: %s",
                hdl_names[i],
                hdl_error()
            );
        }
    }

    hdl_set_progress(NULL);


    if (failed == 0) {

        snprintf(
            status_message,
            sizeof(status_message),
            "HDLoader: %d jogo(s) instalado(s) e conferido(s)%s.",
            installed,
            hdl_verify ? " (completa)" : " (como o OPL)"
        );

    } else {

        snprintf(
            status_message,
            sizeof(status_message),
            "HDLoader: %d ok, %d erro | %s",
            installed,
            failed,
            last_failure
        );
    }

    printf("FM STATUS: %s\n", status_message);

    fm_clear_selection(
        &file_manager
    );
}


static void update_hdl_info(void)
{
    if (pressed(PAD_CROSS)) {

        hdl_info_open = 0;

        hdl_install_all();

        return;
    }

    if (pressed(PAD_SQUARE)) {

        hdl_verify = !hdl_verify;

        printf("FM HDL: conferencia completa %s\n", hdl_verify ? "SIM" : "NAO");
    }

    if (pressed(PAD_CIRCLE))
        hdl_info_open = 0;
}


/* ============================================================
 * Criar pasta (Etapa 4c)
 * ============================================================ */

/*
 * Nome digitado: nao vazio, sem espaco nas pontas, nao "." / "..".
 */
static int name_is_valid(const char *name)
{
    int length = strlen(name);

    if (length == 0)
        return 0;

    if (name[0] == ' ' || name[length - 1] == ' ')
        return 0;

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;

    return 1;
}


/*
 * Ja existe na pasta atual? Sem diferenciar maiusculas
 * (FAT e SMB nao diferenciam).
 */
static int name_exists(const char *name)
{
    int i;

    for (i = 0; i < file_manager.count; i++) {

        if (strcasecmp(file_manager.entries[i].name, name) == 0)
            return 1;
    }

    return 0;
}


static void create_folder(const char *name)
{
    char path[FM_MAX_PATH];
    int result;


    if (!name_is_valid(name)) {

        set_status("Nome invalido.");

        return;
    }

    if (name_exists(name)) {

        snprintf(
            status_message,
            sizeof(status_message),
            "Ja existe: %s",
            name
        );

        return;
    }

    if (!build_destination_path(
            file_manager.current_path,
            name,
            path,
            sizeof(path))) {

        set_status("Caminho invalido.");

        return;
    }


    errno = 0;

    result = mkdir(path, 0777);

    printf("FM MKDIR: %s -> %d (errno %d)\n", path, result, errno);


    if (hdd_mounted_count() > 0)
        hdd_sync_all();

    fm_refresh(&file_manager);


    if (result < 0) {

        snprintf(
            status_message,
            sizeof(status_message),
            "Erro ao criar pasta %s (%d)",
            name,
            errno
        );

        return;
    }

    snprintf(
        status_message,
        sizeof(status_message),
        "Pasta criada: %s",
        name
    );
}


/* ============================================================
 * Renomear (Etapa 4c)
 * ============================================================ */

static int is_memory_card_path(const char *path)
{
    return strncmp(path, "mc0:", 4) == 0 ||
           strncmp(path, "mc1:", 4) == 0;
}


static void open_rename_keyboard(void)
{
    int max_length;

    max_length =
        is_memory_card_path(rename_path)
            ? MC_MAX_NAME
            : NAME_MAX_TYPED;

    /*
     * Nome atual mais longo que o limite de digitacao:
     * aceita ate o tamanho dele.
     */
    if ((int)strlen(rename_name) > max_length)
        max_length = strlen(rename_name);

    osk_open("RENOMEAR", rename_name, max_length);

    keyboard_action = KEYBOARD_RENAME;
}


/*
 * Menu R1 -> Renomear: o item selecionado (um so) ou, sem
 * selecao, o item do cursor.
 */
static void start_rename(void)
{
    FMEntry *entry = NULL;
    int selected;
    int i;


    selected = fm_selected_count(&file_manager);

    if (selected > 1) {

        set_status("Renomear: selecione so um item.");

        return;
    }

    if (selected == 1) {

        for (i = 0; i < file_manager.count; i++) {

            if (file_manager.entries[i].selected) {

                entry = &file_manager.entries[i];

                break;
            }
        }

    } else if (file_manager.count > 0) {

        entry = &file_manager.entries[file_manager.cursor];
    }

    if (!entry) {

        set_status("Nenhum item para renomear.");

        return;
    }

    if (strlen(entry->name) > OSK_MAX_TEXT) {

        set_status("Nome atual longo demais para o teclado.");

        return;
    }


    snprintf(rename_path, sizeof(rename_path), "%s", entry->path);
    snprintf(rename_name, sizeof(rename_name), "%s", entry->name);


    /*
     * Arquivo de sistema do save: confirmacao com TRIANGULO
     * antes de abrir o teclado.
     */
    if (is_system_name(rename_name)) {

        confirm_action = CONFIRM_RENAME;

        set_status("Confirme: renomear arquivo de sistema.");

        return;
    }

    open_rename_keyboard();
}


static void rename_item(const char *new_name)
{
    char new_path[FM_MAX_PATH];
    int result;


    if (!name_is_valid(new_name)) {

        set_status("Nome invalido.");

        return;
    }

    if (strcmp(new_name, rename_name) == 0) {

        set_status("Nome nao mudou.");

        return;
    }

    /*
     * So mudar maiusculas/minusculas do proprio item e permitido.
     */
    if (strcasecmp(new_name, rename_name) != 0 &&
        name_exists(new_name)) {

        snprintf(
            status_message,
            sizeof(status_message),
            "Ja existe: %s",
            new_name
        );

        return;
    }

    if (!build_destination_path(
            file_manager.current_path,
            new_name,
            new_path,
            sizeof(new_path))) {

        set_status("Caminho invalido.");

        return;
    }


    errno = 0;

    if (is_memory_card_path(rename_path))
        result = memorycard_rename(rename_path, new_name);
    else
        result = rename(rename_path, new_path);

    printf(
        "FM RENAME: %s -> %s | %d (errno %d)\n",
        rename_path,
        new_path,
        result,
        errno
    );


    if (hdd_mounted_count() > 0)
        hdd_sync_all();

    fm_refresh(&file_manager);


    if (result < 0) {

        snprintf(
            status_message,
            sizeof(status_message),
            "Erro ao renomear %s (%d)",
            rename_name,
            is_memory_card_path(rename_path) ? result : errno
        );

        return;
    }

    snprintf(
        status_message,
        sizeof(status_message),
        "Renomeado: %s -> %s",
        rename_name,
        new_name
    );
}


/* ============================================================
 * Criar particao no HDD (lista de particoes, R1)
 * ============================================================ */

static void partition_name_entered(const char *name)
{
    if (!name_is_valid(name) || strchr(name, ',')) {

        /*
         * Virgula separa os campos em "hdd0:nome,,,tamanho,PFS".
         */
        set_status("Nome invalido (sem virgula, sem espaco nas pontas).");

        return;
    }

    if (name_exists(name)) {

        snprintf(
            status_message,
            sizeof(status_message),
            "Ja existe: %s",
            name
        );

        return;
    }


    partition_free_mb = hdd_free_mb() / 128 * 128;

    if (partition_free_mb < 128) {

        set_status("Sem espaco livre no HDD (minimo 128 MB).");

        return;
    }

    snprintf(partition_name, sizeof(partition_name), "%s", name);

    partition_size_mb =
        partition_free_mb < 1024
            ? partition_free_mb
            : 1024;

    partition_step = PARTITION_SIZE;
}


static void create_partition(void)
{
    int result;


    scr_clear();

    scr_printf("PS2FileManager\n");
    scr_printf("===============\n\n");
    scr_printf("Criando particao %s (%u MB)...\n", partition_name, partition_size_mb);


    result = hdd_create_partition(partition_name, partition_size_mb);

    printf("FM PARTITION: %s %u MB -> %d\n", partition_name, partition_size_mb, result);


    fm_refresh(&file_manager);


    if (result < 0) {

        snprintf(
            status_message,
            sizeof(status_message),
            "ERRO ao criar particao: %s",
            hdd_error()
        );

        return;
    }

    snprintf(
        status_message,
        sizeof(status_message),
        "Particao %s criada: %d MB (PFS).",
        partition_name,
        result
    );
}


/*
 * Excluir: a particao selecionada (uma so) ou a do cursor.
 */
static void start_partition_delete(void)
{
    FMEntry *entry = NULL;
    int selected;
    int i;


    selected = fm_selected_count(&file_manager);

    if (selected > 1) {

        set_status("Excluir particao: selecione so uma.");

        return;
    }

    if (selected == 1) {

        for (i = 0; i < file_manager.count; i++) {

            if (file_manager.entries[i].selected) {

                entry = &file_manager.entries[i];

                break;
            }
        }

    } else if (file_manager.count > 0) {

        entry = &file_manager.entries[file_manager.cursor];
    }

    if (!entry) {

        set_status("Nenhuma particao.");

        return;
    }


    /*
     * __mbr, __net, __system, __sysconf, __common...: do sistema.
     */
    if (strncmp(entry->name, "__", 2) == 0) {

        snprintf(
            status_message,
            sizeof(status_message),
            "%s e particao do sistema: nao pode ser excluida.",
            entry->name
        );

        return;
    }

    snprintf(partition_name, sizeof(partition_name), "%s", entry->name);

    partition_step = PARTITION_CONFIRM_DELETE;
}


static void delete_partition(void)
{
    int result;


    /*
     * O clipboard pode apontar para a particao (montada em pfsN:).
     */
    if (clipboard.count > 0 &&
        hdd_partition_of(clipboard.paths[0]) &&
        strcmp(hdd_partition_of(clipboard.paths[0]), partition_name) == 0)
        clipboard_clear(&clipboard);


    result = hdd_remove_partition(partition_name);

    printf("FM PARTITION: excluir %s -> %d\n", partition_name, result);

    fm_refresh(&file_manager);


    if (result < 0) {

        snprintf(
            status_message,
            sizeof(status_message),
            "ERRO ao excluir %s: %s",
            partition_name,
            hdd_error()
        );

        return;
    }

    snprintf(
        status_message,
        sizeof(status_message),
        "Particao %s excluida.",
        partition_name
    );
}


static void update_partition(void)
{
    if (pressed(PAD_CIRCLE)) {

        partition_step = PARTITION_NONE;

        set_status("Cancelado.");

        return;
    }


    if (partition_step == PARTITION_MENU) {

        if (pressed(PAD_UP) && partition_cursor > 0)
            partition_cursor--;

        if (pressed(PAD_DOWN) && partition_cursor < PARTITION_MENU_COUNT - 1)
            partition_cursor++;

        if (pressed(PAD_CROSS)) {

            partition_step = PARTITION_NONE;

            if (partition_cursor == PARTITION_MENU_CREATE) {

                osk_open("CRIAR PARTICAO: nome", "", APA_MAX_NAME);

                keyboard_action = KEYBOARD_PARTITION;

            } else if (partition_cursor == PARTITION_MENU_DELETE) {

                start_partition_delete();
            }
        }

        return;
    }


    if (partition_step == PARTITION_CONFIRM_DELETE) {

        if (pressed(PAD_CROSS))
            set_status("Excluir particao: use TRIANGULO para confirmar.");

        if (pressed(PAD_TRIANGLE)) {

            partition_step = PARTITION_NONE;

            delete_partition();
        }

        return;
    }


    /*
     * PARTITION_SIZE: multiplos de 128 MB ate o espaco livre.
     */
    if (pressed(PAD_LEFT) && partition_size_mb > 128)
        partition_size_mb -= 128;

    if (pressed(PAD_RIGHT) && partition_size_mb + 128 <= partition_free_mb)
        partition_size_mb += 128;

    if (pressed(PAD_L1))
        partition_size_mb = partition_size_mb > 1024 + 128 ? partition_size_mb - 1024 : 128;

    if (pressed(PAD_R1))
        partition_size_mb =
            partition_size_mb + 1024 <= partition_free_mb
                ? partition_size_mb + 1024
                : partition_free_mb;

    if (pressed(PAD_CROSS)) {

        partition_step = PARTITION_NONE;

        create_partition();
    }
}


static void draw_partition(void)
{
    scr_printf("PS2FileManager\n");
    scr_printf("===============\n");
    scr_printf("HDD: lista de particoes\n\n");


    if (partition_step == PARTITION_MENU) {

        scr_printf("%s Criar particao\n", partition_cursor == 0 ? ">" : " ");
        scr_printf("%s Excluir particao\n", partition_cursor == 1 ? ">" : " ");
        scr_printf("%s Cancelar\n\n", partition_cursor == 2 ? ">" : " ");

        scr_printf("Excluir age na particao selecionada ou,\n");
        scr_printf("sem selecao, na do cursor.\n\n");

        scr_printf("X confirmar | O cancelar\n");

        return;
    }


    if (partition_step == PARTITION_CONFIRM_DELETE) {

        scr_printf("EXCLUIR A PARTICAO?\n\n");
        scr_printf("  %s\n\n", partition_name);

        scr_printf("A particao e todas as subparticoes serao\n");
        scr_printf("apagadas, com TODO o conteudo (arquivos ou\n");
        scr_printf("jogo HDLoader). NAO pode ser desfeito.\n\n");

        scr_printf("%s\n\n", status_message);

        scr_printf("TRIANGULO excluir | O cancelar\n");

        return;
    }


    scr_printf("CRIAR PARTICAO: %s\n\n", partition_name);

    scr_printf("+--------------------------------------------+\n");
    scr_printf("|  Tamanho escolhido : %6u MB              |\n", partition_size_mb);
    scr_printf("|  Maximo disponivel : %6u MB              |\n", partition_free_mb);
    scr_printf("+--------------------------------------------+\n");

    /*
     * Barra: tamanho escolhido em relacao ao maximo.
     */
    draw_progress_bar(
        11,
        "Tamanho",
        partition_size_mb,
        partition_free_mb
    );

    scr_setXY(0, 13);

    scr_printf("  128 MB  <- ESQUERDA   DIREITA ->  %u MB\n\n", partition_free_mb);

    scr_printf("ESQ/DIR -/+ 128 MB | L1/R1 -/+ 1 GB\n");
    scr_printf("Formato PFS; acima do maximo por particao o\n");
    scr_printf("resto vira subparticoes (como o wLaunchELF).\n\n");

    scr_printf("X criar | O cancelar\n");
}


static void update_keyboard(void)
{
    KeyboardAction action;
    OskResult result;


    result = osk_update(new_pad & ~old_pad);

    if (result == OSK_EDITING)
        return;


    action = keyboard_action;

    keyboard_action = KEYBOARD_NONE;


    if (result == OSK_CANCEL) {

        set_status("Cancelado.");

        return;
    }

    if (action == KEYBOARD_MKDIR)
        create_folder(osk_text());
    else if (action == KEYBOARD_RENAME)
        rename_item(osk_text());
    else if (action == KEYBOARD_PARTITION)
        partition_name_entered(osk_text());
}


/* ============================================================
 * Menu de operações
 * ============================================================ */

static void execute_operation_menu(void)
{
    switch (
        operation_menu_cursor
    ) {

        /*
         * COPIAR
         */
        case 0:

            copy_selection();

            operation_menu_open = 0;

            break;


        /*
         * MOVER
         *
         * Continua desativado.
         */
        case 1:

            operation_menu_open = 0;

            /*
             * Recortar: so marca os itens. Nada e apagado
             * antes do Colar + confirmacao + copia verificada.
             */
            if (clipboard_from_selection(
                    &clipboard,
                    &file_manager,
                    CLIPBOARD_MOVE) <= 0) {

                set_status(
                    "Nenhum item selecionado."
                );

                break;
            }

            snprintf(
                status_message,
                sizeof(status_message),
                "%d item(ns) para mover. Abra o destino e Colar.",
                clipboard.count
            );

            break;


        /*
         * COLAR
         */
        case 2:

            operation_menu_open = 0;

            paste_clipboard();

            break;


        /*
         * CRIAR PASTA: abre o teclado virtual.
         */
        case 3:

            operation_menu_open = 0;

            osk_open(
                "CRIAR PASTA",
                "",
                strncmp(file_manager.current_path, "mc", 2) == 0
                    ? MC_MAX_NAME
                    : NAME_MAX_TYPED
            );

            keyboard_action = KEYBOARD_MKDIR;

            break;


        /*
         * RENOMEAR
         */
        case 4:

            operation_menu_open = 0;

            start_rename();

            break;


        /*
         * EXCLUIR
         */
        case 5:

            operation_menu_open = 0;

            if (fm_selected_count(
                    &file_manager) <= 0) {

                set_status(
                    "Nenhum item selecionado."
                );

                break;
            }

            /*
             * Nada e apagado aqui: so abre a confirmacao.
             */
            confirm_action = CONFIRM_DELETE;

            set_status(
                "Confirme a exclusao."
            );

            break;


        /*
         * SELECIONAR TODOS
         */
        case 6:

            fm_select_all(
                &file_manager
            );

            set_status(
                "Todos selecionados."
            );

            operation_menu_open = 0;

            break;


        /*
         * DESMARCAR TODOS
         */
        case 7:

            fm_clear_selection(
                &file_manager
            );

            set_status(
                "Selecao removida."
            );

            operation_menu_open = 0;

            break;


        /*
         * INSTALAR NO HDD (HDLoader): por enquanto so mostra
         * o que seria instalado.
         */
        case MENU_HDL:

            operation_menu_open = 0;

            if (!hdl_available) {

                set_status(
                    hdl_reason
                );

                break;
            }

            hdl_info_open = 1;

            break;


        /*
         * CANCELAR
         */
        case 9:

        default:

            operation_menu_open = 0;

            break;
    }
}


/* ============================================================
 * Exclusao e confirmacao (Etapa 4)
 * ============================================================ */

static void delete_selection(void)
{
    int i;
    int result;
    int deleted;

    FMEntry *entry;
    FileDeleteStatus delete_status;


    deleted = 0;


    for (i = 0;
         i < file_manager.count;
         i++) {

        entry =
            &file_manager.entries[i];

        if (!entry->selected)
            continue;


        result =
            fileio_delete(
                entry->path,
                &delete_status
            );


        printf(
            "FM DELETE: %s | %s | err %d | sys %d | arquivos %d | pastas %d\n",
            entry->path,
            delete_status.stage,
            delete_status.error,
            delete_status.system_result,
            delete_status.files_deleted,
            delete_status.directories_deleted
        );


        if (result !=
            FILEIO_COPY_OK) {

            snprintf(
                status_message,
                sizeof(status_message),
                "ERRO EXCLUIR %s | err %d | sys %d | %s",
                delete_status.stage,
                delete_status.error,
                delete_status.system_result,
                fileio_error_string(
                    delete_status.error
                )
            );

            /*
             * Para no primeiro erro. Itens ja excluidos
             * continuam excluidos; os demais ficam intactos.
             */
            fm_refresh(
                &file_manager
            );

            clipboard_clear(
                &clipboard
            );

            return;
        }


        deleted++;
    }


    fm_refresh(
        &file_manager
    );

    /*
     * O clipboard pode apontar para itens que
     * deixaram de existir.
     */
    clipboard_clear(
        &clipboard
    );


    snprintf(
        status_message,
        sizeof(status_message),
        "EXCLUIDO: %d item(ns)",
        deleted
    );
}


static int confirm_has_system(void)
{
    if (confirm_action == CONFIRM_RENAME)
        return 1;

    /*
     * Copiar nao apaga nada: X confirma sempre.
     */
    if (confirm_action == CONFIRM_PASTE)
        return 0;

    if (confirm_action == CONFIRM_DELETE)
        return selection_has_system();

    return clipboard_has_system();
}


static void update_confirm(void)
{
    ConfirmAction action;
    int has_system;


    if (pressed(PAD_CIRCLE)) {

        confirm_action = CONFIRM_NONE;

        set_status(
            "Operacao cancelada."
        );

        return;
    }


    if (confirm_action == CONFIRM_PASTE &&
        pressed(PAD_SQUARE)) {

        paste_verify = !paste_verify;

        return;
    }


    /*
     * Com arquivos de sistema do save, so TRIANGULO
     * confirma (evita confirmar no reflexo com X).
     */
    has_system =
        confirm_has_system();


    if (has_system &&
        pressed(PAD_CROSS)) {

        set_status(
            "Arquivos de sistema: use TRIANGULO para confirmar."
        );

        return;
    }


    if (( has_system && pressed(PAD_TRIANGLE)) ||
        (!has_system && pressed(PAD_CROSS))) {

        action = confirm_action;

        confirm_action = CONFIRM_NONE;

        if (action == CONFIRM_DELETE) {

            delete_selection();

            if (hdd_mounted_count() > 0)
                hdd_sync_all();

        } else if (action == CONFIRM_RENAME) {

            open_rename_keyboard();

        } else {

            run_paste();
        }

        return;
    }
}


static void draw_confirm(void)
{
    int i;
    int shown;
    int total;
    int has_system;

    const char *name;


    has_system =
        confirm_has_system();


    scr_printf(
        "PS2FileManager\n"
    );

    scr_printf(
        "===============\n\n"
    );


    if (confirm_action == CONFIRM_PASTE) {

        scr_printf(
            "COLAR %d ITEM(NS) EM:\n  %s\n\n",
            clipboard.count,
            file_manager.current_path
        );

        for (i = 0; i < clipboard.count && i < 10; i++)
            scr_printf("  %s\n", path_filename(clipboard.paths[i]));

        if (clipboard.count > 10)
            scr_printf("  ... e mais %d\n", clipboard.count - 10);

        scr_printf(
            "\nConferir copia (relendo o destino): %s\n",
            paste_verify ? "SIM" : "NAO (mais rapido)"
        );

        scr_printf("\nX colar | QUADRADO conferir sim/nao | O cancelar\n");

        return;
    }


    if (confirm_action == CONFIRM_RENAME) {

        scr_printf("RENOMEAR ARQUIVO DE SISTEMA?\n\n");
        scr_printf("  %s  [SISTEMA]\n\n", rename_name);
        scr_printf("icon.sys, .icn e B?DATA-SYSTEM sao lidos pela BIOS\n");
        scr_printf("pelo nome. Com outro nome o save pode ficar\n");
        scr_printf("sem icone ou \"corrompido\".\n\n");
        scr_printf("TRIANGULO continuar | O cancelar\n");

        return;
    }


    if (confirm_action == CONFIRM_DELETE) {

        total =
            fm_selected_count(
                &file_manager
            );

        scr_printf(
            "EXCLUIR %d ITEM(NS)?\n\n",
            total
        );

    } else {

        total =
            clipboard.count;

        scr_printf(
            "MOVER %d ITEM(NS) PARA:\n  %s\n\n",
            total,
            file_manager.current_path
        );
    }


    shown = 0;


    if (confirm_action == CONFIRM_DELETE) {

        for (i = 0;
             i < file_manager.count && shown < 10;
             i++) {

            if (!file_manager.entries[i].selected)
                continue;

            name =
                file_manager.entries[i].name;

            scr_printf(
                "  %s%s%s\n",
                file_manager.entries[i].is_dir
                    ? "<DIR> "
                    : "",
                name,
                is_system_name(name)
                    ? "  [SISTEMA]"
                    : ""
            );

            shown++;
        }

    } else {

        for (i = 0;
             i < clipboard.count && shown < 10;
             i++) {

            name =
                path_filename(
                    clipboard.paths[i]
                );

            scr_printf(
                "  %s%s\n",
                name,
                is_system_name(name)
                    ? "  [SISTEMA]"
                    : ""
            );

            shown++;
        }
    }


    if (total > shown) {

        scr_printf(
            "  ... e mais %d\n",
            total - shown
        );
    }


    if (confirm_action == CONFIRM_DELETE) {

        scr_printf(
            "\nPastas sao excluidas com todo o conteudo.\n"
        );

        scr_printf(
            "Esta operacao NAO pode ser desfeita.\n"
        );

    } else {

        scr_printf(
            "\nA origem so e excluida depois da copia\n"
        );

        scr_printf(
            "verificada byte a byte (sempre, no mover).\n"
        );
    }


    if (has_system) {

        scr_printf(
            "\nATENCAO: inclui arquivos de SISTEMA do save\n"
        );

        scr_printf(
            "(icon.sys, .icn, B?DATA-SYSTEM).\n"
        );

        scr_printf(
            "O save pode ficar corrompido.\n\n"
        );

        scr_printf(
            "TRIANGULO confirmar | O cancelar\n"
        );

    } else {

        scr_printf(
            "\nX confirmar | O cancelar\n"
        );
    }
}


/* ============================================================
 * Controle do menu R1
 * ============================================================ */

static void update_operation_menu(void)
{
    if (pressed(PAD_UP)) {

        if (operation_menu_cursor > 0)
            operation_menu_cursor--;
    }


    if (pressed(PAD_DOWN)) {

        if (operation_menu_cursor <
            MENU_ITEM_COUNT - 1) {

            operation_menu_cursor++;
        }
    }


    if (pressed(PAD_CROSS)) {

        execute_operation_menu();

        return;
    }


    if (pressed(PAD_CIRCLE)) {

        operation_menu_open = 0;

        return;
    }


    if (pressed(PAD_R1)) {

        operation_menu_open = 0;

        return;
    }
}


/* ============================================================
 * Formatacao (lista de dispositivos, R1)
 * ============================================================ */

static void format_add_option(const char *label, FormatAction action)
{
    if (format_option_count >= FORMAT_MAX_OPTIONS)
        return;

    format_labels[format_option_count] = label;
    format_actions[format_option_count] = action;
    format_option_count++;
}


static void format_build_menu(void)
{
    format_option_count = 0;
    format_cursor = 0;

    if (!format_usb && !format_hdd_fat) {

        /*
         * HDD APA (ou vazio): APA, ou tabela para exFAT/FAT32 (OPL).
         */
        format_add_option("APA (padrao PS2)", FORMAT_ACT_APA);
        format_add_option("Criar tabela MBR (exFAT/FAT32)", FORMAT_ACT_TABLE_MBR);
        format_add_option("Criar tabela GPT (exFAT/FAT32)", FORMAT_ACT_TABLE_GPT);

    } else if (format_disks[format_disk].table == FATFMT_TABLE_NONE ||
               format_disks[format_disk].table == FATFMT_TABLE_EMPTY) {

        format_add_option("Criar tabela MBR", FORMAT_ACT_TABLE_MBR);
        format_add_option("Criar tabela GPT", FORMAT_ACT_TABLE_GPT);

    } else {

        format_add_option("Formatar em FAT32", FORMAT_ACT_FAT32);
        format_add_option("Formatar em exFAT", FORMAT_ACT_EXFAT);
        format_add_option("Recriar tabela MBR (apaga tudo)", FORMAT_ACT_TABLE_MBR);
        format_add_option("Recriar tabela GPT (apaga tudo)", FORMAT_ACT_TABLE_GPT);
    }

    /*
     * HDD em MBR/GPT: voltar ao padrao do PS2.
     */
    if (format_hdd_fat)
        format_add_option("APA (padrao PS2, apaga tudo)", FORMAT_ACT_APA);

    format_add_option("Cancelar", FORMAT_ACT_CANCEL);
}


/*
 * USB: pilha USB + fatfmt.irx e lista dos pendrives (discos
 * inteiros, tambem os sem tabela ou sem FAT).
 */
static void start_format_usb(void)
{
    if (usb_init() < 0 || !usbfmt_ready()) {

        snprintf(
            status_message,
            sizeof(status_message),
            "Formatacao USB indisponivel: %s",
            usb_ready() ? "fatfmt.irx nao carregou" : usb_error()
        );

        return;
    }

    format_disk_count = usbfmt_list(format_disks, FATFMT_MAX_DISKS);

    if (format_disk_count <= 0) {

        set_status("Nenhum pendrive encontrado.");

        return;
    }

    format_usb = 1;
    format_hdd_fat = 0;
    format_disk = 0;
    format_cursor = 0;
    format_step = FORMAT_DISKS;
}


static void start_format(void)
{
    if (device_cursor == 0 || device_cursor == 1) {

        set_status("Memory Card nunca e formatado pelo programa.");

        return;
    }

    if (device_cursor == DEVICE_SMB || device_cursor == DEVICE_CONFIG) {

        set_status("Este item nao pode ser formatado.");

        return;
    }

    if (device_cursor >= DEVICE_USB0) {

        start_format_usb();

        return;
    }


    /*
     * HDD: APA/vazio carrega o driver APA; MBR/GPT fica so no BDM
     * (exFAT/FAT32) e e formatado pelo fatfmt.
     */
    format_usb = 0;
    format_hdd_fat = 0;

    if (hdd_init() < 0) {

        format_hdd_fat =
            hdd_layout_table() == FATFMT_TABLE_MBR ||
            hdd_layout_table() == FATFMT_TABLE_GPT;

        if (!format_hdd_fat) {

            snprintf(
                status_message,
                sizeof(status_message),
                "HDD indisponivel: %s",
                hdd_error()
            );

            return;
        }
    }

    if (usbfmt_hdd_disk(&format_disks[0]) < 0) {

        set_status("HDD nao encontrado no BDM (ata_bd/fatfmt).");

        return;
    }

    format_disk_count = 1;
    format_disk = 0;

    if (format_hdd_fat) {

        printf(
            "FM FORMAT: HDD %u MB, %s, %s\n",
            usbfmt_size_mb(&format_disks[0]),
            usbfmt_table_name(format_disks[0].table),
            usbfmt_fs_name(format_disks[0].fs)
        );

        format_build_menu();
        format_step = FORMAT_MENU;

        return;
    }

    format_total_mb = hdd_total_mb();
    format_formatted = hdd_is_formatted();
    format_partitions = format_formatted ? hdd_partition_count() : 0;

    printf(
        "FM FORMAT: HDD %u MB, APA %s, %d particao(oes)\n",
        format_total_mb,
        format_formatted ? "sim" : "nao",
        format_partitions
    );

    format_usb = 0;
    format_build_menu();
    format_step = FORMAT_MENU;
}


/*
 * Progresso por etapa: o driver nao informa progresso dentro
 * de cada format (cada um leva poucos segundos).
 */
static void format_step_draw(int step, int total, const char *what)
{
    draw_progress_bar(
        6,
        "Etapas",
        (unsigned long long)step,
        (unsigned long long)total
    );

    scr_setXY(0, 8);

    scr_printf(
        "Etapa %d de %d: %-30s\n",
        step < total ? step + 1 : total,
        total,
        what
    );

    printf("FM FORMAT: etapa %d/%d %s\n", step, total, what);
}


static void run_format_apa(void)
{
    int result;


    scr_clear();

    scr_printf("PS2FileManager\n");
    scr_printf("===============\n\n");
    scr_printf("FORMATANDO O HDD EM APA...\n");
    scr_printf("Nao desligue o console.\n");


    /*
     * O clipboard pode apontar para particoes que vao sumir.
     */
    if (clipboard.count > 0 &&
        (strncmp(clipboard.paths[0], "pfs", 3) == 0 ||
         strncmp(clipboard.paths[0], "hdd", 3) == 0))
        clipboard_clear(&clipboard);


    result = hdd_format_apa(format_step_draw);

    printf("FM FORMAT: APA -> %d\n", result);


    if (result < 0) {

        snprintf(
            status_message,
            sizeof(status_message),
            "ERRO ao formatar: %s",
            hdd_error()
        );

        return;
    }

    snprintf(
        status_message,
        sizeof(status_message),
        "HDD formatado em APA: %d particao(oes).",
        hdd_partition_count()
    );
}


static const char *format_action_text(FormatAction action)
{
    switch (action) {
        case FORMAT_ACT_APA:       return "formatar em APA";
        case FORMAT_ACT_FAT32:     return "formatar em FAT32";
        case FORMAT_ACT_EXFAT:     return "formatar em exFAT";
        case FORMAT_ACT_TABLE_MBR: return "criar tabela MBR";
        case FORMAT_ACT_TABLE_GPT: return "criar tabela GPT";
        default:                   return "";
    }
}


/*
 * Pendrive ou HDD (disco "ata"): tabela (uma particao no disco
 * inteiro) ou formatacao da 1a particao. O BDM desmonta e monta de
 * novo (fatfmt.irx).
 */
static void run_format_usb(void)
{
    const fatfmt_disk_t *disk = &format_disks[format_disk];
    int is_table;
    int result;


    is_table =
        format_chosen == FORMAT_ACT_TABLE_MBR ||
        format_chosen == FORMAT_ACT_TABLE_GPT;


    scr_clear();

    scr_printf("PS2FileManager\n");
    scr_printf("===============\n\n");
    scr_printf(
        "%s%u: %s...\n\n",
        disk->name,
        disk->dev_nr,
        format_action_text(format_chosen)
    );
    scr_printf(format_usb ? "Aguarde. Nao remova o pendrive.\n" : "Aguarde. Nao desligue o console.\n");


    /*
     * O clipboard pode apontar para o disco (usbN:, ata0:, pfsN:).
     */
    if (clipboard.count > 0 &&
        (format_usb
            ? strncmp(clipboard.paths[0], USB_PREFIX, strlen(USB_PREFIX)) == 0
            : (strncmp(clipboard.paths[0], "ata", 3) == 0 ||
               strncmp(clipboard.paths[0], "pfs", 3) == 0 ||
               strncmp(clipboard.paths[0], "hdd", 3) == 0)))
        clipboard_clear(&clipboard);


    /*
     * HDD APA com o driver carregado: desmonta as particoes antes
     * de trocar o disco por MBR/GPT.
     */
    if (!format_usb && !format_hdd_fat) {
        hdd_sync_all();
        hdd_unmount_all();
    }


    if (is_table)
        result = usbfmt_table(
            disk,
            format_chosen == FORMAT_ACT_TABLE_GPT ? FATFMT_TABLE_GPT : FATFMT_TABLE_MBR
        );
    else
        result = usbfmt_format(
            disk,
            format_chosen == FORMAT_ACT_EXFAT ? FATFMT_FS_EXFAT : FATFMT_FS_FAT32
        );


    if (format_usb) {

        scr_printf("\nEsperando o pendrive montar de novo...\n");

        usb_wait_mount();

        refresh_devices();

    } else {

        /*
         * HDD agora e MBR/GPT: o driver APA deixa de ser usado e o
         * disco abre como ata0:.
         */
        if (result == 0 && is_table)
            hdd_set_layout(format_chosen == FORMAT_ACT_TABLE_GPT ? FATFMT_TABLE_GPT : FATFMT_TABLE_MBR);

        if (result == 0 && !is_table) {
            scr_printf("\nEsperando o HDD montar...\n");
            hdd_fat_mounted();
        }
    }


    if (result < 0) {

        snprintf(
            status_message,
            sizeof(status_message),
            "ERRO ao %s: %s (%d)",
            format_action_text(format_chosen),
            usbfmt_error_text(result),
            result
        );

        return;
    }

    if (format_chosen == FORMAT_ACT_TABLE_MBR || format_chosen == FORMAT_ACT_TABLE_GPT) {

        snprintf(
            status_message,
            sizeof(status_message),
            "Tabela %s criada. R1 de novo para formatar em FAT32/exFAT.",
            format_chosen == FORMAT_ACT_TABLE_GPT ? "GPT" : "MBR"
        );

    } else {

        snprintf(
            status_message,
            sizeof(status_message),
            "%s formatado em %s.",
            format_usb ? "Pendrive" : "HDD",
            format_chosen == FORMAT_ACT_EXFAT ? "exFAT" : "FAT32"
        );
    }
}


/*
 * HDD -> APA. Se o disco esta em MBR/GPT, as tabelas sao apagadas
 * primeiro (fatfmt WIPE): o disco fica "vazio" e so entao o driver
 * APA e carregado.
 */
static void run_format_hdd_apa(void)
{
    int result;

    if (format_hdd_fat) {

        if (clipboard.count > 0 && strncmp(clipboard.paths[0], "ata", 3) == 0)
            clipboard_clear(&clipboard);

        result = usbfmt_wipe(&format_disks[0]);

        if (result < 0) {

            snprintf(
                status_message,
                sizeof(status_message),
                "ERRO ao apagar tabelas do HDD: %s (%d)",
                usbfmt_error_text(result),
                result
            );

            return;
        }

        hdd_set_layout(FATFMT_TABLE_EMPTY);

        if (hdd_init() < 0) {

            snprintf(
                status_message,
                sizeof(status_message),
                "ERRO ao carregar o driver APA: %s",
                hdd_error()
            );

            return;
        }
    }

    run_format_apa();
}


static void update_format(void)
{
    if (pressed(PAD_CIRCLE)) {

        format_step = FORMAT_NONE;

        set_status("Formatacao cancelada.");

        return;
    }


    switch (format_step) {

        case FORMAT_DISKS:

            if (pressed(PAD_UP) && format_disk > 0)
                format_disk--;

            if (pressed(PAD_DOWN) && format_disk < format_disk_count - 1)
                format_disk++;

            if (pressed(PAD_CROSS)) {

                format_build_menu();

                format_step = FORMAT_MENU;
            }

            break;


        case FORMAT_MENU:

            if (pressed(PAD_UP) && format_cursor > 0)
                format_cursor--;

            if (pressed(PAD_DOWN) && format_cursor < format_option_count - 1)
                format_cursor++;

            if (pressed(PAD_CROSS)) {

                format_chosen = format_actions[format_cursor];

                if (format_chosen == FORMAT_ACT_CANCEL) {

                    format_step = FORMAT_NONE;

                    set_status("Formatacao cancelada.");

                } else if (format_chosen == FORMAT_ACT_SOON) {

                    set_status("FAT32/exFAT no HDD: em breve.");

                } else {

                    format_step = FORMAT_CONFIRM;
                }
            }

            break;


        case FORMAT_CONFIRM:

            if (pressed(PAD_TRIANGLE))
                format_step = FORMAT_CONFIRM2;

            break;


        case FORMAT_CONFIRM2:

            if (pressed(PAD_START)) {

                format_step = FORMAT_NONE;

                if (format_chosen == FORMAT_ACT_APA)
                    run_format_hdd_apa();
                else
                    run_format_usb();
            }

            break;


        default:
            format_step = FORMAT_NONE;
            break;
    }
}


static void draw_format_usb(void)
{
    const fatfmt_disk_t *disk;
    int i;


    scr_printf("PS2FileManager\n");
    scr_printf("===============\n\n");


    if (format_step == FORMAT_DISKS) {

        scr_printf("FORMATAR PENDRIVE: escolha o disco\n\n");

        for (i = 0; i < format_disk_count; i++) {

            disk = &format_disks[i];

            scr_printf(
                "%s %s%u  %6u MB  %-10s %s\n",
                i == format_disk ? ">" : " ",
                disk->name,
                disk->dev_nr,
                usbfmt_size_mb(disk),
                usbfmt_table_name(disk->table),
                usbfmt_fs_name(disk->fs)
            );
        }

        scr_printf("\nO numero (usb0, usb1) e o do BDM: pode nao bater\n");
        scr_printf("com o usb0:/usb1: da lista (ordem de montagem).\n\n");

        scr_printf("X escolher | O cancelar\n");

        return;
    }


    disk = &format_disks[format_disk];

    if (format_usb)
        scr_printf("FORMATAR: pendrive %s%u\n\n", disk->name, disk->dev_nr);
    else
        scr_printf("FORMATAR: HDD interno (%s%u)\n\n", disk->name, disk->dev_nr);

    scr_printf("Capacidade : %u MB\n", usbfmt_size_mb(disk));
    scr_printf("Tabela     : %s\n", usbfmt_table_name(disk->table));
    scr_printf("Formato    : %s\n\n", usbfmt_fs_name(disk->fs));


    if (format_step == FORMAT_MENU) {

        if (disk->table == FATFMT_TABLE_NONE) {
            scr_printf("Sua midia nao possui tabela de particoes\n");
            scr_printf("GPT ou MBR. Deseja criar?\n\n");
        }

        for (i = 0; i < format_option_count; i++)
            scr_printf("%s %s\n", i == format_cursor ? ">" : " ", format_labels[i]);

        scr_printf("\nX escolher | O cancelar\n");

        return;
    }


    scr_printf("ACAO: %s\n\n", format_action_text(format_chosen));

    if (format_chosen == FORMAT_ACT_APA) {
        scr_printf("ATENCAO: TODOS OS DADOS DO HDD SERAO APAGADOS.\n");
        scr_printf("As tabelas MBR/GPT sao zeradas e o HDD volta ao\n");
        scr_printf("padrao do PS2 (APA + __net, __system, __sysconf,\n");
        scr_printf("__common).\n\n");
    } else if (format_chosen == FORMAT_ACT_TABLE_MBR || format_chosen == FORMAT_ACT_TABLE_GPT) {
        scr_printf("ATENCAO: TODOS OS DADOS DO %s SERAO\n", format_usb ? "PENDRIVE" : "HDD (APA, JOGOS)");
        scr_printf("APAGADOS. Sera criada uma particao no disco\n");
        scr_printf("inteiro; depois formate em FAT32 ou exFAT.\n\n");
    } else {
        scr_printf("ATENCAO: TODOS OS DADOS DA PARTICAO SERAO\n");
        scr_printf("APAGADOS.\n\n");
    }

    if (format_step == FORMAT_CONFIRM) {

        scr_printf("TRIANGULO continuar | O cancelar\n");

    } else {

        scr_printf("ULTIMA CONFIRMACAO.\n\n");
        scr_printf("START EXECUTAR AGORA | O cancelar\n");
    }
}


static void draw_format(void)
{
    int i;


    /*
     * Pendrive, HDD em MBR/GPT, ou HDD APA indo para MBR/GPT.
     */
    if (format_usb ||
        format_hdd_fat ||
        (format_step != FORMAT_MENU && format_chosen != FORMAT_ACT_APA)) {

        draw_format_usb();

        return;
    }


    scr_printf("PS2FileManager\n");
    scr_printf("===============\n\n");
    scr_printf("FORMATAR: HDD interno (hdd0:)\n\n");

    scr_printf("Capacidade : %u MB\n", format_total_mb);

    if (format_formatted)
        scr_printf("Formato    : APA, %d particao(oes)\n\n", format_partitions);
    else
        scr_printf("Formato    : sem formato APA\n\n");


    if (format_step == FORMAT_MENU) {

        for (i = 0; i < format_option_count; i++)
            scr_printf("%s %s\n", i == format_cursor ? ">" : " ", format_labels[i]);

        scr_printf("\n%s\n\n", status_message);

        scr_printf("X escolher | O cancelar\n");

        return;
    }


    scr_printf("ATENCAO: TODOS OS DADOS DO HDD SERAO APAGADOS:\n");
    scr_printf("todas as particoes, jogos HDLoader, saves do\n");
    scr_printf("__common e o que mais houver no disco.\n\n");

    scr_printf("Sera criado: __mbr, __net, __system, __sysconf e\n");
    scr_printf("__common (PFS), como o wLaunchELF.\n\n");


    if (format_step == FORMAT_CONFIRM) {

        scr_printf("TRIANGULO continuar | O cancelar\n");

    } else {

        scr_printf("ULTIMA CONFIRMACAO.\n\n");
        scr_printf("START FORMATAR AGORA | O cancelar\n");
    }
}


/* ============================================================
 * Tela de dispositivos
 * ============================================================ */

static void update_device_screen(void)
{
    if (format_step != FORMAT_NONE) {

        update_format();

        return;
    }


    if (pressed(PAD_R1)) {

        start_format();

        return;
    }


    if (pressed(PAD_UP)) {

        if (device_cursor > 0)
            device_cursor--;
    }


    if (pressed(PAD_DOWN)) {

        if (device_cursor < DEVICE_COUNT - 1)
            device_cursor++;
    }


    if (pressed(PAD_CROSS)) {

        open_device(
            device_cursor
        );

        return;
    }


    /*
     * TRIANGLE:
     *
     * atualiza estado dos Memory Cards.
     */
    if (pressed(PAD_TRIANGLE)) {

        refresh_devices();

        return;
    }
}


/* ============================================================
 * Tela de configuracao de rede e SMB
 * ============================================================ */

static void update_settings_screen(void)
{
    SettingsResult result;


    result = settings_update(new_pad & ~old_pad);

    if (result == SETTINGS_OPEN)
        return;


    current_screen = SCREEN_DEVICES;

    if (result == SETTINGS_CANCELLED) {

        set_status("Configuracao: nada alterado.");

        return;
    }


    /*
     * SMB reconecta com os dados novos na proxima abertura;
     * com a rede ja carregada, o modo de IP vale na hora.
     */
    smb_disconnect();

    if (network_ready && network_apply_config() < 0)
        printf("FM CONFIG: falha ao aplicar a rede\n");


    if (result == SETTINGS_SAVED) {

        snprintf(
            status_message,
            sizeof(status_message),
            "Configuracao salva em %s",
            config_path()
        );

    } else {

        set_status(
            "Configuracao aplicada, mas NAO gravada no Memory Card."
        );
    }
}


/* ============================================================
 * Tela do gerenciador
 * ============================================================ */

static void update_file_screen(void)
{
    int selected;


    if (confirm_action != CONFIRM_NONE) {

        update_confirm();

        return;
    }


    if (keyboard_action != KEYBOARD_NONE) {

        update_keyboard();

        return;
    }


    if (partition_step != PARTITION_NONE) {

        update_partition();

        return;
    }


    if (hdl_info_open) {

        update_hdl_info();

        return;
    }


    if (operation_menu_open) {

        update_operation_menu();

        return;
    }


    /*
     * Navegação
     */
    if (pressed(PAD_UP)) {

        fm_cursor_up(
            &file_manager
        );
    }


    if (pressed(PAD_DOWN)) {

        fm_cursor_down(
            &file_manager
        );
    }


    /*
     * X:
     *
     * entra no diretório.
     *
     * Nesta etapa arquivos comuns não são
     * executados.
     */
    if (pressed(PAD_CROSS) &&
        is_partition_list()) {

        enter_partition();

        return;
    }


    if (pressed(PAD_CROSS)) {

        if (fm_enter(
                &file_manager)) {

            snprintf(
                status_message,
                sizeof(status_message),
                "Diretorio: %s",
                file_manager.current_path
            );
        }

        return;
    }


    /*
     * CIRCLE:
     *
     * volta um diretório.
     *
     * Se já estivermos na raiz do MC,
     * volta para seleção de dispositivos.
     */
    if (pressed(PAD_CIRCLE)) {

        if (!fm_back(
                &file_manager)) {

            if (strncmp(file_manager.current_path, "pfs", 3) == 0) {

                leave_partition();

                return;
            }

            current_screen =
                SCREEN_DEVICES;

            refresh_devices();
        }

        return;
    }


    /*
     * SQUARE:
     *
     * seleciona/desmarca item atual.
     */
    if (pressed(PAD_SQUARE)) {

        fm_toggle_selected(
            &file_manager
        );


        selected =
            fm_selected_count(
                &file_manager
            );


        snprintf(
            status_message,
            sizeof(status_message),
            "Selecionados: %d",
            selected
        );

        return;
    }


    /*
     * TRIANGLE:
     *
     * Se houver algo selecionado,
     * desmarca tudo.
     *
     * Caso contrário,
     * seleciona tudo.
     */
    if (pressed(PAD_TRIANGLE)) {

        selected =
            fm_selected_count(
                &file_manager
            );


        if (selected > 0) {

            fm_clear_selection(
                &file_manager
            );

            set_status(
                "Selecao removida."
            );

        } else {

            fm_select_all(
                &file_manager
            );

            set_status(
                "Todos selecionados."
            );
        }

        return;
    }


    /*
     * R1:
     *
     * abre menu de operações.
     */
    /*
     * Lista de particoes: R1 abre o menu de particoes (criar).
     */
    if (pressed(PAD_R1) &&
        is_partition_list()) {

        partition_cursor = 0;
        partition_step = PARTITION_MENU;

        return;
    }


    if (pressed(PAD_R1)) {

        operation_menu_cursor = 0;

        hdl_check();

        operation_menu_open = 1;

        return;
    }
}


/* ============================================================
 * Desenho da tela de dispositivos
 * ============================================================ */

static void draw_device_screen(void)
{
    int i;

    scr_clear();

    if (format_step != FORMAT_NONE) {

        draw_format();

        return;
    }

    scr_printf(
        "PS2FileManager\n"
    );

    scr_printf(
        "===============\n\n"
    );

    scr_printf(
        "Selecione o dispositivo:\n\n"
    );


    if (device_cursor == 0)
        scr_printf("> ");
    else
        scr_printf("  ");


    scr_printf(
        "Memory Card 1 (mc0:) [%s]\n",
        mc0_available
            ? "OK"
            : "INDISPONIVEL"
    );


    if (device_cursor == 1)
        scr_printf("> ");
    else
        scr_printf("  ");


    scr_printf(
        "Memory Card 2 (mc1:) [%s]\n",
        mc1_available
            ? "OK"
            : "INDISPONIVEL"
    );


    if (device_cursor == DEVICE_SMB)
        scr_printf("> ");
    else
        scr_printf("  ");


    scr_printf(
        "Rede SMB (smb0:) [%s%s/%s]\n",
        smb_get_info()->connected
            ? "CONECTADO "
            : "",
        config_get()->smb_server,
        config_get()->smb_share
    );

    if (device_cursor == DEVICE_HDD)
        scr_printf("> ");
    else
        scr_printf("  ");


    if (hdd_layout_table() == FATFMT_TABLE_MBR ||
        hdd_layout_table() == FATFMT_TABLE_GPT) {

        scr_printf(
            "HDD interno (ata0:) [%s exFAT/FAT32]\n",
            usbfmt_table_name(hdd_layout_table())
        );

    } else {

        scr_printf(
            "HDD interno (hdd0:) [%d montada(s)]\n",
            hdd_mounted_count()
        );
    }

    for (i = 0; i < USB_UNITS; i++) {

        if (device_cursor == DEVICE_USB0 + i)
            scr_printf("> ");
        else
            scr_printf("  ");


        /*
         * Sem "USB 1/2": o BDM numera na ordem de deteccao,
         * nao pela porta (no PCSX2, usb0: e a porta 2).
         */
        scr_printf(
            "Pendrive USB (" USB_PREFIX "%d:) [%s]\n",
            i,
            !usb_ready()
                ? "X para detectar"
                : usb_present[i]
                    ? "OK"
                    : "AUSENTE"
        );
    }

    scr_printf(
        "\n%sConfiguracoes de rede e SMB [%s]\n",
        device_cursor == DEVICE_CONFIG ? "> " : "  ",
        config_get()->dhcp ? "DHCP" : config_get()->ip
    );

    scr_printf(
        "\n\n"
    );

    scr_printf(
        "X        Abrir\n"
    );

    scr_printf(
        "TRIANGLE Atualizar dispositivos\n"
    );

    scr_printf(
        "R1       Formatar (HDD/USB)\n"
    );


    scr_printf(
        "\n%s\n",
        status_message
    );
}


/* ============================================================
 * Desenho do menu R1
 * ============================================================ */

static void draw_operation_menu(void)
{
    int i;

    scr_printf(
        "\n"
    );

    scr_printf(
        "===============================\n"
    );

    scr_printf(
        "OPERACOES\n"
    );

    scr_printf(
        "===============================\n"
    );


    for (i = 0;
         i < MENU_ITEM_COUNT;
         i++) {

        scr_printf(
            "%s %s%s\n",
            i == operation_menu_cursor ? ">" : " ",
            operation_menu[i],
            i == MENU_HDL && !hdl_available
                ? " [indisponivel]"
                : ""
        );
    }


    scr_printf(
        "\nX confirmar | O cancelar\n"
    );
}


/* ============================================================
 * Desenho da tela de arquivos
 * ============================================================ */

static void draw_file_screen(void)
{
    int i;
    int end;

    FMEntry *entry;


    scr_clear();


    /*
     * Confirmacao (excluir/mover) ocupa a tela inteira.
     */
    if (confirm_action != CONFIRM_NONE) {

        draw_confirm();

        return;
    }


    if (keyboard_action != KEYBOARD_NONE) {

        osk_draw();

        return;
    }


    if (partition_step != PARTITION_NONE) {

        draw_partition();

        return;
    }


    if (hdl_info_open) {

        draw_hdl_info();

        return;
    }


    /*
     * Menu R1 tambem em tela propria: desenhado abaixo da
     * lista ele saia da area visivel em pastas com muitos itens.
     */
    if (operation_menu_open) {

        scr_printf(
            "PS2FileManager\n"
        );

        scr_printf(
            "Path: %s\n",
            file_manager.current_path
        );

        scr_printf(
            "Selecionados: %d\n",
            fm_selected_count(
                &file_manager
            )
        );

        draw_operation_menu();

        return;
    }


    scr_printf(
        "PS2FileManager\n"
    );

    scr_printf(
        "===============\n"
    );


    scr_printf(
        "Path: %s\n",
        file_manager.current_path
    );


    scr_printf(
        "Itens: %d | Selecionados: %d\n",
        file_manager.count,
        fm_selected_count(
            &file_manager
        )
    );


    if (clipboard.mode !=
        CLIPBOARD_NONE) {

        scr_printf(
            "Clipboard: %s (%d)\n",
            clipboard.mode ==
                    CLIPBOARD_COPY
                ? "COPY"
                : "MOVE",
            clipboard.count
        );

    } else {

        scr_printf(
            "Clipboard: vazio\n"
        );
    }


    /*
     * Status logo abaixo do cabecalho: no rodape ele ficava
     * fora da area visivel (cerca de 28 linhas de texto).
     */
    scr_printf(
        "Status: %s\n",
        status_message
    );


    scr_printf(
        "--------------------------------\n"
    );


    end =
        file_manager.scroll +
        MAX_VISIBLE_FILES;


    if (end >
        file_manager.count) {

        end =
            file_manager.count;
    }


    for (i =
            file_manager.scroll;
         i < end;
         i++) {

        entry =
            &file_manager.entries[i];


        /*
         * Cursor
         */
        if (i ==
            file_manager.cursor) {

            scr_printf("> ");

        } else {

            scr_printf("  ");
        }


        /*
         * Seleção
         */
        if (entry->selected)
            scr_printf("[X] ");
        else
            scr_printf("[ ] ");


        /*
         * Diretório / arquivo
         */
        if (entry->is_dir) {

            scr_printf(
                "<DIR> %s\n",
                entry->name
            );

        } else {

            scr_printf(
                "      %s  (%u bytes)\n",
                entry->name,
                entry->size
            );
        }
    }


    if (file_manager.count == 0) {

        scr_printf(
            "\nDiretorio vazio.\n"
        );
    }


    scr_printf(
        "\n--------------------------------\n"
    );

    scr_printf(
        "X Abrir | O Voltar | [] Selecionar\n"
    );

    scr_printf(
        "TRIANGLE Todos/Nenhum | R1 Operacoes\n"
    );


}


/* ============================================================
 * Desenho
 * ============================================================ */

static void draw_screen(void)
{
    static char last_logged_status[256];

    /*
     * Registra no log do PCSX2 toda mudanca de status.
     */
    if (strcmp(
            last_logged_status,
            status_message) != 0) {

        printf(
            "FM STATUS: %s\n",
            status_message
        );

        strcpy(
            last_logged_status,
            status_message
        );
    }

    if (current_screen ==
        SCREEN_DEVICES) {

        draw_device_screen();

    } else if (current_screen == SCREEN_SETTINGS) {

        scr_clear();

        settings_draw();

    } else {

        draw_file_screen();
    }
}


/* ============================================================
 * Inicialização principal
 * ============================================================ */

static int initialize(void)
{
    SifInitRpc(0);


    /*
     * Inicializa debug screen.
     */
    init_scr();


    scr_printf(
        "PS2FileManager iniciando...\n"
    );


    /*
     * Carrega módulos ROM.
     */
    load_modules();


    /*
     * Inicializa libmc.
     */
    if (memorycard_init() < 0) {

        scr_printf(
            "Falha ao inicializar Memory Card.\n"
        );

        return 0;
    }


    /*
     * Inicializa controle.
     */
    if (!init_pad()) {

        scr_printf(
            "Falha ao inicializar controle.\n"
        );

        return 0;
    }


    /*
     * Inicializa clipboard.
     */
    clipboard_init(
        &clipboard
    );


    /*
     * Detecta MC0/MC1.
     */
    refresh_devices();


    /*
     * Rede/SMB: mc0:/PS2FM/network.cfg (ou mc1:), senao padrao.
     */
    config_load();


    set_status(
        "PS2FileManager pronto."
    );


    return 1;
}


/* ============================================================
 * MAIN
 * ============================================================ */

int main(int argc, char *argv[])
{
    u32 previous_pad;

    (void)argc;
    (void)argv;

    if (!initialize()) {
        while (1) {
            nopdelay();
        }

        return 0;
    }

    /*
     * Primeiro desenho da tela.
     */
    draw_screen();

    while (1) {

        /*
         * Guarda o estado atual antes
         * de consultar novamente o controle.
         */
        previous_pad = new_pad;

        if (!read_pad()) {
            nopdelay();
            continue;
        }

        /*
         * Nenhum botão mudou.
         *
         * Não processamos a interface e,
         * principalmente, não chamamos
         * draw_screen().
         */
        if (new_pad == previous_pad) {
            nopdelay();
            continue;
        }

        /*
         * read_pad() já fez:
         *
         * old_pad = estado anterior
         * new_pad = estado atual
         *
         * Portanto pressed() continua
         * funcionando normalmente.
         */
        if (current_screen == SCREEN_DEVICES) {
            update_device_screen();
        } else if (current_screen == SCREEN_SETTINGS) {
            update_settings_screen();
        } else {
            update_file_screen();
        }

        /*
         * Redesenha somente depois de
         * alguma mudança no controle.
         */
        draw_screen();
    }

    return 0;
}

