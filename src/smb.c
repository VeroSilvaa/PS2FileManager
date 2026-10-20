#include <stdio.h>
#include <string.h>

#include <tamtypes.h>
#include <kernel.h>
#include <loadfile.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <ps2smb.h>

#include "smb.h"

/*
 * Baseado no smb.c do PS2DVDPlayer (validado no PCSX2).
 *
 * Diferencas:
 * - IOMANX/FILEXIO ja sao carregados no inicio (main.c);
 *   aqui so entra o SMBMAN.
 * - Sem a listagem de ISOs: o PS2FileManager lista smb0:/
 *   com opendir()/readdir().
 */

#define SMB_DEVICE "smb0:"
#define SMB_PORT   445

extern unsigned char SMBMAN_irx[];
extern unsigned int size_SMBMAN_irx;

static SMBInfo smbInfo;
static int smbmanLoaded = 0;


/* ============================================================
 * ERRO
 * ============================================================ */

static void smb_set_error(
    const char *message,
    int code
)
{
    if (code != 0)
    {
        snprintf(
            smbInfo.error,
            sizeof(smbInfo.error),
            "%s (%d)",
            message,
            code
        );
    }
    else
    {
        snprintf(
            smbInfo.error,
            sizeof(smbInfo.error),
            "%s",
            message
        );
    }
}


/* ============================================================
 * INIT
 * ============================================================ */

int smb_init(void)
{
    int module_id;
    int module_result = 0;


    if (smbInfo.initialized)
        return 0;


    memset(
        &smbInfo,
        0,
        sizeof(smbInfo)
    );


    /*
     * SMBMAN importa a biblioteca IOP "ps2ip", carregada
     * por network_init() (PS2IP-NM). O IRX continua
     * residente: nunca carregar uma segunda instancia.
     */
    if (!smbmanLoaded)
    {
        printf("SMB: loading SMBMAN\n");

        module_id =
            SifExecModuleBuffer(
                SMBMAN_irx,
                size_SMBMAN_irx,
                0,
                NULL,
                &module_result
            );

        printf(
            "SMB: SMBMAN id=%d start_result=%d\n",
            module_id,
            module_result
        );

        if (module_id < 0 ||
            module_result < 0)
        {
            smb_set_error(
                "SMBMAN load failed",
                module_id < 0 ? module_id : module_result
            );

            return -1;
        }

        smbmanLoaded = 1;
    }


    smbInfo.initialized = 1;
    smbInfo.error[0] = '\0';

    return 0;
}


/* ============================================================
 * CONNECT
 * ============================================================ */

