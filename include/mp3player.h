#ifndef PS2FILEMANAGER_MP3PLAYER_H
#define PS2FILEMANAGER_MP3PLAYER_H

/*
 * Player de MP3 (fase 1: debug screen).
 *
 * A pasta e a playlist: todos os .mp3 dela, em ordem alfabetica,
 * comecando pelo escolhido. Decodificacao: minimp3 (CC0). Saida:
 * audsrv.irx (PS2SDK) + rom0:LIBSD.
 *
 * X play/pause | L1/R1 anterior/proxima | CIMA/BAIXO volume
 * QUADRADO aleatorio | TRIANGULO repetir (nao/uma/todas) | O parar
 */

/*
 * Botoes recem-pressionados (mascara PAD_* da libpad); 0 se nada.
 */
typedef unsigned int (*Mp3PadFn)(void);

/*
 * 1 se o nome termina em .mp3.
 */
int mp3_is_mp3(const char *name);

/*
 * Toca a pasta 'dir' a partir de 'first' (nome do arquivo). Volta
 * quando o usuario aperta O ou a playlist termina. 0 = ok.
 */
int mp3player_run(const char *dir, const char *first, Mp3PadFn pad);

const char *mp3player_error(void);

#endif
