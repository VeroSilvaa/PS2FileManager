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

#include "memorycard.h"
#include "filemanager.h"
#include "clipboard.h"
#include "fm_fileio.h"
#include "network.h"
#include "smb.h"



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

#define MENU_ITEM_COUNT     8

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

#define DEVICE_COUNT        3
#define DEVICE_SMB          2


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
    mc0_available =
        memorycard_available(0);

    mc1_available =
        memorycard_available(1);


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
 * Abre Memory Card
 * ============================================================ */

static void open_device(int device)
{
    char path[16];

    if (device == DEVICE_SMB) {

        open_smb();

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

static void run_paste(void)
{
    int i;
    int result;
    int done;
    int length;
    int move;

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
         * CANCELAR
         */
        case 7:

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

        if (action == CONFIRM_DELETE)
            delete_selection();
        else
            run_paste();

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
    if (pressed(PAD_R1)) {

        operation_menu_cursor = 0;

        operation_menu_open = 1;

        return;
    }
}


/* ============================================================
 * Desenho da tela de dispositivos
 * ============================================================ */

static void draw_device_screen(void)
{
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

        if (i ==
            operation_menu_cursor) {

            scr_printf(
                "> %s\n",
                operation_menu[i]
            );

        } else {

            scr_printf(
                "  %s\n",
                operation_menu[i]
            );
        }
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

