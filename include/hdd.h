#ifndef PS2FILEMANAGER_HDD_H
#define PS2FILEMANAGER_HDD_H

/*
 * HDD interno (Etapa 5): APA (particoes) + PFS (arquivos).
 * Baseado no hdd.c do PS2DVDPlayer.
 *
 * - "hdd0:"          lista de particoes (somente leitura aqui)
 * - "pfs0:", "pfs1:" particoes montadas (continuam montadas
 *                    ao navegar em outros dispositivos)
 */

int hdd_init(void);

/*
 * Monta (ou reutiliza) a particao e devolve o ponto de
 * montagem ("pfs0:"/"pfs1:"). keep_point nunca e desmontado
 * (o clipboard aponta para ele). NULL em caso de erro.
 */
const char *hdd_mount_partition(const char *name, const char *keep_point);

/*
 * Grava no disco os dados pendentes de todas as particoes.
 */
void hdd_sync_all(void);

/*
 * Nome da particao montada em "pfsN:..." (NULL se nenhuma).
 */
const char *hdd_partition_of(const char *path);

int hdd_mounted_count(void);

/*
 * Desmonta pfs0:/pfs1: (antes de formatar).
 */
void hdd_unmount_all(void);

/*
 * Disco: tamanho em MB, se esta formatado em APA e quantas
 * particoes principais (sem subparticoes) existem.
 */
unsigned int hdd_total_mb(void);
int hdd_is_formatted(void);
int hdd_partition_count(void);

/*
 * Formata em APA (cria __mbr, __net, __system, __sysconf,
 * __common) e formata essas quatro em PFS, como a libhdd do
 * PS2SDK (usada pelo wLaunchELF). APAGA TUDO. 0 = ok.
 * step (opcional) e chamado antes de cada etapa (0..total-1).
 */
#define HDD_FORMAT_STEPS 5

typedef void (*HddFormatStepFn)(int step, int total, const char *what);

int hdd_format_apa(HddFormatStepFn step);

const char *hdd_error(void);

#endif
