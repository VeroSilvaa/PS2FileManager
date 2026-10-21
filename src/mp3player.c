#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <tamtypes.h>
#include <kernel.h>
#include <loadfile.h>
#include <debug.h>
#include <libpad.h>
#include <audsrv.h>

#include "minimp3.h"
#include "id3.h"
#include "mp3player.h"

extern unsigned char AUDSRV_irx[];
extern unsigned int size_AUDSRV_irx;

#define MP3_MAX_TRACKS   512
#define MP3_NAME_MAX     256
#define MP3_PATH_MAX     1024

#define IN_BUF_SIZE      (16 * 1024)

/*
 * Animacao: barras que acompanham o volume da musica (no lugar do
 * quadro da capa, ate existir a interface grafica).
 */
#define BARS             16
#define BAR_ROWS         6

/*
 * Linhas fixas da tela (debug screen, ~28 linhas).
 */
#define ROW_STATUS       2
#define ROW_BARS         11
#define ROW_TIME         18
#define ROW_STATE        19
#define ROW_VOLUME       20

#define VOLUME_STEP      5

typedef enum {
    REPEAT_OFF = 0,
    REPEAT_ONE,
    REPEAT_ALL
} RepeatMode;

typedef enum {
    TRACK_END = 0,      /* acabou sozinha */
    TRACK_NEXT,
    TRACK_PREV,
    TRACK_STOP,
    TRACK_ERROR
} TrackResult;

static char tracks[MP3_MAX_TRACKS][MP3_NAME_MAX];
static int track_count;

static unsigned char in_buf[IN_BUF_SIZE] __attribute__((aligned(64)));
static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME] __attribute__((aligned(64)));
static mp3dec_t decoder;

static int audio_ready = 0;
static int format_rate = 0;
static int format_channels = 0;

static int shuffle = 0;
static RepeatMode repeat = REPEAT_OFF;
/*
 * Comeca no volume original da musica (o audsrv a 80 ja soa
 * bem mais baixo).
 */
static int volume = MAX_VOLUME;

static int levels[BARS];

static char last_error[128];


/* ============================================================
 * Utilitarios
 * ============================================================ */

int mp3_is_mp3(const char *name)
{
    int length = name ? strlen(name) : 0;

    return length > 4 && strcasecmp(name + length - 4, ".mp3") == 0;
}


const char *mp3player_error(void)
{
    return last_error;
}


static void join_path(char *out, int size, const char *dir, const char *name)
{
    int length = strlen(dir);

    snprintf(out, size, length > 0 && dir[length - 1] == '/' ? "%s%s" : "%s/%s", dir, name);
}


static void format_mmss(char *out, int size, unsigned int seconds)
{
    snprintf(out, size, "%02u:%02u", seconds / 60, seconds % 60);
}


/* ============================================================
 * Audio: rom0:LIBSD + audsrv.irx (uma vez)
 * ============================================================ */

static int audio_load(void)
{
    int result;
    int id;

    if (audio_ready)
        return 0;

    id = SifLoadModule("rom0:LIBSD", 0, NULL);

    printf("FM MP3: LIBSD id %d\n", id);

    if (id < 0) {
        snprintf(last_error, sizeof(last_error), "LIBSD falhou (%d)", id);
        return -1;
    }

    id = SifExecModuleBuffer(AUDSRV_irx, size_AUDSRV_irx, 0, NULL, &result);

    printf("FM MP3: AUDSRV id %d ret %d\n", id, result);

    if (id < 0 || result == 1) {
        snprintf(last_error, sizeof(last_error), "audsrv.irx falhou (%d)", id);
        return -1;
    }

    result = audsrv_init();

    if (result != 0) {
        snprintf(last_error, sizeof(last_error), "audsrv_init falhou (%d)", result);
        return -1;
    }

    audio_ready = 1;

    return 0;
}


static void audio_format(int rate, int channels)
{
    struct audsrv_fmt_t fmt;

    if (rate == format_rate && channels == format_channels)
        return;

    fmt.freq = rate;
    fmt.bits = 16;
    fmt.channels = channels;

    printf("FM MP3: formato %d Hz, %d canal(is) -> %d\n", rate, channels, audsrv_set_format(&fmt));

    audsrv_set_volume(volume);

    format_rate = rate;
    format_channels = channels;
}


/*
 * Para a saida. O audsrv so volta a tocar depois de um novo
 * audsrv_set_format: esquece o formato atual.
 */
static void audio_stop(void)
{
    audsrv_stop_audio();

    format_rate = 0;
    format_channels = 0;
}


