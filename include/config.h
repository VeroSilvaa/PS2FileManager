#ifndef PS2FILEMANAGER_CONFIG_H
#define PS2FILEMANAGER_CONFIG_H

/*
 * Configuracao de rede e SMB, gravada em texto ("chave=valor")
 * em mc0:/PS2FM/network.cfg (ou mc1: sem cartao no slot 1).
 * A senha fica em texto puro no arquivo.
 */

#define CONFIG_IP_TEXT  16
#define CONFIG_TEXT     64

typedef struct {
    int dhcp;

    char ip[CONFIG_IP_TEXT];
    char netmask[CONFIG_IP_TEXT];
    char gateway[CONFIG_IP_TEXT];
    char dns[CONFIG_IP_TEXT];

    char smb_server[CONFIG_IP_TEXT];
    char smb_share[CONFIG_TEXT];
    char smb_user[CONFIG_TEXT];
    char smb_password[CONFIG_TEXT];
} AppConfig;

AppConfig *config_get(void);

void config_defaults(AppConfig *config);

/*
 * 1 = lido do Memory Card; 0 = nao achou (fica o padrao).
 */
int config_load(void);

/*
 * 0 = gravado; < 0 = erro. Caminho usado em config_path().
 */
int config_save(void);

const char *config_path(void);

/*
 * "a.b.c.d" com 0..255 em cada parte.
 */
int config_valid_ip(const char *text);

#endif
