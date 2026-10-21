#include "ui.h"


void gui_panel(float x, float y, float w, float h, const char *title)
{
    gfx_rect(x, y, w, h, UI_PANEL);
    gfx_rect(x + 2, y + 2, w - 4, 24, UI_TITLE_BAR);
    gfx_border(x, y, w, h, 2, UI_BORDER);

    if (title)
        gfx_text_fit(x + 10, y + 4, w - 20, title, UI_TEXT);
}


void gui_selection(float x, float y, float w, float h, int focused)
{
    if (focused)
        gfx_rect_vgradient(x, y, w, h, UI_SEL_TOP, UI_SEL_BOTTOM);
    else
        gfx_rect(x, y, w, h, UI_SEL_DIM);
}


float gui_window(float x, float y, float w, float h, const char *title)
{
    gfx_rect(0, 0, GFX_W, GFX_H, UI_DIM);

    gfx_rect(x, y, w, h, UI_WINDOW);
    gfx_rect_vgradient(x + 2, y + 2, w - 4, 26, UI_SEL_TOP, UI_TITLE_BAR);
    gfx_border(x, y, w, h, 2, UI_BORDER);

    gfx_text_fit(x + 12, y + 5, w - 24, title, UI_TEXT);

    return y + 38;
}
