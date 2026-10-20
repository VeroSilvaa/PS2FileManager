#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include "filemanager.h"

#define CLIPBOARD_MAX_ITEMS 256

typedef enum {
    CLIPBOARD_NONE = 0,
    CLIPBOARD_COPY,
    CLIPBOARD_MOVE
} ClipboardMode;

typedef struct {
    ClipboardMode mode;

    char paths[CLIPBOARD_MAX_ITEMS][FM_MAX_PATH];

    int count;
} Clipboard;

void clipboard_init(Clipboard *cb);

void clipboard_clear(Clipboard *cb);

int clipboard_from_selection(
    Clipboard *cb,
    FileManager *fm,
    ClipboardMode mode
);

#endif