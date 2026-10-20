#!/bin/sh
# Confere imagens de pendrive de teste (tabela + sistema de arquivos)
# depois de formatar pelo PS2FileManager.
#   docker compose run --rm ps2filemanager sh usb/checkimg.sh [usb/usb1.img ...]

apk add -q --no-cache sgdisk sfdisk dosfstools exfatprogs mtools blkid >/dev/null 2>&1

[ $# -eq 0 ] && set -- usb/usb1.img usb/usb2.img

for img in "$@"; do

    echo
    echo "===== $img"

    table=$(sfdisk -d "$img" 2>/dev/null | sed -n 's/^label: *//p')
    echo "tabela: ${table:-nenhuma}"

    if [ "$table" = "gpt" ]; then
        sgdisk -v "$img" | grep -E 'No problems|Problem|Caution|Warning'
    fi

    line=$(sfdisk -d "$img" 2>/dev/null | grep 'start=' | head -1)

    if [ -z "$line" ]; then
        echo "sem particao"
        continue
    fi

    echo "$line"

    start=$(echo "$line" | sed 's/.*start= *\([0-9]*\).*/\1/')
    size=$(echo "$line" | sed 's/.*size= *\([0-9]*\).*/\1/')

    part=/tmp/$(basename "$img").p1
    dd if="$img" of="$part" bs=512 skip="$start" count="$size" 2>/dev/null

    if blkid "$part" | grep -q exfat; then
        echo "-- exFAT"
        fsck.exfat -n "$part"
    else
        echo "-- FAT"
        fsck.fat -n -v "$part" | grep -E 'FATs|hidden|clusters|files|differ|error'
    fi

    echo "-- arquivos"
    mdir -i "$part" -/ ::/ 2>/dev/null | grep -vE '^$' | head -20
done
