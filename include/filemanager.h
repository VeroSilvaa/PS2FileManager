#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#define FM_MAX_FILES       256
#define FM_MAX_NAME        256
#define FM_MAX_PATH        1024

typedef struct {
    char name[FM_MAX_NAME];
    char path[FM_MAX_PATH];

    int is_dir;
    int selected;

    unsigned int size;
} FMEntry;

typedef struct {
    char current_path[FM_MAX_PATH];

    FMEntry entries[FM_MAX_FILES];

    int count;
    int cursor;
    int scroll;
} FileManager;

void fm_init(FileManager *fm, const char *path);

int fm_refresh(FileManager *fm);

void fm_cursor_up(FileManager *fm);
void fm_cursor_down(FileManager *fm);

void fm_toggle_selected(FileManager *fm);
void fm_select_all(FileManager *fm);
void fm_clear_selection(FileManager *fm);

int fm_selected_count(FileManager *fm);

int fm_enter(FileManager *fm);
int fm_back(FileManager *fm);

#endif