/* ============================================================
 * Playlist: os .mp3 da pasta, em ordem alfabetica
 * ============================================================ */

static int compare_names(const void *a, const void *b)
{
    return strcasecmp((const char *)a, (const char *)b);
}


static int build_playlist(const char *dir)
{
    struct dirent *entry;
    DIR *handle;

    track_count = 0;

    handle = opendir(dir);

    if (!handle) {
        snprintf(last_error, sizeof(last_error), "nao abriu a pasta %s", dir);
        return -1;
    }

    while ((entry = readdir(handle)) != NULL && track_count < MP3_MAX_TRACKS) {

        if (mp3_is_mp3(entry->d_name)) {
            snprintf(tracks[track_count], MP3_NAME_MAX, "%s", entry->d_name);
            track_count++;
        }
    }

    closedir(handle);

    qsort(tracks, track_count, MP3_NAME_MAX, compare_names);

    printf("FM MP3: %d musica(s) em %s\n", track_count, dir);

    return track_count;
}


/* ============================================================
 * Duracao: cabecalho Xing/Info ou VBRI (VBR); senao CBR
 * ============================================================ */

static unsigned int be32(const unsigned char *p)
{
    return (unsigned int)p[0] << 24 | (unsigned int)p[1] << 16 |
           (unsigned int)p[2] << 8 | p[3];
}


/*
 * Quadros declarados no 1o frame (0 = nao e VBR com cabecalho).
 */
static unsigned int vbr_frames(const unsigned char *frame, int available)
{
    int mpeg1 = ((frame[1] >> 3) & 3) == 3;
    int mono = (frame[3] >> 6) == 3;
    int side = mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17);
    const unsigned char *xing = frame + 4 + side;
    const unsigned char *vbri = frame + 4 + 32;

    if (4 + side + 12 <= available &&
        (memcmp(xing, "Xing", 4) == 0 || memcmp(xing, "Info", 4) == 0) &&
        (be32(xing + 4) & 1))
        return be32(xing + 8);

    if (4 + 32 + 18 <= available && memcmp(vbri, "VBRI", 4) == 0)
        return be32(vbri + 14);

    return 0;
}


/* ============================================================
 * Tela
 * ============================================================ */

typedef struct {
    Id3Info id3;
    char name[MP3_NAME_MAX];
    int index;

    int hz;
    int channels;
    int bitrate;

    unsigned int duration;      /* segundos (0 = desconhecida) */
    unsigned long long samples; /* tocadas */

    int paused;
} TrackState;


static void draw_static(const TrackState *t)
{
    scr_clear();

    scr_printf("PS2FileManager - MP3 Player\n");
    scr_printf("===========================\n");

    scr_setXY(0, 4);

    scr_printf("Titulo : %.60s\n", t->id3.title[0] ? t->id3.title : t->name);
    scr_printf("Artista: %.60s\n", t->id3.artist[0] ? t->id3.artist : "-");
    scr_printf("Album  : %.60s\n", t->id3.album[0] ? t->id3.album : "-");
    scr_printf("Faixa  : %s\n", t->id3.track[0] ? t->id3.track : "-");

    if (t->id3.has_cover)
        scr_printf("Capa   : sim (%s, %u KB) - exibida na interface grafica\n",
                   t->id3.cover_mime[0] ? t->id3.cover_mime : "imagem",
                   t->id3.cover_size / 1024);
    else
        scr_printf("Capa   : nao\n");

    scr_setXY(0, 21);

    scr_printf("X play/pause | L1/R1 anterior/proxima | CIMA/BAIXO volume\n");
    scr_printf("QUADRADO aleatorio | TRIANGULO repetir | O parar\n\n");
    scr_printf("%.70s\n", t->name);
}


