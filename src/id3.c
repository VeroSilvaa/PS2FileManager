#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "id3.h"

/*
 * Frames de texto maiores que isso sao ignorados (capas e letras
 * sao puladas com lseek, sem ler).
 */
#define ID3_FRAME_MAX 512


/*
 * Latin-1 0xC0..0xFF sem acento (debug screen so tem ASCII).
 */
static const char latin1_ascii[64] =
    "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYPs"
    "aaaaaaaceeeeiiii" "dnooooo/ouuuuypy";


static char to_ascii(unsigned int code)
{
    if (code >= 0x20 && code < 0x7F)
        return (char)code;

    if (code >= 0xC0 && code <= 0xFF)
        return latin1_ascii[code - 0xC0];

    /*
     * Pontuacao Unicode comum em titulos.
     */
    switch (code) {
        case 0xA0:                          /* espaco nao separavel */
            return ' ';
        case 0x2013: case 0x2014:           /* travessoes */
        case 0x2022: case 0xB7:             /* marcadores */
            return '-';
        case 0x2018: case 0x2019: case 0xB4:
            return '\'';
        case 0x201C: case 0x201D:
            return '"';
    }

    return code == 0 ? 0 : '?';
}


static void put_char(char *out, int size, int *pos, unsigned int code)
{
    char c;

    /*
     * Reticencias (U+2026) viram tres pontos.
     */
    if (code == 0x2026) {
        put_char(out, size, pos, '.');
        put_char(out, size, pos, '.');
        put_char(out, size, pos, '.');
        return;
    }

    c = to_ascii(code);

    if (c && *pos < size - 1)
        out[(*pos)++] = c;
}


/*
 * Texto de um frame (byte 0 = codificacao) para ASCII.
 */
static void decode_text(const unsigned char *data, int len, char *out, int size)
{
    int encoding;
    int pos = 0;
    int i;

    out[0] = '\0';

    if (len < 1)
        return;

    encoding = data[0];
    data++;
    len--;

    if (encoding == 1 || encoding == 2) {

        /*
         * UTF-16 (1: com BOM; 2: big-endian sem BOM).
         */
        int big_endian = encoding == 2;

        i = 0;

        if (encoding == 1 && len >= 2) {
            big_endian = data[0] == 0xFE && data[1] == 0xFF;
            i = 2;
        }

        for (; i + 1 < len; i += 2) {

            unsigned int code = big_endian
                ? (unsigned int)(data[i] << 8 | data[i + 1])
                : (unsigned int)(data[i + 1] << 8 | data[i]);

            if (code == 0)
                break;

            put_char(out, size, &pos, code);
        }

    } else if (encoding == 3) {

        /*
         * UTF-8: 2 bytes (acentos) e 3 bytes (travessao, marcador).
         */
        for (i = 0; i < len && data[i]; i++) {

            unsigned int code = data[i];

            if ((code & 0xE0) == 0xC0 && i + 1 < len) {
                code = ((code & 0x1F) << 6) | (data[i + 1] & 0x3F);
                i++;
            } else if ((code & 0xF0) == 0xE0 && i + 2 < len) {
                code = ((code & 0x0F) << 12) | ((data[i + 1] & 0x3F) << 6) | (data[i + 2] & 0x3F);
                i += 2;
            } else if (code >= 0x80) {
                while (i + 1 < len && (data[i + 1] & 0xC0) == 0x80)
                    i++;
                code = '?';
            }

            put_char(out, size, &pos, code);
        }

    } else {

        for (i = 0; i < len && data[i]; i++)
            put_char(out, size, &pos, data[i]);
    }

    /*
     * Sem espacos no fim (ID3v1 completa com espacos).
     */
    while (pos > 0 && out[pos - 1] == ' ')
        pos--;

    out[pos] = '\0';
}


static unsigned int be32(const unsigned char *p)
{
    return (unsigned int)p[0] << 24 | (unsigned int)p[1] << 16 |
           (unsigned int)p[2] << 8 | p[3];
}


static unsigned int synchsafe32(const unsigned char *p)
{
    return (unsigned int)(p[0] & 0x7F) << 21 | (unsigned int)(p[1] & 0x7F) << 14 |
           (unsigned int)(p[2] & 0x7F) << 7 | (p[3] & 0x7F);
}


/*
 * Capa (APIC v2.3/2.4: enc, mime\0, tipo, descricao\0, imagem;
 * PIC v2.2: enc, formato 3 letras, tipo, descricao\0, imagem).
 */
