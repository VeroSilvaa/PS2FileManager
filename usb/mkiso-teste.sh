#!/bin/sh
# Cria ISOs de teste (NAO sao jogos de PS2) e copia para os pendrives virtuais.
# Uso: docker compose run --rm ps2filemanager sh usb/mkiso-teste.sh
set -e
apk add --no-cache xorriso mtools >/dev/null
cd "$(dirname "$0")"
t=/tmp/iso
rm -rf $t && mkdir -p $t/naojogo $t/ps1
echo "ISO de teste sem SYSTEM.CNF" > $t/naojogo/LEIAME.TXT
printf 'BOOT = cdrom:\\SLUS_000.01;1\r\nTCB = 2\r\n' > $t/ps1/SYSTEM.CNF
echo "executavel falso" > $t/ps1/SLUS_000.01
xorriso -as mkisofs -quiet -V NAOJOGO -o $t/NAOJOGO.iso $t/naojogo
xorriso -as mkisofs -quiet -V JOGOPS1 -o $t/JOGOPS1.iso $t/ps1
for img in usb1.img usb2.img; do
  mmd -D s -i $img@@1M ::/ISOS 2>/dev/null || true
  mcopy -o -i $img@@1M $t/NAOJOGO.iso $t/JOGOPS1.iso ::/ISOS/
  echo "== $img"; mdir -i $img@@1M ::/ISOS | grep -i iso
done
