# PS2FileManager

Gerenciador de arquivos homebrew para **PlayStation 2**, escrito em C com o [PS2SDK](https://github.com/ps2dev/ps2sdk).

> Em desenvolvimento. Interface gráfica 640×448 (gsKit), em português, toda operada pelo controle.

## Download

O `PS2FileManager.elf` pronto está em [Releases](https://github.com/VeroSilvaa/PS2FileManager/releases). Execute pelo wLaunchELF, uLaunchELF, OPL (como app) ou no PCSX2.

## Dispositivos

| Dispositivo | Caminho | Navegar | Copiar | Mover | Excluir | Formatar |
|---|---|---|---|---|---|---|
| Memory Card 1 e 2 | `mc0:` / `mc1:` | ✅ | ✅ | ✅ | ✅ | nunca |
| Rede SMB | `smb0:` | ✅ | ✅ | ✅ | ✅ | — |
| HDD interno (APA/PFS) | `hdd0:` → `pfs0:`/`pfs1:` | ✅ | ✅ | ✅ | ✅ | APA |
| HDD interno (MBR/GPT + exFAT/FAT32) | `ata0:` | ✅ | ✅ | ✅ | ✅ | exFAT, FAT32 |
| Pendrive USB (FAT32/exFAT) | `usb0:` / `usb1:` | ✅ | ✅ | ✅ | ✅ | exFAT, FAT32 |

Mover = cópia verificada → conferência dos totais (arquivos e bytes) → exclusão da origem. Se algo não bater, a origem é mantida.

## Recursos

- **Interface gráfica**: tela inicial com cartões dos dispositivos, lista de arquivos com ícones por tipo (pasta, música, app ELF, imagem de disco), janelas de menu, confirmação, progresso e erro, e dicas com os botões do controle
- **Player de MP3** com tocar em segundo plano, veja abaixo
- **Executar apps ELF** direto da lista de arquivos, veja abaixo
- Nomes com **acentos** no SMB (UTF-8) e no pendrive
- Cópia **verificada byte a byte** (lê o destino de volta e compara); no Colar, **Quadrado** desliga a conferência para copiar mais rápido (o mover sempre confere)
- Cópia recursiva de pastas e seleção múltipla
- Copiar, recortar (mover), colar e excluir entre quaisquer dispositivos
- **Criar pasta** e **renomear** com teclado virtual, com página de acentos (no Memory Card, nomes de até 31 caracteres e sem acentos)
- **Configuração de rede e SMB** na própria tela: DHCP ou IP estático, servidor, compartilhamento, usuário e senha
- Progresso em tempo real: barras por arquivo e total, velocidade e tempo restante
- Confirmação em tela cheia antes de excluir/mover
- Proteção dos arquivos de sistema dos saves (`icon.sys`, `*.icn`, `B?DATA-SYSTEM`)
- **Instalação de jogos no HDD no formato HDLoader** (compatível com o OPL), veja abaixo
- **Formatação** de pendrive e HDD, e criar/excluir partições do HDD, veja abaixo

## Player de MP3

**X** sobre um `.mp3` abre o player: a **pasta vira a playlist** (em ordem alfabética, começando pela música escolhida). Funciona com músicas no pendrive, no HDD e no SMB.

- Título, artista, álbum e faixa das tags **ID3** (v1, v2.2, v2.3, v2.4); duração para MP3 CBR e VBR (Xing/VBRI)
- Analisador de espectro (16 faixas), barra de tempo e volume
- Botões na tela (**Esquerda/Direita** escolhe, **X** aciona): anterior, tocar/pausar, próxima, parar, aleatório, repetir (uma/todas)
- **Start** tocar/pausar, **L1/R1** faixa anterior/próxima, **Cima/Baixo** volume
- **Círculo** volta para a lista **com a música tocando**; **Select** reabre o player. A música atual aparece no topo da tela.

Decodificação pelo [minimp3](https://github.com/lieff/minimp3) (CC0) e saída pelo `audsrv` do PS2SDK.

## Executar apps ELF

**X** sobre um `.elf` pede confirmação e executa o programa (como o wLaunchELF faz). Testado no PCSX2 com o OPL a partir do pendrive.

## Formatar (pendrive e HDD)

Na lista de dispositivos, **R1** sobre o pendrive ou o HDD. Toda formatação pede **Triângulo** e depois **Start**. Memory Cards nunca são formatados.

**Pendrive:** escolha o disco na lista (com tamanho, tabela e formato atuais).

- Sem tabela de partições: o programa pergunta se deseja criar e oferece **MBR** ou **GPT**.
- Com tabela: **FAT32**, **exFAT** ou recriar a tabela.

**HDD interno:**

- **APA** (padrão do PS2): cria `__mbr`, `__net`, `__system`, `__sysconf` e `__common`, como o wLaunchELF.
- **MBR/GPT + exFAT ou FAT32**: o formato do modo **"BDM HDD"** do OPL. O HDD passa a abrir como `ata0:`.
- De MBR/GPT dá para voltar ao APA pelo mesmo menu.

**Partições do HDD (APA):** na lista de partições, **R1** cria (nome pelo teclado virtual, tamanho em uma janela com barra: Esquerda/Direita ±128 MB, L1/R1 ±1 GB) ou exclui uma partição. Partições do sistema (`__*`) não podem ser excluídas.

**Segurança:** antes de carregar o driver APA, o programa lê os setores 0 e 1 do HDD, como o OPL. Se o disco for MBR/GPT ou tiver dados desconhecidos, o driver APA **não é carregado** (ele grava no disco ao iniciar), e nada é alterado.

A formatação FAT32/exFAT usa um módulo IOP próprio (`iop/fatfmt`) com o [FatFs](http://elm-chan.org/fsw/ff/) de ChaN, porque o driver de FAT do PS2SDK não formata.

### HDD em exFAT no OPL

1. Formate o HDD em **MBR** ou **GPT** + **exFAT** pelo PS2FileManager.
2. Copie as ISOs para as pastas `CD/` ou `DVD/` na raiz do HDD (`ata0:/CD/...`). Qualquer nome serve (o OPL lê o ID no `SYSTEM.CNF`).
3. No OPL, em *Dispositivos BDM*, ligue o **HDD (ATA)** e **desligue o modo HDD (APA)**: os dois modos não funcionam juntos.

Cada ISO pode ter no máximo 64 fragmentos (limite do OPL). Num disco recém-formatado a cópia grava em sequência.

## Instalar jogos no HDD (HDLoader)

Instala ISOs de jogos de PS2 que estejam no **USB** ou no **SMB** diretamente no HDD interno (APA), no formato que o **Open PS2 Loader** lista no modo HDD.

1. Marque uma ou mais ISOs com **Quadrado** e aperte **R1**.
2. Escolha **Instalar no HDD (HDLoader)**. A opção só fica disponível se todos os itens marcados forem ISOs de jogo de PS2 (o ID é lido do `SYSTEM.CNF`) e houver um HDD formatado em APA.
3. Confira a lista de jogos (ID, CD/DVD, tamanho e situação: pronto, já instalado ou não suportado) e o espaço livre no HDD. **Quadrado** liga ou desliga a *conferência completa* (padrão: desligada). Aperte **X** para instalar.

Cada jogo vira uma partição `PP.<ID>.HDL.<nome>`, com subpartições quando ele não cabe numa só (jogos acima de 1 GB). Depois da gravação, o app:

- lê o jogo **do mesmo jeito que o OPL**, pelo cabeçalho HDL e pelos LBAs do disco, e compara setores de cada fatia (sempre, é rápido);
- com a conferência completa ligada, relê também o HDD inteiro e compara com a ISO de origem (dobra o tempo).

Se qualquer etapa falhar, a partição incompleta é apagada. Um jogo já instalado (mesmo ID) não é gravado de novo, só conferido.

Limitações atuais:

- **DVD de duas camadas (DVD9)** ainda não é suportado.
- Pelo SMB a instalação é lenta (no PCSX2, cerca de 700 KB/s: um jogo de 1,2 GB leva uns 30 minutos sem a conferência completa).
- Testado no PCSX2: o jogo aparece no OPL. Rodar o jogo pelo OPL **ainda não foi testado em console real**.

## Controles

**Tela inicial (dispositivos)**

| Botão | Ação |
|---|---|
| Cima/Baixo, Esquerda/Direita | Escolher o dispositivo (lista e cartões) |
| X | Abrir o dispositivo |
| R1 | Formatar (HDD e pendrive) |
| Triângulo | Atualizar |
| L1 | Voltar para a pasta que estava aberta |

**Lista de arquivos**

| Botão | Ação |
|---|---|
| Cima/Baixo | Mover o cursor |
| X | Entrar na pasta / tocar MP3 / executar ELF |
| Quadrado | Marcar/desmarcar item |
| Triângulo | Marcar todos / desmarcar todos |
| R1 | Menu de operações (copiar, recortar, colar, excluir, criar pasta, renomear, HDLoader...) |
| Esquerda | Ir para a lista de dispositivos |
| Círculo | Voltar |
| Select | Abrir o player (com música tocando) |

Excluir, mover ou renomear arquivos de sistema de um save só é confirmado com **Triângulo** (X é recusado).

### Teclado virtual

| Botão | Ação |
|---|---|
| Direcional | Mover na grade (a última linha tem Teclas, Espaço, Apagar e OK) |
| X | Digitar o caractere / acionar a tecla |
| Quadrado | Apagar |
| R1 | Espaço |
| Triângulo | Trocar entre maiúsculas, minúsculas, símbolos e acentos |
| Start | Confirmar |
| Círculo | Cancelar |

Nos campos de IP o teclado é **numérico** (0–9 e ponto, com Apagar e Limpar).

## Compilar

Requer [Docker](https://www.docker.com/). A imagem usa o toolchain oficial `ghcr.io/ps2dev/ps2dev`.

```sh
docker compose run --rm ps2filemanager make
```

O resultado é `PS2FileManager.elf`. Os módulos IOP (IRX) do PS2SDK são embutidos no ELF durante a compilação, junto com módulos compilados pelo próprio projeto:

- `iop/ps2hdd-hdl`: o driver APA do PS2SDK, sem modificações no código, compilado com o suporte a partições HDLoader (que no PS2SDK só existe na versão para PC).
- `iop/hdlwrite`: grava os dados do jogo nas fatias da partição HDLoader.
- `iop/fatfmt`: formatação FAT32/exFAT e tabelas MBR/GPT (FatFs).
- `iop/smbman-utf8`: o `smbman` do PS2SDK com uma única mudança, a conversão dos nomes UTF-16 ↔ UTF-8 (o original trocava todo caractere fora do ASCII por `?`, e pastas com acento não abriam).

As imagens da interface (fundo, fontes, logotipo e botões) ficam em `assets/` e são geradas por `tools/make_assets.sh` a partir das artes originais.

## Testar no PCSX2

```sh
pcsx2-qt -elf PS2FileManager.elf
```

- **HDD:** ative o HDD virtual em *Configurações → Rede e HDD*.
- **USB:** crie duas imagens de pendrive FAT32 de teste e ligue-as em *Configurações → Controles → USB Port 1/2 → USB Mass Storage*:

  ```sh
  docker compose run --rm ps2filemanager sh usb/mkimg.sh   # gera usb/usb1.img e usb/usb2.img
  ```

  O BDM numera as unidades na ordem de detecção, então `usb0:` não é necessariamente a porta 1. Para conferir as imagens depois de formatar: `docker compose run --rm ps2filemanager sh usb/checkimg.sh`.

  Problema conhecido no PCSX2: com dois pendrives, se o primeiro a conectar estiver em **GPT**, o segundo falha ao conectar e o driver USB trava. Com um pendrive só, ou com o GPT conectando em segundo, funciona.
- **SMB:** veja a seção abaixo.

## Configurar o SMB (rede)

Na tela inicial, abra **Configurações** (último item):

| Campo | Padrão |
|---|---|
| Modo de IP | DHCP (X ou Esquerda/Direita alternam para estático) |
| IP do PS2, máscara, gateway, DNS | usados só no modo estático |
| Servidor SMB | `192.168.1.9` (só IP, sem nome) |
| Compartilhamento | `samba` |
| Usuário | `guest` |
| Senha | vazia |

X edita o campo com o teclado virtual; **Start** (ou *Salvar*) grava em `mc0:/PS2FM/network.cfg` (ou `mc1:` sem cartão no slot 1). Ao salvar, a conexão SMB é refeita com os dados novos na próxima vez que o SMB for aberto.

> A senha fica gravada em texto puro no arquivo do Memory Card.

Requisitos do servidor:

- **SMB1 (NT1) habilitado.** O módulo `smbman` do PS2SDK só fala SMB1, na porta 445. No Windows 10/11 o SMB1 vem desligado: ative *Suporte ao Compartilhamento de Arquivos SMB 1.0/CIFS* em *Recursos do Windows*. No Samba, use `server min protocol = NT1` no `smb.conf`.
- **Compartilhamento com senha: NTLMv1.** O `smbman` autentica com NTLMv1. O Samba 4.5 ou mais novo recusa isso por padrão (erro *LOGON: autenticacao recusada*); acrescente no `[global]` do `smb.conf`:

  ```ini
  ntlm auth = ntlmv1-permitted
  ```

  e reinicie o `smbd`. Acesso de convidado (sem senha) não precisa disso.
- **Rede:** com DHCP o PS2 espera até 20 s pelo IP; com IP estático, só pelo link.
- No PCSX2, use a rede no modo *Sockets*, em *Configurações → Rede e HDD*.

> ⚠️ Excluir arquivos no SMB apaga os arquivos **de verdade** no servidor.

## Planejado

- HDLoader: DVD de duas camadas
- Player de MP3: capa do álbum
- Servidor FTP
- Testes em console real (log de depuração gravado no SMB/pendrive)
- Áudio Bluetooth para o player (experimental, depende de testes em console real)

## Apoie o projeto

Se o PS2FileManager foi útil para você, considere apoiar o desenvolvimento com uma doação via **Pix** (qualquer valor):

<img src="docs/pix-qrcode.png" alt="QR Code Pix para doação" width="220">

**Pix copia e cola:**

```
00020126580014BR.GOV.BCB.PIX01362c7f2bbb-2b6b-4b41-8b5c-b47a222531d65204000053039865802BR5915VERIDIANO SILVA6008REDENCAO62070503***6304DD08
```

Chave aleatória: `2c7f2bbb-2b6b-4b41-8b5c-b47a222531d6` (Veridiano Silva)

## Créditos

- [PS2SDK / ps2dev](https://github.com/ps2dev) — toolchain e módulos IOP
- [wLaunchELF](https://github.com/ps2homebrew/wLaunchELF) e [Open PS2 Loader](https://github.com/ps2homebrew/Open-PS2-Loader) — referência de inicialização do IOP, HDD, USB e do formato HDLoader
- [HDLGameInstaller](https://github.com/ps2homebrew/HDLGameInstaller) e [hdl-dump](https://github.com/ps2homebrew/hdl-dump) — referência do formato HDLoader (nenhum código copiado)
- [FatFs](http://elm-chan.org/fsw/ff/) (ChaN) — formatação FAT32/exFAT
- [gsKit](https://github.com/ps2dev/gsKit) e libpng — interface gráfica
- [minimp3](https://github.com/lieff/minimp3) (lieff) — decodificação de MP3
- Fontes [Noto Sans](https://fonts.google.com/noto) e [Orbitron](https://fonts.google.com/specimen/Orbitron) (SIL Open Font License)

## Licença

[MIT](LICENSE), exceto:

- `iop/ps2hdd-hdl/` e `iop/smbman-utf8/`: código do PS2SDK sob a Academic Free License 2.0 ([LICENSE-PS2SDK](iop/ps2hdd-hdl/LICENSE-PS2SDK));
- `iop/fatfmt/fatfs/`: FatFs de ChaN (fork usado pelo PS2SDK), sob a licença FatFs ([LICENSE-FatFs.txt](iop/fatfmt/fatfs/LICENSE-FatFs.txt));
- `third_party/minimp3/`: CC0 ([LICENSE](third_party/minimp3/LICENSE));
- `third_party/fonts/`: SIL Open Font License ([OFL.txt](third_party/fonts/OFL.txt), [OFL-Orbitron.txt](third_party/fonts/OFL-Orbitron.txt)).
