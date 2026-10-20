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

#include "memorycard.h"
#include "filemanager.h"
#include "clipboard.h"
#include "fm_fileio.h"
#include "network.h"
#include "smb.h"
#include "hdd.h"
#include "usb.h"
#include "iso9660.h"



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

#define MENU_ITEM_COUNT     9
#define MENU_HDL            7

/*
 * Instalacao HDLoader: jogos por vez.
 */
#define HDL_MAX_GAMES       16

/*
 * SMB (Etapa 8). Fixo ate existir a tela de configuracao
 * (depende do teclado virtual da Etapa 4c). Mesmos dados
 * usados com sucesso no PS2DVDPlayer.
 */
#define SMB_SERVER          "192.168.1.9"
#define SMB_SHARE           "samba"
#define SMB_USER            "guest"
#define SMB_PASSWORD        ""
#define SMB_DHCP_TIMEOUT    20

#define DEVICE_COUNT        6
#define DEVICE_SMB          2
#define DEVICE_HDD          3
#define DEVICE_USB0         4   /* mass0:, DEVICE_USB0 + 1 = mass1: */


/* ============================================================
 * Estados da interface
 * ============================================================ */

typedef enum {
    SCREEN_DEVICES = 0,
    SCREEN_FILES
} AppScreen;


/* ============================================================
 * Menu R1
 * ============================================================ */

static const char *operation_menu[MENU_ITEM_COUNT] = {
    "Copiar",
    "Recortar (mover)",
    "Colar",
    "Renomear [desativado]",
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

static int operation_menu_open = 0;

/*
 * Confirmacao em tela cheia (Etapa 4).
 */
typedef enum {
    CONFIRM_NONE = 0,
    CONFIRM_DELETE,
    CONFIRM_MOVE
} ConfirmAction;

static ConfirmAction confirm_action = CONFIRM_NONE;
static int operation_menu_cursor = 0;

/*
 * Instalacao HDLoader: verificada ao abrir o menu R1.
 */
static IsoGameInfo hdl_games[HDL_MAX_GAMES];
static char hdl_names[HDL_MAX_GAMES][FM_MAX_NAME];
static int hdl_count = 0;
static int hdl_available = 0;
static int hdl_partitions = 0;
static int hdl_info_open = 0;
static char hdl_reason[96];

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

static int smb_ensure_connected(void)
{
    static int network_ready = 0;

    const NetworkInfo *net;
    const SMBInfo *smb;

    clock_t start;
    clock_t tick;
    int seconds;


    smb = smb_get_info();

    if (smb->connected)
        return 1;


    scr_clear();

    scr_printf("PS2FileManager\n");
    scr_printf("===============\n\n");
    scr_printf("Conectando ao SMB...\n\n");
    scr_printf("Servidor: %s\n", SMB_SERVER);
    scr_printf("Share   : %s\n", SMB_SHARE);
    scr_printf("Usuario : %s\n\n", SMB_USER);


    /*
     * Pilha de rede: carregada uma unica vez.
     */
    if (!network_ready) {

        scr_printf("Iniciando rede (DEV9/SMAP/PS2IP)...\n");

        if (network_init() < 0) {

            set_status(
                "ERRO rede: falha ao carregar modulos."
            );

            return 0;
        }

        if (network_start_dhcp() < 0) {

            set_status(
                "ERRO rede: falha ao iniciar DHCP."
            );

            return 0;
        }

        network_ready = 1;
    }


    scr_printf("Aguardando DHCP");

    start = clock();
    seconds = 0;

    while (1) {

        network_update();

        net = network_get_info();

        if (net->link_up &&
            net->dhcp_bound &&
            strcmp(net->ip, "0.0.0.0") != 0)
            break;

        if (clock() - start >
            (clock_t)SMB_DHCP_TIMEOUT * CLOCKS_PER_SEC) {

            snprintf(
                status_message,
                sizeof(status_message),
                "ERRO: DHCP sem resposta em %ds (link %s)",
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
            SMB_SERVER,
            SMB_SHARE,
            SMB_USER,
            SMB_PASSWORD) < 0) {

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
        SMB_SHARE,
        SMB_SERVER
    );
}


/* ============================================================
 * HDD interno (Etapa 5)
 * ============================================================ */

static int is_partition_list(void)
{
    return strcmp(file_manager.current_path, "hdd0:") == 0;
}


static void open_hdd(void)
{
    scr_clear();

    scr_printf("PS2FileManager\n");
    scr_printf("===============\n\n");
    scr_printf("Iniciando HDD (DEV9/ATAD/APA/PFS)...\n");


    if (hdd_init() < 0) {

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
            "Pendrive mass%d: nao encontrado.",
            unit
        );

        return;
    }


    snprintf(
        root,
        sizeof(root),
        "mass%d:/",
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
            "COPIA VERIFICADA: %d item(ns)",
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


    progress_total = total_bytes * 2ULL;
    progress_done = 0;
    progress_files_total = total_files;
    progress_file_index = 0;
    progress_start = clock();
    progress_last_draw = 0;


    fileio_set_progress(paste_progress);

    run_paste_items();

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


    run_paste();
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

    scr_printf("\nA gravacao no HDD sera a proxima etapa.\n");
    scr_printf("Nada foi alterado.\n\n");

    scr_printf("O voltar\n");
}


static void update_hdl_info(void)
{
    if (pressed(PAD_CIRCLE) ||
        pressed(PAD_CROSS))
        hdl_info_open = 0;
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
         * RENOMEAR
         */
        case 3:

            set_status(
                "Renomear ainda esta desativado."
            );

            operation_menu_open = 0;

            break;


        /*
         * EXCLUIR
         */
        case 4:

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
        case 5:

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
        case 6:

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
        case 8:

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
            "verificada byte a byte.\n"
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
 * Tela de dispositivos
 * ============================================================ */

static void update_device_screen(void)
{
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
 * Tela do gerenciador
 * ============================================================ */

static void update_file_screen(void)
{
    int selected;


    if (confirm_action != CONFIRM_NONE) {

        update_confirm();

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
    if (pressed(PAD_R1) &&
        is_partition_list()) {

        set_status(
            "Lista de particoes: operacoes bloqueadas."
        );

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
        "Rede SMB (smb0:) [%s]\n",
        smb_get_info()->connected
            ? "CONECTADO"
            : SMB_SERVER "/" SMB_SHARE
    );

    if (device_cursor == DEVICE_HDD)
        scr_printf("> ");
    else
        scr_printf("  ");


    scr_printf(
        "HDD interno (hdd0:) [%d montada(s)]\n",
        hdd_mounted_count()
    );

    for (i = 0; i < USB_UNITS; i++) {

        if (device_cursor == DEVICE_USB0 + i)
            scr_printf("> ");
        else
            scr_printf("  ");


        /*
         * Sem "USB 1/2": o BDM numera na ordem de deteccao,
         * nao pela porta (no PCSX2, mass0: e a porta 2).
         */
        scr_printf(
            "Pendrive USB (mass%d:) [%s]\n",
            i,
            !usb_ready()
                ? "X para detectar"
                : usb_present[i]
                    ? "OK"
                    : "AUSENTE"
        );
    }

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

