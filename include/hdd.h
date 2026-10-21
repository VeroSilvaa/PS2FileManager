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

/*
 * Carrega DEV9 + ata_bd (ATAD com BDM), confere os setores 0-1
 * (como o OPL) e so entao o driver APA + PFS. Disco MBR/GPT ou
 * desconhecido: -5, nada e gravado e o APA nunca e carregado.
 */
int hdd_init(void);

/*
 * Tabela vista nos setores 0-1 (FATFMT_TABLE_*), -1 se nao lida.
 */
int hdd_layout_table(void);

/*
 * Depois de mudar o disco (fatfmt): APA/vazio libera o driver APA;
 * MBR/GPT bloqueia (o HDD passa a ser aberto como ata0:).
 */
void hdd_set_layout(int table);

/*
 * 1 se DEV9 + ata_bd ja foram carregados (o disco "ata" existe).
 */
int hdd_stack_loaded(void);

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
/*
 * Espaco livre no disco (MB) e tamanho maximo de uma particao
 * (as maiores sao montadas com subparticoes).
 */
unsigned int hdd_free_mb(void);

/*
 * Cria uma particao PFS de size_mb (multiplo de 128), como o
 * hddMakeFilesystem da libhdd (wLaunchELF): principal do maior
 * tamanho possivel + subparticoes, depois formata em PFS.
 * Devolve os MB criados (> 0) ou erro (< 0).
 */
int hdd_create_partition(const char *name, unsigned int size_mb);

/*
 * Exclui a particao (principal + subparticoes). Desmonta antes
 * se estiver em pfs0:/pfs1:. 0 = ok.
 */
int hdd_remove_partition(const char *name);

#define HDD_FORMAT_STEPS 5

typedef void (*HddFormatStepFn)(int step, int total, const char *what);

int hdd_format_apa(HddFormatStepFn step);

const char *hdd_error(void);

#endif