static void read_cover(int fd, unsigned int frame_size, int v22, Id3Info *info)
{
    unsigned char head[64];
    int got;

    info->has_cover = 1;
    info->cover_size = frame_size;

    got = read(fd, head, frame_size < sizeof(head) ? frame_size : sizeof(head));

    if (got < 2)
        return;

    if (v22) {
        snprintf(info->cover_mime, sizeof(info->cover_mime), "image/%.3s", (const char *)head + 1);
    } else {
        int i;

        for (i = 1; i < got && head[i] && i - 1 < (int)sizeof(info->cover_mime) - 1; i++)
            info->cover_mime[i - 1] = head[i];

        info->cover_mime[i - 1] = '\0';
    }
}


static void read_v2(int fd, Id3Info *info)
{
    unsigned char header[10];
    unsigned char frame[ID3_FRAME_MAX];
    unsigned int pos;
    unsigned int end;
    int version;
    int v22;

    if (lseek(fd, 0, SEEK_SET) < 0 || read(fd, header, 10) != 10)
        return;

    if (memcmp(header, "ID3", 3) != 0)
        return;

    version = header[3];
    v22 = version == 2;

    info->tag_size = 10 + synchsafe32(header + 6);

    if (header[5] & 0x10)
        info->tag_size += 10;   /* rodape v2.4 */

    end = info->tag_size;
    pos = 10;

    /*
     * Cabecalho estendido (v2.3/2.4): pulado.
     */
    if (!v22 && (header[5] & 0x40)) {

        unsigned char ext[4];

        if (read(fd, ext, 4) != 4)
            return;

        pos += version == 4 ? synchsafe32(ext) : 4 + be32(ext);
    }


    while (pos + (v22 ? 6 : 10) <= end) {

        char id[5];
        unsigned int size;
        int header_len = v22 ? 6 : 10;

        if (lseek(fd, pos, SEEK_SET) < 0 || read(fd, frame, header_len) != header_len)
            return;

        if (frame[0] == 0)
            return;     /* preenchimento */

        if (v22) {
            memcpy(id, frame, 3);
            id[3] = '\0';
            size = (unsigned int)frame[3] << 16 | (unsigned int)frame[4] << 8 | frame[5];
        } else {
            memcpy(id, frame, 4);
            id[4] = '\0';
            size = version == 4 ? synchsafe32(frame + 4) : be32(frame + 4);
        }

        pos += header_len;

        if (size == 0 || pos + size > end)
            return;


        if (strcmp(id, "APIC") == 0 || strcmp(id, "PIC") == 0) {

            read_cover(fd, size, v22, info);

        } else if (size <= ID3_FRAME_MAX) {

            char *target = NULL;
            int target_size = ID3_TEXT;

            if (!strcmp(id, "TIT2") || !strcmp(id, "TT2"))
                target = info->title;
            else if (!strcmp(id, "TPE1") || !strcmp(id, "TP1"))
                target = info->artist;
            else if (!strcmp(id, "TALB") || !strcmp(id, "TAL"))
                target = info->album;
            else if (!strcmp(id, "TRCK") || !strcmp(id, "TRK")) {
                target = info->track;
                target_size = sizeof(info->track);
            }

            if (target && read(fd, frame, size) == (int)size)
                decode_text(frame, size, target, target_size);
        }

        pos += size;
    }
}


static void read_v1(int fd, unsigned int file_size, Id3Info *info)
{
    unsigned char tag[128];
    unsigned char text[31];

    if (file_size < 128 ||
        lseek(fd, file_size - 128, SEEK_SET) < 0 ||
        read(fd, tag, 128) != 128 ||
        memcmp(tag, "TAG", 3) != 0)
        return;

    info->has_v1 = 1;

    /*
     * v1 so completa o que o v2 nao trouxe (Latin-1, 30 bytes).
     */
    text[0] = 0;

    if (!info->title[0]) {
        memcpy(text + 1, tag + 3, 30);
        decode_text(text, 31, info->title, ID3_TEXT);
    }

    if (!info->artist[0]) {
        memcpy(text + 1, tag + 33, 30);
        decode_text(text, 31, info->artist, ID3_TEXT);
    }

    if (!info->album[0]) {
        memcpy(text + 1, tag + 63, 30);
        decode_text(text, 31, info->album, ID3_TEXT);
    }

    /*
     * ID3v1.1: faixa no byte 126 (com 125 = 0).
     */
    if (!info->track[0] && tag[125] == 0 && tag[126] != 0)
        snprintf(info->track, sizeof(info->track), "%d", tag[126]);
}


int id3_read(int fd, unsigned int file_size, Id3Info *info)
{
    memset(info, 0, sizeof(Id3Info));

    read_v2(fd, info);
    read_v1(fd, file_size, info);

    return 0;
}
