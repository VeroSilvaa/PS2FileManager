#!/bin/sh
# Cria duas imagens de pendrive (MBR + FAT32) para o USB Mass Storage do PCSX2.
# Uso: docker compose run --rm ps2filemanager sh usb/mkimg.sh
set -e
apk add --no-cache dosfstools mtools sfdisk >/dev/null
cd "$(dirname "$0")"
for n in 1 2; do
  img=usb$n.img
  rm -f $img
  truncate -s 256M $img
  echo 'start=2048, type=c' | sfdisk -q --label dos $img
  mkfs.fat -F 32 -n PENDRIVE$n --offset 2048 $img >/dev/null
  echo "Arquivo de teste do pendrive $n" > /tmp/LEIAME.TXT
  head -c 3000000 /dev/urandom > /tmp/GRANDE.BIN
  mmd -i $img@@1M ::/PASTA$n ::/PASTA$n/SUB
  mcopy -i $img@@1M /tmp/LEIAME.TXT ::/
  mcopy -i $img@@1M /tmp/GRANDE.BIN ::/PASTA$n/
  mcopy -i $img@@1M /tmp/LEIAME.TXT ::/PASTA$n/SUB/
  echo "== $img"
  mdir -/ -i $img@@1M ::/ | grep -vE '^$'
done
