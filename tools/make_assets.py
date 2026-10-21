#!/usr/bin/env python3
"""
Gera os recursos graficos da interface (rodar no conteiner):

    docker compose run --rm ps2filemanager sh tools/make_assets.sh

- assets/bg_640x448.png      fundo: a arte original (16:9) cortada para
                             4:3 pelo lado esquerdo (os simbolos ficam a
                             direita) e reduzida para 640x448
- assets/font_mono_8x16.png  atlas da fonte monoespacada: bytes 32..255
                             (Windows-1252: acentos, travessao, aspas),
                             16 por linha, celulas de 8x16, tons de cinza
- assets/preview_font.png    previa ampliada (nao vai para o ELF)

Fonte: Noto Sans Mono (licenca OFL, third_party/fonts).
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")

BACKGROUND_SRC = os.path.join(ROOT, "Fundo Futurista Azul com Símbolos PlayStation.png")
FONT_MONO = os.path.join(ROOT, "third_party", "fonts", "NotoSansMono-Regular.ttf")

SCREEN_W, SCREEN_H = 640, 448

CELL_W, CELL_H = 8, 16
FONT_SIZE = 13
BASELINE = 12          # y da linha de base dentro da celula
FIRST_CHAR, LAST_CHAR = 32, 255
PER_ROW = 16


def make_background():
    src = Image.open(BACKGROUND_SRC).convert("RGB")

    # A tela do PS2 (640x448) e exibida em 4:3: corta a largura para
    # 4:3 tirando do lado esquerdo (so ondas); os simbolos ficam.
    crop_w = round(src.height * 4 / 3)
    left = max(0, src.width - crop_w)
    cropped = src.crop((left, 0, src.width, src.height))

    out = cropped.resize((SCREEN_W, SCREEN_H), Image.LANCZOS)
    out.save(os.path.join(ASSETS, "bg_640x448.png"), optimize=True)

    print("fundo: %dx%d -> corte %dx%d -> %dx%d" %
          (src.width, src.height, cropped.width, cropped.height, SCREEN_W, SCREEN_H))


def char_for_byte(code):
    try:
        return bytes([code]).decode("cp1252")
    except UnicodeDecodeError:
        return None      # 0x81, 0x8D, 0x8F, 0x90, 0x9D: sem caractere


def make_font():
    font = ImageFont.truetype(FONT_MONO, FONT_SIZE)
    count = LAST_CHAR - FIRST_CHAR + 1
    rows = (count + PER_ROW - 1) // PER_ROW

    atlas = Image.new("L", (PER_ROW * CELL_W, rows * CELL_H), 0)
    draw = ImageDraw.Draw(atlas)

    for code in range(FIRST_CHAR, LAST_CHAR + 1):
        ch = char_for_byte(code)
        if ch is None or not ch.isprintable() or ch == "\x7f":
            continue

        i = code - FIRST_CHAR
        x = (i % PER_ROW) * CELL_W
        y = (i // PER_ROW) * CELL_H

        # Centraliza na celula pela largura do avanco.
        advance = font.getlength(ch)
        dx = (CELL_W - advance) / 2

        draw.text((x + dx, y + BASELINE), ch, fill=255, font=font, anchor="ls")

    atlas.save(os.path.join(ASSETS, "font_mono_8x16.png"), optimize=True)

    preview = atlas.resize((atlas.width * 4, atlas.height * 4), Image.NEAREST)
    preview.save(os.path.join(ASSETS, "preview_font.png"))

    print("fonte: %d caracteres, atlas %dx%d" % (count, atlas.width, atlas.height))


def main():
    os.makedirs(ASSETS, exist_ok=True)
    make_background()
    make_font()
    return 0


if __name__ == "__main__":
    sys.exit(main())
