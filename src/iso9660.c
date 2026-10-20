#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include <fcntl.h>
#include <unistd.h>

#include "iso9660.h"

/*
 * Pasta raiz lida no maximo ate aqui (SYSTEM.CNF fica
 * sempre nos primeiros registros; jogos tem poucos arquivos
 * na raiz).
 */
#define ISO_ROOT_MAX_SECTORS 16

static unsigned char sector[ISO_SECTOR_SIZE];


static unsigned int iso_le32(const unsigned char *p)
{
    return (unsigned int)p[0] |
           ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) |
           ((unsigned int)p[3] << 24);
}


static void iso_reason(IsoGameInfo *info, const char *reason)
{
    snprintf(info->reason, sizeof(info->reason), "%s", reason);
}


/*
 * Tenta duas vezes: no SMB a primeira leitura logo apos abrir
 * ja falhou uma vez (a mesma ISO leu certo em seguida).
 */
static int iso_read_sector(int fd, unsigned int lba, unsigned char *buffer)
{
    int attempt;

    for (attempt = 0; attempt < 2; attempt++) {

        if (lseek(fd, (off_t)lba * ISO_SECTOR_SIZE, SEEK_SET) >= 0 &&
            read(fd, buffer, ISO_SECTOR_SIZE) == ISO_SECTOR_SIZE)
            return 1;

        printf("FM ISO: falha ao ler setor %u (tentativa %d)\n", lba, attempt + 1);
    }

    return 0;
}


int iso_has_extension(const char *name)
{
    int length;

    if (!name)
        return 0;

    length = strlen(name);

    return length > 4 &&
           strcasecmp(name + length - 4, ".iso") == 0;
}


/*
 * Procura SYSTEM.CNF na pasta raiz. Devolve o setor e o
 * tamanho do arquivo, ou 0 se nao achou.
 */
static int iso_find_system_cnf(
    int fd,
    unsigned int root_lba,
    unsigned int root_size,
    unsigned int *cnf_lba,
    unsigned int *cnf_size)
{
    unsigned int count;
    unsigned int s;
    int offset;

    count = (root_size + ISO_SECTOR_SIZE - 1) / ISO_SECTOR_SIZE;

    if (count > ISO_ROOT_MAX_SECTORS)
        count = ISO_ROOT_MAX_SECTORS;

    for (s = 0; s < count; s++) {

        if (!iso_read_sector(fd, root_lba + s, sector))
            return 0;

        offset = 0;

        while (offset < ISO_SECTOR_SIZE) {

            const unsigned char *record = sector + offset;
            int record_len = record[0];
            int name_len;

            /*
             * Registro 0 = resto do setor vazio.
             */
            if (record_len == 0 ||
                offset + record_len > ISO_SECTOR_SIZE)
                break;

            name_len = record[32];

            /*
             * "SYSTEM.CNF;1" (com versao) ou "SYSTEM.CNF".
             */
            if (33 + name_len <= record_len &&
                name_len >= 10 &&
                strncasecmp((const char *)record + 33, "SYSTEM.CNF", 10) == 0 &&
                (name_len == 10 || record[33 + 10] == ';')) {

                *cnf_lba = iso_le32(record + 2);
                *cnf_size = iso_le32(record + 10);

                return 1;
            }

            offset += record_len;
        }
    }

    return 0;
}


/*
 * Extrai o ID de "BOOT2 = cdrom0:\SLUS_123.45;1".
 * 1 = achou BOOT2; -1 = so BOOT (PS1); 0 = nenhum.
 */
static int iso_parse_boot2(char *text, char *game_id, int size)
{
    char *line;
    char *next;
    int has_ps1_boot = 0;

    for (line = text; line && *line; line = next) {

        char *value;
        char *start;
        char *end;

        next = strpbrk(line, "\r\n");

        if (next) {
            *next = '\0';
            next++;
        }

        while (*line == ' ' || *line == '\t')
            line++;

        if (strncasecmp(line, "BOOT", 4) != 0)
            continue;

        value = strchr(line, '=');

        if (!value)
            continue;

        if (line[4] != '2') {
            has_ps1_boot = 1;
            continue;
        }

        /*
         * Nome do executavel: depois da ultima '\' (ou ':'),
         * sem o ";1".
         */
        start = strrchr(value, '\\');

        if (!start)
            start = strrchr(value, ':');

        if (!start)
            start = value;

        start++;

        while (*start == ' ')
            start++;

        end = start;

        while (*end && *end != ';' && !isspace((unsigned char)*end))
            end++;

        if (end == start)
            return 0;

        if (end - start >= size)
            end = start + size - 1;

        memcpy(game_id, start, end - start);
        game_id[end - start] = '\0';

        return 1;
    }

    return has_ps1_boot ? -1 : 0;
}


int iso_probe_game(const char *path, IsoGameInfo *info)
{
    unsigned int root_lba;
    unsigned int root_size;
    unsigned int cnf_lba;
    unsigned int cnf_size;
    char text[ISO_SECTOR_SIZE + 1];
    int fd;
    int i;
    int boot;


    memset(info, 0, sizeof(IsoGameInfo));


    fd = open(path, O_RDONLY);

    if (fd < 0) {
        iso_reason(info, "nao abriu");
        return 0;
    }


    /*
     * Descritor primario: setor 16, tipo 1, "CD001".
     */
    if (!iso_read_sector(fd, 16, sector)) {
        close(fd);
        iso_reason(info, "erro de leitura");
        return 0;
    }

    if (sector[0] != 1 ||
        memcmp(sector + 1, "CD001", 5) != 0) {

        close(fd);
        iso_reason(info, "nao e ISO9660");
        return 0;
    }


    memcpy(info->volume_id, sector + 40, 32);
    info->volume_id[32] = '\0';

    for (i = 31; i >= 0 && info->volume_id[i] == ' '; i--)
        info->volume_id[i] = '\0';

    info->sectors = iso_le32(sector + 80);
    info->is_dvd = info->sectors > ISO_CD_MAX_SECTORS;


    /*
     * Registro da pasta raiz: offset 156 do descritor.
     */
    root_lba = iso_le32(sector + 156 + 2);
    root_size = iso_le32(sector + 156 + 10);


    if (!iso_find_system_cnf(fd, root_lba, root_size, &cnf_lba, &cnf_size)) {
        close(fd);
        iso_reason(info, "sem SYSTEM.CNF");
        return 0;
    }


    if (cnf_size > ISO_SECTOR_SIZE)
        cnf_size = ISO_SECTOR_SIZE;

    if (!iso_read_sector(fd, cnf_lba, sector)) {
        close(fd);
        iso_reason(info, "erro ao ler SYSTEM.CNF");
        return 0;
    }

    close(fd);


    memcpy(text, sector, cnf_size);
    text[cnf_size] = '\0';

    boot = iso_parse_boot2(text, info->game_id, sizeof(info->game_id));

    if (boot < 0) {
        iso_reason(info, "jogo de PS1");
        return 0;
    }

    if (boot == 0) {
        iso_reason(info, "SYSTEM.CNF sem BOOT2");
        return 0;
    }

    return 1;
}
