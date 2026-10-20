#include <stdio.h>
#include <string.h>

#include "filemanager.h"
#include "memorycard.h"

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
    fm->count = memorycard_list(
        fm->current_path,
        fm->entries,
        FM_MAX_FILES
    );

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

    /*
     * Não sair de mc0:/ ou mc1:/
     */
    if (strlen(fm->current_path) <= 5)
        return 0;

    slash = strrchr(
        fm->current_path,
        '/'
    );

    if (!slash)
        return 0;

    if (slash <= fm->current_path + 4) {
        fm->current_path[4] = '/';
        fm->current_path[5] = '\0';
    } else {
        *slash = '\0';
    }

    fm->cursor = 0;
    fm->scroll = 0;

    fm_refresh(fm);

    return 1;
}