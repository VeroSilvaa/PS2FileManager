#ifndef PS2FILEMANAGER_GFX_H
#define PS2FILEMANAGER_GFX_H

/*
 * Interface grafica (Etapa 10) com gsKit, 640x448.
 *
 * Passo 1: o fundo + a "camada de texto" (console.c), que recebe o
 * que as telas antigas escrevem com scr_printf e desenha com a
 * fonte nova. As telas convertidas desenham direto com gsKit.
 */

int gfx_init(void);

/*
 * Desenha um quadro (fundo + texto) e troca o buffer. Espera o
 * VSync: pode ser chamado em laco (60 Hz).
 */
void gfx_frame(void);

/*
 * Como gfx_frame, mas no maximo ~20 vezes por segundo (para telas
 * de progresso que escrevem muito).
 */
void gfx_frame_throttled(void);

#endif
