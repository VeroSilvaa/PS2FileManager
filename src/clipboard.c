#include <stdio.h>
#include <string.h>

#include "clipboard.h"

void clipboard_init(Clipboard *cb)
{
    memset(cb, 0, sizeof(Clipboard));

    cb->mode = CLIPBOARD_NONE;
}

void clipboard_clear(Clipboard *cb)
{
    memset(cb, 0, sizeof(Clipboard));

    cb->mode = CLIPBOARD_NONE;
}

int clipboard_from_selection(
    Clipboard *cb,
    FileManager *fm,
    ClipboardMode mode)
{
    int i;

    clipboard_clear(cb);

    cb->mode = mode;

    for (i = 0; i < fm->count; i++) {

        if (!fm->entries[i].selected)
            continue;

        if (cb->count >= CLIPBOARD_MAX_ITEMS)
            break;

        strncpy(
            cb->paths[cb->count],
            fm->entries[i].path,
            FM_MAX_PATH - 1
        );

        cb->paths[cb->count][FM_MAX_PATH - 1] = '\0';

        cb->count++;
    }

    if (cb->count == 0) {
        cb->mode = CLIPBOARD_NONE;
        return 0;
    }

    return cb->count;
}