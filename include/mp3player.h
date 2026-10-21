#ifndef PS2FILEMANAGER_MP3PLAYER_H
#define PS2FILEMANAGER_MP3PLAYER_H

/*
 * Player de MP3 (debug screen), tocando em segundo plano.
 *
 * A pasta e a playlist: todos os .mp3 dela, em ordem alfabetica,
 * comecando pelo escolhido. Decodificacao: minimp3 (CC0). Saida:
 * audsrv.irx (PS2SDK) + rom0:LIBSD.
 *
 * A reproducao roda numa thread do EE (prioridade acima da
 * interface; dorme no audsrv_wait_audio). So a thread principal
 * desenha: a thread do player so atualiza o estado.
 */

typedef enum {
    MP3_CMD_PAUSE = 0,      /* alterna play/pause */
    MP3_CMD_NEXT,
    MP3_CMD_PREV,
    MP3_CMD_STOP,
    MP3_CMD_VOLUME_UP,
    MP3_CMD_VOLUME_DOWN,
    MP3_CMD_SHUFFLE,
    MP3_CMD_REPEAT
} Mp3Command;

/*
 * 1 se o nome termina em .mp3.
 */
int mp3_is_mp3(const char *name);

/*
 * Comeca a tocar a pasta 'dir' a partir de 'first' (troca a
 * playlist se ja estiver tocando). 0 = ok.
 */
int mp3player_start(const char *dir, const char *first);

void mp3player_command(Mp3Command command);

/*
 * Para e espera a thread ficar parada (antes de formatar, trocar a
 * rede...).
 */
void mp3player_stop(void);

/*
 * 1 enquanto toca ou esta pausado.
 */
int mp3player_active(void);

/*
 * Tela do player. full = redesenha tudo (ao entrar na tela); a
 * parte fixa tambem e redesenhada quando a musica muda.
 */
void mp3player_draw(int full);

/*
 * Linha curta "Tocando: Artista - Titulo 01:23 / 03:45" para o
 * cabecalho das outras telas ("" se parado).
 */
void mp3player_now_playing(char *out, int size);

const char *mp3player_error(void);

/*
 * Retrato do estado para a interface grafica (copias, seguras de
 * ler na thread principal).
 */
#define MP3_STATUS_BARS 16

typedef struct {
    int active;
    int paused;
    int shuffle;
    int repeat;                 /* 0 nao, 1 uma, 2 todas */
    int volume;                 /* 0..100 */

    int index;                  /* 0.. */
    int count;

    char name[96];              /* arquivo */
    char title[64];
    char artist[64];
    char album[64];
    char track[8];

    int has_cover;
    unsigned int cover_size;
    char cover_mime[16];

    int hz;
    int channels;
    int bitrate;

    unsigned int elapsed;       /* segundos */
    unsigned int duration;      /* 0 = desconhecida */

    int spectrum[MP3_STATUS_BARS];  /* 0..100, graves a esquerda */
} Mp3Status;

void mp3player_status(Mp3Status *status);

#endif
