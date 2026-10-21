#!/bin/sh
# Gera assets/ (fundo e atlas da fonte). Rodar no conteiner:
#   docker compose run --rm ps2filemanager sh tools/make_assets.sh
set -e

apk add -q --no-cache font-noto py3-pillow >/dev/null 2>&1

# Fonte (OFL) copiada para o repositorio: o atlas e derivado dela.
mkdir -p third_party/fonts
cp /usr/share/fonts/noto/NotoSansMono-Regular.ttf third_party/fonts/
cp /usr/share/fonts/noto/NotoSans-Regular.ttf third_party/fonts/

python3 tools/make_assets.py
