#ifndef PS2FILEMANAGER_ELFRUN_H
#define PS2FILEMANAGER_ELFRUN_H

/*
 * Executar ELF (como o wLaunchELF): libelf-loader do PS2SDK (AFL).
 * O mini-loader carrega o ELF pelo IOP atual (todos os nossos
 * drivers: MC, USB, HDD PFS/exFAT, SMB), reinicia o IOP e salta.
 */

/*
 * 1 se o nome termina em .elf.
 */
int elf_is_elf(const char *name);

/*
 * 1 se o arquivo comeca com a assinatura ELF (0x7F "ELF").
 */
int elf_check(const char *path);

/*
 * Executa. So volta em caso de erro (< 0; texto em elf_error()).
 */
int elf_run(const char *path);

const char *elf_error(void);

#endif
