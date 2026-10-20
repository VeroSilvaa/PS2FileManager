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

const char *hdd_error(void);

#endif
