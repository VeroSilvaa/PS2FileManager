#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "config.h"

#define CONFIG_DIR   "PS2FM"
#define CONFIG_FILE  "network.cfg"

static AppConfig app_config;
static char app_config_path[48];


AppConfig *config_get(void)
{
    return &app_config;
}


/*
 * Padrao: os dados que estavam fixos no codigo ate a tela de
 * configuracao existir.
 */
void config_defaults(AppConfig *config)
{
    memset(config, 0, sizeof(AppConfig));

    config->dhcp = 1;

    strcpy(config->ip, "192.168.1.10");
    strcpy(config->netmask, "255.255.255.0");
    strcpy(config->gateway, "192.168.1.1");
    strcpy(config->dns, "192.168.1.1");

    strcpy(config->smb_server, "192.168.1.9");
    strcpy(config->smb_share, "samba");
    strcpy(config->smb_user, "guest");
}


int config_valid_ip(const char *text)
{
    unsigned int part[4];
    char extra;

    if (!text)
        return 0;

    if (sscanf(text, "%u.%u.%u.%u%c",
               &part[0], &part[1], &part[2], &part[3], &extra) != 4)
        return 0;

    return part[0] <= 255 && part[1] <= 255 &&
           part[2] <= 255 && part[3] <= 255;
}


static void config_copy(char *dst, int size, const char *value)
{
    snprintf(dst, (size_t)size, "%s", value);
}


static void config_set(AppConfig *config, const char *key, const char *value)
{
    if (strcmp(key, "dhcp") == 0)
        config->dhcp = atoi(value) != 0;
    else if (strcmp(key, "ip") == 0)
        config_copy(config->ip, sizeof(config->ip), value);
    else if (strcmp(key, "netmask") == 0)
        config_copy(config->netmask, sizeof(config->netmask), value);
    else if (strcmp(key, "gateway") == 0)
        config_copy(config->gateway, sizeof(config->gateway), value);
    else if (strcmp(key, "dns") == 0)
        config_copy(config->dns, sizeof(config->dns), value);
    else if (strcmp(key, "smb_server") == 0)
        config_copy(config->smb_server, sizeof(config->smb_server), value);
    else if (strcmp(key, "smb_share") == 0)
        config_copy(config->smb_share, sizeof(config->smb_share), value);
    else if (strcmp(key, "smb_user") == 0)
        config_copy(config->smb_user, sizeof(config->smb_user), value);
    else if (strcmp(key, "smb_password") == 0)
        config_copy(config->smb_password, sizeof(config->smb_password), value);
}


static int config_read_file(const char *path, AppConfig *config)
{
    char line[160];
    char *value;
    char *end;
    FILE *file;

    file = fopen(path, "r");

    if (!file)
        return 0;

    while (fgets(line, sizeof(line), file)) {

        end = line + strlen(line);

        while (end > line && (end[-1] == '\n' || end[-1] == '\r'))
            *--end = '\0';

        if (line[0] == '#' || !(value = strchr(line, '=')))
            continue;

        *value++ = '\0';

        config_set(config, line, value);
    }

    fclose(file);

    return 1;
}


int config_load(void)
{
    static const char *slots[] = { "mc0:", "mc1:" };
    char path[48];
    int i;

    config_defaults(&app_config);

    for (i = 0; i < 2; i++) {

        snprintf(path, sizeof(path), "%s/%s/%s", slots[i], CONFIG_DIR, CONFIG_FILE);

        if (config_read_file(path, &app_config)) {

            snprintf(app_config_path, sizeof(app_config_path), "%s", path);

            printf("FM CONFIG: lido %s\n", path);

            return 1;
        }
    }

    app_config_path[0] = '\0';

    printf("FM CONFIG: sem arquivo, usando padrao\n");

    return 0;
}


static int config_write_file(const char *slot)
{
    char dir[32];
    char path[48];
    FILE *file;
    int ok;

    snprintf(dir, sizeof(dir), "%s/%s", slot, CONFIG_DIR);
    snprintf(path, sizeof(path), "%s/%s", dir, CONFIG_FILE);

    /*
     * Pasta pode ja existir (mkdir falha: tudo bem). O arquivo
     * e apagado antes: no MC o O_TRUNC nao e garantido.
     */
    mkdir(dir, 0777);
    remove(path);

    file = fopen(path, "w");

    if (!file) {
        printf("FM CONFIG: fopen %s falhou (errno %d)\n", path, errno);
        return -1;
    }

    fprintf(file, "# PS2FileManager - rede e SMB\n");
    fprintf(file, "dhcp=%d\n", app_config.dhcp);
    fprintf(file, "ip=%s\n", app_config.ip);
    fprintf(file, "netmask=%s\n", app_config.netmask);
    fprintf(file, "gateway=%s\n", app_config.gateway);
    fprintf(file, "dns=%s\n", app_config.dns);
    fprintf(file, "smb_server=%s\n", app_config.smb_server);
    fprintf(file, "smb_share=%s\n", app_config.smb_share);
    fprintf(file, "smb_user=%s\n", app_config.smb_user);
    ok = fprintf(file, "smb_password=%s\n", app_config.smb_password) > 0;

    if (fclose(file) != 0)
        ok = 0;

    if (!ok) {
        printf("FM CONFIG: erro ao gravar %s\n", path);
        return -1;
    }

    snprintf(app_config_path, sizeof(app_config_path), "%s", path);

    printf("FM CONFIG: gravado %s\n", path);

    return 0;
}


int config_save(void)
{
    /*
     * Mesmo cartao de onde foi lido; senao mc0:, depois mc1:.
     */
    if (strncmp(app_config_path, "mc1:", 4) == 0) {

        if (config_write_file("mc1:") == 0)
            return 0;

        return config_write_file("mc0:");
    }

    if (config_write_file("mc0:") == 0)
        return 0;

    return config_write_file("mc1:");
}


const char *config_path(void)
{
    return app_config_path;
}
