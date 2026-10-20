#ifndef PS2FILEMANAGER_SMB_H
#define PS2FILEMANAGER_SMB_H

/*
 * Cliente SMB (SMBMAN do PS2SDK, protocolo SMB1).
 * Baseado no smb.c do PS2DVDPlayer.
 *
 * Depois de conectado, o compartilhamento fica acessivel
 * em "smb0:/" pelas funcoes POSIX (open/opendir/...).
 */

typedef struct
{
    int initialized;
    int logged_on;
    int share_open;
    int connected;

    char server[64];
    char share[64];
    char user[64];

    char error[128];
} SMBInfo;

int smb_init(void);

int smb_connect(
    const char *server,
    const char *share,
    const char *user,
    const char *password
);

void smb_disconnect(void);

const SMBInfo *smb_get_info(void);

#endif
