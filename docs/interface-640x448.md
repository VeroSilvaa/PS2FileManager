# PS2FileManager — Especificação da interface gráfica (640×448)

Guia para montar as telas no Figma (ou Inkscape) e para o código gsKit usar **as mesmas medidas**.
Base visual: mockups do PS2FileManager e do PS2DVDPlayer (painéis azul-escuros, seleção azul, barra de botões).

---

## 1. Tela e limites do hardware

| Item | Valor |
|---|---|
| Resolução de trabalho | **640×448** (NTSC, entrelaçado) |
| PAL | 640×512 — mesmo layout, +64 px usados para **3 linhas extras** na lista |
| Área segura (TVs cortam as bordas) | margem **24 px** nas laterais e **16 px** em cima/embaixo |
| VRAM | 4 MB no total — PNG só para **ícones e logo** (num atlas). Painéis, bordas, gradientes, seleção e barras de progresso são **desenhados por código** |
| Entrelaçado | evitar linhas de **1 px** na horizontal (cintilam) → bordas e divisórias com **2 px**; texto ≥ **14 px** |

---

## 2. Grade da tela principal (navegação)

```
 x=0                                                              x=640
 ┌───────────────────────────────────────────────────────────────────┐ y=0
 │  (margem 16)                                                      │
 │  [LOGO 240×32]                          IP 192.0.2.100   20:07    │ y=16 … 48   CABEÇALHO
 │                                                                   │
 │ ┌─DISPOSITIVOS──────┐ ┌─mc0:/BASLUS-21298─────────────────────────┐│ y=56
 │ │ ▣ mc0  Memory C.0 │ │ Nome                    Tamanho   Data    ││
 │ │ ▣ mc1  Memory C.1 │ │ 📁 BASLUS-21298            <DIR>  12/05/24 ││
 │ │ ▣ hdd0 HDD        │ │ 📄 SYSTEM.CNF              1 KB   01/08/24 ││ 12 linhas
 │ │ ▣ mass USB        │ │ …                                         ││
 │ │ ▣ smb  Rede       │ │                                           ││
 │ └───────────────────┘ └───────────────────────────────────────────┘│ y=376
 │  Status: COPIA VERIFICADA: 3 item(ns)                             │ y=380 … 396 STATUS
 │ ─────────────────────────────────────────────────────────────────  │
 │  ✕ Abrir  ○ Voltar  □ Marcar  △ Todos   R1 Menu   START Config.   │ y=404 … 432 RODAPÉ
 └───────────────────────────────────────────────────────────────────┘ y=448
  x=24                x=200 x=208                                x=616
```

| Região | x | y | largura | altura | Observação |
|---|---|---|---|---|---|
| Cabeçalho | 24 | 16 | 592 | 32 | logo à esquerda; IP e hora à direita (texto 14) |
| Painel Dispositivos | 24 | 56 | 176 | 320 | título 24 px + itens de 32 px (ícone 24) |
| Painel Arquivos | 208 | 56 | 408 | 320 | ver tabela abaixo |
| Linha de status | 24 | 380 | 592 | 16 | texto 14, cor conforme resultado |
| Rodapé (botões) | 24 | 404 | 592 | 28 | ícone 20×20 + rótulo 14; espaço 16 entre pares |

### Painel de arquivos (x=208, w=408)

| Elemento | y relativo | altura |
|---|---|---|
| Barra do caminho (`mc0:/...`) | 0 | 24 |
| Cabeçalho das colunas | 24 | 20 |
| Linhas (12 × 22 px) | 48 | 264 |
| Margem inferior | 312 | 8 |

| Coluna | x relativo | largura | Alinhamento |
|---|---|---|---|
| Caixa de seleção (modo seleção múltipla) | 6 | 16 | — |
| Ícone do tipo | 26 | 16 | — |
| Nome | 48 | 220 | esquerda (corta com "…") |
| Tamanho | 272 | 56 | **direita** (`<DIR>` para pastas) |
| Data | 336 | 66 | esquerda, formato `dd/mm/aa` |

