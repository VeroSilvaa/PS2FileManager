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
FONT_SANS = os.path.join(ROOT, "third_party", "fonts", "NotoSans-Regular.ttf")

# Fonte proporcional da interface (telas convertidas)
SANS_SIZE = 14
SANS_CELL_W, SANS_CELL_H = 16, 20
SANS_BASELINE = 15

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


def make_sans():
    """
    assets/font_sans_14.png + assets/font_sans_14.h (largura de cada
    caractere, em pixels, para o texto proporcional).
    """
    font = ImageFont.truetype(FONT_SANS, SANS_SIZE)
    count = LAST_CHAR - FIRST_CHAR + 1
    rows = (count + PER_ROW - 1) // PER_ROW

    atlas = Image.new("L", (PER_ROW * SANS_CELL_W, rows * SANS_CELL_H), 0)
    draw = ImageDraw.Draw(atlas)
    widths = []

    for code in range(FIRST_CHAR, LAST_CHAR + 1):
        ch = char_for_byte(code)
        width = 0

        if ch is not None and ch.isprintable() and ch != "\x7f":

            i = code - FIRST_CHAR
            x = (i % PER_ROW) * SANS_CELL_W
            y = (i // PER_ROW) * SANS_CELL_H

            draw.text((x + 1, y + SANS_BASELINE), ch, fill=255, font=font, anchor="ls")
            width = min(SANS_CELL_W - 1, round(font.getlength(ch)))

        widths.append(width)

    atlas.save(os.path.join(ASSETS, "font_sans_14.png"), optimize=True)

    with open(os.path.join(ASSETS, "font_sans_14.h"), "w") as out:
        out.write("/* Gerado por tools/make_assets.py: Noto Sans %d px, bytes %d..%d (Windows-1252). */\n"
                  % (SANS_SIZE, FIRST_CHAR, LAST_CHAR))
        out.write("#define FONT_SANS_CELL_W %d\n#define FONT_SANS_CELL_H %d\n\n" % (SANS_CELL_W, SANS_CELL_H))
        out.write("static const unsigned char font_sans_width[%d] = {\n" % count)
        for i in range(0, count, 16):
            out.write("    " + ", ".join("%2d" % w for w in widths[i:i + 16]) + ",\n")
        out.write("};\n")

    preview = atlas.resize((atlas.width * 2, atlas.height * 2), Image.NEAREST)
    preview.save(os.path.join(ASSETS, "preview_sans.png"))

    print("fonte sans: atlas %dx%d" % (atlas.width, atlas.height))


FONT_LOGO = os.path.join(ROOT, "third_party", "fonts", "Orbitron.ttf")


def make_logo():
    """
    assets/logo.png: "PS2FileManager" em Orbitron (OFL) com gradiente
    azul e brilho, fundo transparente. Desenhado em 4x e reduzido
    (suavizacao). Nao usa o logotipo oficial "PS2".
    """
    from PIL import ImageFilter

    scale = 4
    size = 22 * scale
    text = "PS2FileManager"

    font = ImageFont.truetype(FONT_LOGO, size)
    try:
        font.set_variation_by_name("Bold")
    except Exception:
        pass

    left, top, right, bottom = font.getbbox(text)
    pad = 6 * scale
    w = right - left + 2 * pad
    h = bottom - top + 2 * pad

    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).text((pad - left, pad - top), text, fill=255, font=font)

    # Gradiente vertical: azul claro -> azul do tema.
    grad = Image.new("RGB", (w, h))
    gd = ImageDraw.Draw(grad)
    top_c, bottom_c = (0xE8, 0xF2, 0xFF), (0x2F, 0x7B, 0xFF)
    for y in range(h):
        t = y / max(1, h - 1)
        gd.line([(0, y), (w, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(top_c, bottom_c)))

    # Brilho: o texto borrado, azul, por baixo.
    glow_alpha = mask.filter(ImageFilter.GaussianBlur(4 * scale)).point(lambda v: min(255, v * 2) * 0.55)
    glow = Image.new("RGBA", (w, h), (0x2F, 0x7B, 0xFF, 0))
    glow.putalpha(glow_alpha.convert("L"))

    letters = grad.convert("RGBA")
    letters.putalpha(mask)

    logo = Image.alpha_composite(glow, letters)
    logo = logo.resize((w // scale, h // scale), Image.LANCZOS)
    logo.save(os.path.join(ASSETS, "logo.png"), optimize=True)

    print("logo: %dx%d" % logo.size)


def main():
    os.makedirs(ASSETS, exist_ok=True)
    make_background()
    make_font()
    make_sans()
    make_logo()
    return 0


if __name__ == "__main__":
    sys.exit(main())
