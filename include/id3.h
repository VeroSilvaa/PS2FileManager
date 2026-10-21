#ifndef PS2FILEMANAGER_ID3_H
#define PS2FILEMANAGER_ID3_H

/*
 * Tags ID3 de MP3: ID3v2.2/2.3/2.4 no inicio do arquivo e ID3v1
 * (128 bytes "TAG" no fim). Textos convertidos para ASCII (a debug
 * screen nao tem acentos).
 */

#define ID3_TEXT 64

typedef struct {
    char title[ID3_TEXT];
    char artist[ID3_TEXT];
    char album[ID3_TEXT];
    char track[8];

    int has_cover;
    unsigned int cover_size;    /* bytes da imagem (APIC/PIC) */
    char cover_mime[16];        /* "image/jpeg", "image/png" */

    unsigned int tag_size;      /* bytes do ID3v2 no inicio (pular) */
    int has_v1;                 /* 128 bytes no fim (nao sao audio) */
} Id3Info;

/*
 * Le as tags de um arquivo aberto (fd POSIX). Devolve 0; o fd fica
 * em posicao indefinida.
 */
int id3_read(int fd, unsigned int file_size, Id3Info *info);

#endif