> A coluna "Tipo" dos mockups foi retirada: o **ícone** já indica o tipo e não há largura para ela em 640 px.

---

## 3. Janelas sobrepostas

Todas com fundo da tela escurecido (preto, alfa 50%).

| Janela | x | y | w | h | Conteúdo |
|---|---|---|---|---|---|
| Menu R1 (Operações) | 392 | 80 | 216 | 272 | título 24 + 9 itens × 26 px (ícone 16 + texto 14) |
| Confirmação (excluir/mover) | 120 | 112 | 400 | 200 | ícone 48 + texto + botões Sim/Não (96×28) |
| Progresso | 100 | 104 | 440 | 224 | origem → destino, 2 barras (h 12), tempos, botão Cancelar |
| Formatação | 80 | 96 | 480 | 240 | dispositivo/capacidade/formato, lista FAT32/exFAT/APA, aviso vermelho |
| Informações / Propriedades | 120 | 120 | 400 | 192 | ícone 48 + pares rótulo/valor |
| Configuração SMB | 80 | 88 | 480 | 256 | 5 campos (h 26) + "Salvar" + Conectar/Cancelar |
| Teclado virtual | 64 | 168 | 512 | 232 | campo de texto + 4 linhas × 13 teclas (32×32) + Espaço/Apagar/OK |

---

## 4. Cores

Alfa do GS vai de **0 a 128** (128 = opaco). A coluna "Alfa" já está nessa escala.

| Uso | Hex | Alfa GS |
|---|---|---|
| Fundo — gradiente topo | `#050B1E` | 128 |
| Fundo — gradiente base | `#0A1F4A` | 128 |
| Painel — preenchimento | `#0B1630` | 100 |
| Painel — borda (2 px) | `#2E5FB8` | 128 |
| Painel — barra de título | `#12306E` | 128 |
| Seleção — gradiente | `#1E5BD8` → `#2F7BFF` | 128 |
| Linha marcada (□) | `#163B7A` | 128 |
| Texto principal | `#E6EEFF` | 128 |
| Texto secundário (colunas, rótulos) | `#9DB2D9` | 128 |
| Texto desativado | `#56627A` | 128 |
| Sucesso / "OK" | `#3DDC84` | 128 |
| Aviso | `#FFB020` | 128 |
| Erro / perigo | `#E5484D` | 128 |
| Barra de progresso — trilho | `#1A2747` | 128 |
| Barra de progresso — preenchimento | `#2F7BFF` | 128 |
| Escurecer fundo sob janelas | `#000000` | 64 |

Cantos arredondados: raio **6 px** (painéis) e **4 px** (botões/seleção).

---

## 5. Tipografia

| Uso | Tamanho | Peso |
|---|---|---|
| Listas, status, rodapé | 14 px | regular |
| Títulos de painel | 16 px | semibold |
| Título de janela | 20 px | semibold |
| Valores grandes (tela inicial) | 18 px | semibold |

**Fonte:** precisa de **acentos (português)**. Recomendação: **Noto Sans** (licença OFL, livre para redistribuir).

Opções no PS2 (decidir na implementação):

1. **FreeType + TTF** (biblioteca já disponível no SDK; é como o OPL faz) — qualquer tamanho, UTF-8, mais memória.
2. **Fonte bitmap PNG** (`GSKIT_FTYPE_PNG_DAT` do gsKit) — gerada a partir da TTF em 14 e 16 px; leve e rápida; textos em Latin-1.
3. FontM da BIOS (usada no PS2DVDPlayer) — sem arquivo extra, mas visual fixo e acentuação limitada.

No Figma, use Noto Sans nos tamanhos acima para que o mockup corresponda ao resultado.

---

## 6. PNGs (RGBA, fundo transparente)

Nomes no formato `grupo_nome_tamanho.png`. No final, os ícones pequenos são juntados num **atlas 256×256**.