int smb_connect(
    const char *server,
    const char *share,
    const char *user,
    const char *password
)
{
    smbLogOn_in_t logon;
    smbEcho_in_t echo;
    smbOpenShare_in_t openShare;

    int result;


    if (!smbInfo.initialized)
    {
        smb_set_error(
            "SMB not initialized",
            0
        );

        return -1;
    }


    if (!server ||
        !share ||
        !user ||
        !password)
    {
        smb_set_error(
            "Invalid SMB parameters",
            0
        );

        return -2;
    }


    if (smbInfo.connected)
        smb_disconnect();


    memset(&logon, 0, sizeof(logon));
    memset(&echo, 0, sizeof(echo));
    memset(&openShare, 0, sizeof(openShare));


    snprintf(
        logon.serverIP,
        sizeof(logon.serverIP),
        "%s",
        server
    );

    logon.serverPort =
        SMB_PORT;

    snprintf(
        logon.User,
        sizeof(logon.User),
        "%s",
        user
    );


    if (password[0] != '\0')
    {
        snprintf(
            logon.Password,
            sizeof(logon.Password),
            "%s",
            password
        );

        logon.PasswordType =
            PLAINTEXT_PASSWORD;

        snprintf(
            openShare.Password,
            sizeof(openShare.Password),
            "%s",
            password
        );

        openShare.PasswordType =
            PLAINTEXT_PASSWORD;
    }
    else
    {
        logon.PasswordType =
            NO_PASSWORD;

        openShare.PasswordType =
            NO_PASSWORD;
    }


    printf(
        "SMB: [1/3] LOGON -> %s:%d user=%s\n",
        server,
        SMB_PORT,
        user
    );

    result =
        fileXioDevctl(
            SMB_DEVICE,
            SMB_DEVCTL_LOGON,
            &logon,
            sizeof(logon),
            NULL,
            0
        );

    printf("SMB: LOGON returned %d\n", result);

    if (result < 0)
    {
        if (result == -SMB_DEVCTL_LOGON_ERR_CONN)
            smb_set_error("LOGON: sem conexao com o servidor", result);
        else if (result == -SMB_DEVCTL_LOGON_ERR_PROT)
            smb_set_error("LOGON: erro de protocolo", result);
        else if (result == -SMB_DEVCTL_LOGON_ERR_LOGON)
            smb_set_error("LOGON: autenticacao recusada", result);
        else
            smb_set_error("LOGON falhou", result);

        printf("SMB: %s\n", smbInfo.error);

        return -3;
    }

    smbInfo.logged_on = 1;


    snprintf(
        echo.echo,
        sizeof(echo.echo),
        "PS2FileManager ECHO"
    );

    echo.len =
        strlen(echo.echo);

    printf("SMB: [2/3] ECHO\n");

    result =
        fileXioDevctl(
            SMB_DEVICE,
            SMB_DEVCTL_ECHO,
            &echo,
            sizeof(echo),
            NULL,
            0
        );

    printf("SMB: ECHO returned %d\n", result);

    if (result < 0)
    {
        smb_set_error("ECHO falhou", result);

        printf("SMB: %s\n", smbInfo.error);

        smb_disconnect();

        return -4;
    }


    snprintf(
        openShare.ShareName,
        sizeof(openShare.ShareName),
        "%s",
        share
    );

    printf("SMB: [3/3] OPENSHARE %s\n", share);

    result =
        fileXioDevctl(
            SMB_DEVICE,
            SMB_DEVCTL_OPENSHARE,
            &openShare,
            sizeof(openShare),
            NULL,
            0
        );

    printf("SMB: OPENSHARE returned %d\n", result);

    if (result < 0)
    {
        smb_set_error("OPENSHARE falhou", result);

        printf("SMB: %s\n", smbInfo.error);

        smb_disconnect();

        return -5;
    }


    smbInfo.share_open = 1;
    smbInfo.connected = 1;

    snprintf(smbInfo.server, sizeof(smbInfo.server), "%s", server);
    snprintf(smbInfo.share, sizeof(smbInfo.share), "%s", share);
    snprintf(smbInfo.user, sizeof(smbInfo.user), "%s", user);

    smbInfo.error[0] = '\0';

    printf("SMB: connection ready at %s\n", SMB_DEVICE);

    return 0;
}


/* ============================================================
 * DISCONNECT
 * ============================================================ */

void smb_disconnect(void)
{
    int result;


    if (!smbInfo.initialized)
        return;


    if (smbInfo.share_open)
    {
        result =
            fileXioDevctl(
                SMB_DEVICE,
                SMB_DEVCTL_CLOSESHARE,
                NULL,
                0,
                NULL,
                0
            );

        printf("SMB: CLOSESHARE returned %d\n", result);

        smbInfo.share_open = 0;
    }


    if (smbInfo.logged_on)
    {
        result =
            fileXioDevctl(
                SMB_DEVICE,
                SMB_DEVCTL_LOGOFF,
                NULL,
                0,
                NULL,
                0
            );

        printf("SMB: LOGOFF returned %d\n", result);

        smbInfo.logged_on = 0;
    }


    smbInfo.connected = 0;
}


/* ============================================================
 * INFO
 * ============================================================ */

const SMBInfo *smb_get_info(void)
{
    return &smbInfo;
}
