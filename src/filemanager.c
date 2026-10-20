#include <stdio.h>
#include <string.h>

#include <dirent.h>
#include <sys/stat.h>

#include "filemanager.h"
#include "memorycard.h"

/*
 * Tamanho da raiz do dispositivo ("mc0:/" = 5, "smb0:/" = 6).
 */
static int fm_root_length(const char *path)
{
    const char *colon;

    colon = strchr(path, ':');

    if (!colon)
        return 0;

    return (int)(colon - path) + 2;
}

/*
 * Listagem generica (smb0:, e depois mass:, pfs:...):
 * opendir()/readdir() do newlib sobre o fileXio.
 *
 * O Memory Card continua usando mcGetDir() (memorycard_list),
 * a enumeracao ja validada.
 */
static int fm_list_posix(const char *path, FMEntry *entries, int max_entries)
{
    DIR *dir;
    struct dirent *de;
    struct stat st;
    int count = 0;
    int path_len;
    int written;

    dir = opendir(path);

    if (!dir) {
        printf("FM LIST: opendir(%s) falhou\n", path);
        return -1;
    }

    path_len = strlen(path);

    while (count < max_entries && (de = readdir(dir)) != NULL) {

        FMEntry *entry = &entries[count];

        /*
         * "." e "..": selecionados por engano fariam a
         * copia/exclusao agir sobre a pasta PAI.
         */
        if (de->d_name[0] == '\0' ||
            strcmp(de->d_name, ".") == 0 ||
            strcmp(de->d_name, "..") == 0)
            continue;

        memset(entry, 0, sizeof(FMEntry));

        strncpy(entry->name, de->d_name, FM_MAX_NAME - 1);

        if (path_len > 0 && path[path_len - 1] == '/')
            written = snprintf(entry->path, FM_MAX_PATH, "%s%s", path, de->d_name);
        else
            written = snprintf(entry->path, FM_MAX_PATH, "%s/%s", path, de->d_name);

        if (written < 0 || written >= FM_MAX_PATH)
            continue;

        /*
         * O item acabou de ser listado, entao existe:
         * aqui o stat() e confiavel para tipo e tamanho.
         */
        memset(&st, 0, sizeof(st));

        if (stat(entry->path, &st) == 0) {
            entry->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
            entry->size = (unsigned int)st.st_size;
        }

        count++;
    }

    closedir(dir);

    printf("FM LIST: %s -> %d itens\n", path, count);

    return count;
}

void fm_init(FileManager *fm, const char *path)
{
    memset(fm, 0, sizeof(FileManager));

    strncpy(
        fm->current_path,
        path,
        FM_MAX_PATH - 1
    );

    fm_refresh(fm);
}

int fm_refresh(FileManager *fm)
{
    if (strncmp(fm->current_path, "mc", 2) == 0) {

        fm->count = memorycard_list(
            fm->current_path,
            fm->entries,
            FM_MAX_FILES
        );

    } else {

        fm->count = fm_list_posix(
            fm->current_path,
            fm->entries,
            FM_MAX_FILES
        );
    }

    if (fm->count < 0)
        fm->count = 0;

    if (fm->cursor >= fm->count)
        fm->cursor = fm->count - 1;

    if (fm->cursor < 0)
        fm->cursor = 0;

    fm->scroll = 0;

    return fm->count;
}

void fm_cursor_up(FileManager *fm)
{
    if (fm->cursor > 0)
        fm->cursor--;

    if (fm->cursor < fm->scroll)
        fm->scroll = fm->cursor;
}

void fm_cursor_down(FileManager *fm)
{
    if (fm->cursor < fm->count - 1)
        fm->cursor++;

    if (fm->cursor >= fm->scroll + 18)
        fm->scroll++;
}

void fm_toggle_selected(FileManager *fm)
{
    if (fm->count == 0)
        return;

    fm->entries[fm->cursor].selected =
        !fm->entries[fm->cursor].selected;
}

void fm_select_all(FileManager *fm)
{
    int i;

    for (i = 0; i < fm->count; i++)
        fm->entries[i].selected = 1;
}

void fm_clear_selection(FileManager *fm)
{
    int i;

    for (i = 0; i < fm->count; i++)
        fm->entries[i].selected = 0;
}

int fm_selected_count(FileManager *fm)
{
    int i;
    int count = 0;

    for (i = 0; i < fm->count; i++) {
        if (fm->entries[i].selected)
            count++;
    }

    return count;
}

int fm_enter(FileManager *fm)
{
    if (fm->count == 0)
        return 0;

    FMEntry *entry =
        &fm->entries[fm->cursor];

    if (!entry->is_dir)
        return 0;

    strncpy(
        fm->current_path,
        entry->path,
        FM_MAX_PATH - 1
    );

    fm->cursor = 0;
    fm->scroll = 0;

    fm_refresh(fm);

    return 1;
}

int fm_back(FileManager *fm)
{
    char *slash;
    int root_len;

    /*
     * Nao sair da raiz do dispositivo (mc0:/, smb0:/ ...).
     */
    root_len = fm_root_length(fm->current_path);

    if (root_len <= 0 ||
        (int)strlen(fm->current_path) <= root_len)
        return 0;

    slash = strrchr(
        fm->current_path,
        '/'
    );

    if (!slash)
        return 0;

    if (slash < fm->current_path + root_len) {
        fm->current_path[root_len - 1] = '/';
        fm->current_path[root_len] = '\0';
    } else {
        *slash = '\0';
    }

    fm->cursor = 0;
    fm->scroll = 0;

    fm_refresh(fm);

    return 1;
}