| Grupo | Arquivos | Tamanho |
|---|---|---|
| Logo | `logo_filemanager_240x32` — **logotipo próprio, sem o "PS2" oficial da Sony** | 240×32 |
| Dispositivos (lista) | `dev_mc`, `dev_hdd`, `dev_usb`, `dev_smb`, `dev_ftp` | 24×24 |
| Dispositivos (tela inicial) | os mesmos | 64×64 |
| Tipos de arquivo | `file_folder`, `file_generic`, `file_image`, `file_elf`, `file_iso`, `file_audio`, `file_text`, `file_save` | 16×16 |
| Seleção | `check_on`, `check_off` | 16×16 |
| Botões do controle | `btn_cross`, `btn_circle`, `btn_square`, `btn_triangle`, `btn_l1`, `btn_r1`, `btn_select`, `btn_start` | 20×20 (L1/R1/SELECT/START: 32×20) |
| Ações do menu | `act_copy`, `act_move`, `act_paste`, `act_delete`, `act_newfolder`, `act_rename`, `act_info`, `act_format`, `act_props` | 16×16 |
| Diálogos | `dlg_warning`, `dlg_trash`, `dlg_ok`, `dlg_error`, `dlg_folder` | 48×48 |

**Não** exportar como PNG: fundo, painéis, bordas, barras de título, seleção, barras de progresso, botões Sim/Não — tudo isso é desenhado pelo código com as cores da seção 4.

---

## 7. Telas (correspondência com os mockups)

| # | Tela | Mockup de referência | Situação no código |
|---|---|---|---|
| T1 | Início — cartões de dispositivo (grade 3×2, cartão 184×132) | "Tela Inicial" | lista de texto |
| T2 | Navegação | "Navegação de Arquivos" | ✅ (texto) |
| T3 | Menu R1 | "Menu de Operações" | ✅ (texto) |
| T4 | Seleção múltipla | "Seleção Múltipla" | ✅ (texto) |
| T5 | Progresso | "Cópia / Movimento" | ✅ (texto) |
| T6 | Confirmação de exclusão | "Confirmação de Exclusão" | ✅ (texto) |
| T7 | Formatação USB/HDD | "Formatação de Mídia" | pendente |
| T8 | Configuração SMB | "Configuração SMB" | pendente (precisa do teclado) |
| T9 | Informações do arquivo | "Informações de Arquivo/Pasta" | pendente |
| T10 | Propriedades da mídia | "Propriedades da Mídia" | pendente |
| T11 | Teclado virtual | — (novo) | pendente |

Ordem sugerida para desenhar: **T2 → T3 → T6 → T5 → T1**, depois o resto.

---

## 8. Montando no Figma

1. Crie um **Frame de 640×448** para cada tela (T1…T11).
2. Em cada frame, adicione um **Layout grid**: colunas com margem 24; linhas de 22 px a partir de y=104 (área das linhas da lista).
3. Crie **componentes** para: painel (com título), item de lista (normal / selecionado / marcado), botão (normal / foco), barra de progresso, item do rodapé (ícone + rótulo).
4. Desenhe os ícones em frames do **tamanho exato** (16, 20, 24, 48, 64) — sem redimensionar depois, para não borrar.
5. Exportação: selecione o frame do ícone → *Export* → **PNG, 1x**, fundo transparente.
6. Visualização realista: veja os frames em **100%** (não ampliado) — é o tamanho real na TV.

---

## 9. Para o código (gsKit)

- Modo: `GS_MODE_NTSC` (ou PAL), entrelaçado, **640×448**, buffer duplo, 16 ou 32 bits.
- Ordem de desenho por quadro: fundo (gradiente) → painéis → textos → ícones (atlas) → janela sobreposta → rodapé.
- Redesenhar **a cada VSync** (60 Hz) — a tela gráfica não pisca como o debug screen.
- Ícones: um único `GSTEXTURE` (atlas) enviado à VRAM uma vez; cada ícone é um recorte (u, v, w, h).