static void draw_dynamic(const TrackState *t)
{
    static const char *repeat_names[] = { "nao", "uma", "todas" };
    char elapsed[8];
    char total[8];
    char bar[33];
    unsigned int seconds;
    int filled;
    int r;
    int i;


    scr_setXY(0, ROW_STATUS);

    scr_printf(
        "Musica %d de %d | Aleatorio: %-3s | Repetir: %-5s   \n",
        t->index + 1,
        track_count,
        shuffle ? "sim" : "nao",
        repeat_names[repeat]
    );


    /*
     * Barras (de cima para baixo).
     */
    for (r = BAR_ROWS; r >= 1; r--) {

        scr_setXY(4, ROW_BARS + (BAR_ROWS - r));

        for (i = 0; i < BARS; i++)
            scr_printf(levels[i] >= r ? "## " : "   ");
    }


    seconds = t->hz ? (unsigned int)(t->samples / t->hz) : 0;

    if (t->duration && seconds > t->duration)
        seconds = t->duration;

    filled = t->duration ? (int)(seconds * 32 / t->duration) : 0;

    for (i = 0; i < 32; i++)
        bar[i] = i < filled ? '#' : '-';

    bar[32] = '\0';

    format_mmss(elapsed, sizeof(elapsed), seconds);
    format_mmss(total, sizeof(total), t->duration);

    scr_setXY(0, ROW_TIME);

    scr_printf("[%s] %s / %s   \n", bar, elapsed, t->duration ? total : "--:--");

    scr_printf(
        "%-8s %3d kbps  %5d Hz  %s       \n",
        t->paused ? "PAUSADO" : "TOCANDO",
        t->bitrate,
        t->hz,
        t->channels == 1 ? "mono" : "estereo"
    );


    /*
     * Volume: 20 posicoes de 5%.
     */
    for (i = 0; i < 20; i++)
        bar[i] = i < volume / 5 ? '#' : '-';

    bar[20] = '\0';

    scr_setXY(0, ROW_VOLUME);

    scr_printf("Volume [%s] %3d%%  (CIMA/BAIXO)   \n", bar, volume);
}


/*
 * Nivel de cada barra a partir de uma fatia do frame decodificado.
 */
static void update_levels(const mp3d_sample_t *samples, int count, int channels)
{
    static const int thresholds[BAR_ROWS] = { 300, 900, 2000, 4000, 8000, 14000 };
    int slice = count / BARS;
    int i;
    int j;

    if (slice <= 0)
        return;

    for (i = 0; i < BARS; i++) {

        long sum = 0;
        int level = 0;
        int average;

        for (j = 0; j < slice; j++) {
            int v = samples[(i * slice + j) * channels];
            sum += v < 0 ? -v : v;
        }

        average = (int)(sum / slice);

        while (level < BAR_ROWS && average >= thresholds[level])
            level++;

        /*
         * Sobe na hora, desce devagar.
         */
        if (level > levels[i])
            levels[i] = level;
        else if (levels[i] > 0)
            levels[i]--;
    }
}


/* ============================================================
 * Tocar uma musica
 * ============================================================ */

static TrackResult play_track(const char *dir, int index, Mp3PadFn pad)
{
    char path[MP3_PATH_MAX];
    TrackState t;
    mp3dec_frame_info_t info;
    unsigned int audio_end;
    unsigned int position;
    unsigned int size;
    clock_t last_draw = 0;
    int first_frame = 1;
    int in_len = 0;
    int eof = 0;
    int fd;


    memset(&t, 0, sizeof(t));
    memset(levels, 0, sizeof(levels));

    t.index = index;
    snprintf(t.name, sizeof(t.name), "%s", tracks[index]);

    join_path(path, sizeof(path), dir, tracks[index]);

    fd = open(path, O_RDONLY);

    if (fd < 0) {
        printf("FM MP3: nao abriu %s (%d)\n", path, fd);
        return TRACK_ERROR;
    }

    size = (unsigned int)lseek(fd, 0, SEEK_END);

    id3_read(fd, size, &t.id3);

    position = t.id3.tag_size < size ? t.id3.tag_size : 0;
    audio_end = size - (t.id3.has_v1 ? 128 : 0);

    lseek(fd, position, SEEK_SET);

    printf(
        "FM MP3: %s | %s - %s | id3 %u bytes | capa %s\n",
        t.name,
        t.id3.artist,
        t.id3.title,
        t.id3.tag_size,
        t.id3.has_cover ? "sim" : "nao"
    );

    mp3dec_init(&decoder);

    draw_static(&t);
    draw_dynamic(&t);


    while (1) {

        unsigned int buttons = pad();
        mp3d_sample_t *out;
        int samples;


        if (buttons & PAD_CIRCLE) {
            close(fd);
            return TRACK_STOP;
        }

        if (buttons & PAD_R1) {
            close(fd);
            return TRACK_NEXT;
        }

        if (buttons & PAD_L1) {
            close(fd);
            return TRACK_PREV;
        }

        if (buttons & PAD_CROSS)
            t.paused = !t.paused;

        if (buttons & PAD_SQUARE)
            shuffle = !shuffle;

        if (buttons & PAD_TRIANGLE)
            repeat = (RepeatMode)((repeat + 1) % 3);

        if (buttons & (PAD_UP | PAD_DOWN)) {

            volume += (buttons & PAD_UP) ? VOLUME_STEP : -VOLUME_STEP;

            if (volume > MAX_VOLUME)
                volume = MAX_VOLUME;

            if (volume < 0)
                volume = 0;

            audsrv_set_volume(volume);
        }


        /*
         * Tela: ~8 vezes por segundo (e logo apos um botao).
         */
        if (buttons || clock() - last_draw > CLOCKS_PER_SEC / 8) {
            draw_dynamic(&t);
            last_draw = clock();
        }


        if (t.paused) {

            clock_t wait = clock();

            while (clock() - wait < CLOCKS_PER_SEC / 50)
                ;

            continue;
        }


        /*
         * Completa o buffer de entrada.
         */
        if (!eof && in_len < IN_BUF_SIZE / 2) {

            int want = IN_BUF_SIZE - in_len;
            int got;

            if ((unsigned int)want > audio_end - position)
                want = audio_end - position;

            got = want > 0 ? read(fd, in_buf + in_len, want) : 0;

            if (got <= 0)
                eof = 1;
            else {
                in_len += got;
                position += got;
            }
        }

        if (in_len == 0) {
            close(fd);
            return TRACK_END;
        }


        samples = mp3dec_decode_frame(&decoder, in_buf, in_len, pcm, &info);

        if (info.frame_bytes == 0) {

            /*
             * Nenhum frame no buffer: precisa de mais dados (ou e lixo).
             */
            if (eof) {
                close(fd);
                return TRACK_END;
            }

            if (in_len >= IN_BUF_SIZE / 2)
                in_len = 0;

            continue;
        }


        if (first_frame && samples > 0) {

            unsigned int frames;

            first_frame = 0;

            t.hz = info.hz;
            t.channels = info.channels;

            frames = vbr_frames(in_buf + info.frame_offset, in_len - info.frame_offset);

            if (frames)
                t.duration = (unsigned int)((unsigned long long)frames * samples / info.hz);
            else if (info.bitrate_kbps)
                t.duration = (audio_end - t.id3.tag_size) / (info.bitrate_kbps * 125);

            printf(
                "FM MP3: %d Hz, %d canal(is), %d kbps, %u s (%s)\n",
                info.hz,
                info.channels,
                info.bitrate_kbps,
                t.duration,
                frames ? "VBR" : "CBR"
            );

            audio_format(info.hz, info.channels);
        }


        in_len -= info.frame_bytes;
        memmove(in_buf, in_buf + info.frame_bytes, in_len);


        if (samples > 0) {

            int bytes = samples * info.channels * (int)sizeof(mp3d_sample_t);

            out = pcm;

            t.bitrate = info.bitrate_kbps;

            audsrv_wait_audio(bytes);
            audsrv_play_audio((const char *)out, bytes);

            t.samples += samples;

            update_levels(pcm, samples, info.channels);
        }
    }
}


/* ============================================================
 * Playlist
 * ============================================================ */

int mp3player_run(const char *dir, const char *first, Mp3PadFn pad)
{
    TrackResult result;
    int failures = 0;
    int index = 0;
    int i;


    last_error[0] = '\0';

    if (audio_load() < 0)
        return -1;

    if (build_playlist(dir) <= 0) {
        if (!last_error[0])
            snprintf(last_error, sizeof(last_error), "nenhum .mp3 na pasta");
        return -1;
    }

    for (i = 0; i < track_count; i++) {
        if (strcasecmp(tracks[i], first) == 0)
            index = i;
    }

    srand((unsigned int)clock());


    while (1) {

        result = play_track(dir, index, pad);

        if (result == TRACK_STOP)
            break;

        /*
         * Arquivos que nao abrem: pula, mas nao fica em laco.
         */
        if (result == TRACK_ERROR) {

            if (++failures >= track_count) {

                /*
                 * No SMB (sem Unicode) nomes com travessao/acentos que
                 * nao existem na pagina "DOS" do servidor viram '?'.
                 */
                snprintf(
                    last_error,
                    sizeof(last_error),
                    "nenhuma musica abriu (nomes com acento/travessao no SMB?)"
                );

                audio_stop();

                return -1;
            }

            result = TRACK_NEXT;
        } else {
            failures = 0;
        }

        if (result == TRACK_PREV) {
            index = index > 0 ? index - 1 : track_count - 1;
            continue;
        }

        if (result == TRACK_END && repeat == REPEAT_ONE)
            continue;

        if (shuffle && track_count > 1) {

            int next;

            do {
                next = rand() % track_count;
            } while (next == index);

            index = next;
            continue;
        }

        if (index + 1 < track_count) {
            index++;
        } else if (repeat == REPEAT_ALL || result == TRACK_NEXT) {
            index = 0;
        } else {
            break;  /* fim da playlist */
        }
    }

    audio_stop();

    return 0;
}
