# PS2FileManager

Gerenciador de arquivos homebrew para **PlayStation 2**, escrito em C com o [PS2SDK](https://github.com/ps2dev/ps2sdk).

> Em desenvolvimento. A interface atual usa a tela de depuração (texto); uma interface gráfica com gsKit está planejada.

## Download

O `PS2FileManager.elf` pronto está em [Releases](https://github.com/VeroSilvaa/PS2FileManager/releases). Execute pelo wLaunchELF, uLaunchELF, OPL (como app) ou no PCSX2.

## Dispositivos

| Dispositivo | Caminho | Navegar | Copiar | Mover | Excluir |
|---|---|---|---|---|---|
| Memory Card 1 e 2 | `mc0:` / `mc1:` | ✅ | ✅ | ✅ | ✅ |
| Rede SMB | `smb0:` | ✅ | ✅ | ✅ | ✅ |
| HDD interno (APA/PFS) | `hdd0:` → `pfs0:`/`pfs1:` | ✅ | ✅ | ✅ | ✅ |
| Pendrive USB (FAT32/exFAT) | `mass0:` / `mass1:` | ✅ | ✅ | ✅ | ✅ |

Mover = cópia verificada → conferência dos totais (arquivos e bytes) → exclusão da origem. Se algo não bater, a origem é mantida.

## Recursos

- Cópia **verificada byte a byte** (lê o destino de volta e compara)
- Cópia recursiva de pastas e seleção múltipla
- Copiar, recortar (mover), colar e excluir entre quaisquer dispositivos
- Progresso em tempo real: barras por arquivo e total, velocidade e tempo restante
- Confirmação em tela cheia antes de excluir/mover
- Proteção dos arquivos de sistema dos saves (`icon.sys`, `*.icn`, `B?DATA-SYSTEM`)
- **Instalação de jogos no HDD no formato HDLoader** (compatível com o OPL), veja abaixo

## Instalar jogos no HDD (HDLoader)

Instala ISOs de jogos de PS2 que estejam no **USB** ou no **SMB** diretamente no HDD interno (APA), no formato que o **Open PS2 Loader** lista no modo HDD.

1. Marque uma ou mais ISOs com **Quadrado** e aperte **R1**.
2. Escolha **Instalar no HDD (HDLoader)**. A opção só fica disponível se todos os itens marcados forem ISOs de jogo de PS2 (o ID é lido do `SYSTEM.CNF`) e houver um HDD formatado em APA.
3. Confira a lista de jogos (ID, CD/DVD, tamanho) e aperte **X** para instalar.

Cada jogo vira uma partição `PP.<ID>.HDL.<nome>`, com subpartições quando ele não cabe numa só. Depois da gravação, o app:

- relê o HDD e compara com a ISO de origem;
- lê o jogo **do mesmo jeito que o OPL**, pelo cabeçalho HDL e pelos LBAs do disco, e compara setores de cada fatia.

Se qualquer etapa falhar, a partição incompleta é apagada. Um jogo já instalado (mesmo ID) não é gravado de novo, só conferido.

Limitações atuais:

- **DVD de duas camadas (DVD9)** ainda não é suportado.
- Pelo SMB a instalação é lenta, porque grava e depois confere a ISO inteira.
- Testado no PCSX2: o jogo aparece no OPL. Rodar o jogo pelo OPL **ainda não foi testado em console real**.

## Controles

| Botão | Ação |
|---|---|
| Direcional | Mover o cursor |
| X | Abrir dispositivo / entrar na pasta |
| Quadrado | Marcar/desmarcar item |
| R1 | Menu de operações (copiar, recortar, colar, excluir...) |
| Triângulo | Marcar todos / desmarcar todos (na lista de arquivos); atualizar (na lista de dispositivos) |
| Círculo | Voltar / cancelar |

Excluir ou mover arquivos de sistema de um save só é confirmado com **Triângulo** (X é recusado).

## Compilar

Requer [Docker](https://www.docker.com/). A imagem usa o toolchain oficial `ghcr.io/ps2dev/ps2dev`.

```sh
docker compose run --rm ps2filemanager make
```

O resultado é `PS2FileManager.elf`. Os módulos IOP (IRX) do PS2SDK são embutidos no ELF durante a compilação, junto com dois módulos compilados pelo próprio projeto:

- `iop/ps2hdd-hdl`: o driver APA do PS2SDK, sem modificações no código, compilado com o suporte a partições HDLoader (que no PS2SDK só existe na versão para PC).
- `iop/hdlwrite`: grava os dados do jogo nas fatias da partição HDLoader.

## Testar no PCSX2

```sh
pcsx2-qt -elf PS2FileManager.elf
```

- **HDD:** ative o HDD virtual em *Configurações → Rede e HDD*.
- **USB:** crie duas imagens de pendrive FAT32 de teste e ligue-as em *Configurações → Controles → USB Port 1/2 → USB Mass Storage*:

  ```sh
  docker compose run --rm ps2filemanager sh usb/mkimg.sh   # gera usb/usb1.img e usb/usb2.img
  ```

  O BDM numera as unidades na ordem de detecção, então `mass0:` não é necessariamente a porta 1.
- **SMB:** veja a seção abaixo.

## Configurar o SMB (rede)

Ainda não existe tela de configuração (depende do teclado virtual, planejado). Os dados do servidor estão **fixos no código**, em `src/main.c`:

```c
#define SMB_SERVER          "192.168.1.9"   /* IP do PC/servidor com o compartilhamento */
#define SMB_SHARE           "samba"         /* nome do compartilhamento */
#define SMB_USER            "guest"         /* usuario */
#define SMB_PASSWORD        ""              /* senha (vazia = sem senha) */
```

Para testar, escolha uma das opções:

1. **Usar o ELF do release sem compilar:** configure seu servidor para bater com os valores acima: IP `192.168.1.9`, compartilhamento `samba`, acesso de convidado (`guest`) sem senha.
2. **Compilar com os seus dados:** troque os quatro valores em `src/main.c` e compile (veja [Compilar](#compilar)).

Requisitos do servidor:

- **SMB1 (NT1) habilitado.** O módulo `smbman` do PS2SDK só fala SMB1, na porta 445. No Windows 10/11 o SMB1 vem desligado: ative *Suporte ao Compartilhamento de Arquivos SMB 1.0/CIFS* em *Recursos do Windows*. No Samba, use `server min protocol = NT1` no `smb.conf`.
- **DHCP na rede:** o PS2 pega IP automaticamente e espera até 20 s.
- No PCSX2, use a rede no modo *Sockets*, em *Configurações → Rede e HDD*.

> ⚠️ Excluir arquivos no SMB apaga os arquivos **de verdade** no servidor.

## Planejado

- HDLoader: DVD de duas camadas e conferência completa opcional
- Criar pasta e renomear com teclado virtual (e tela de configuração do SMB)
- Formatação de pendrive/HDD
- Interface gráfica 640×448 com gsKit
- Player de MP3, servidor FTP

## Créditos

- [PS2SDK / ps2dev](https://github.com/ps2dev) — toolchain e módulos IOP
- [wLaunchELF](https://github.com/ps2homebrew/wLaunchELF) e [Open PS2 Loader](https://github.com/ps2homebrew/Open-PS2-Loader) — referência de inicialização do IOP, HDD, USB e do formato HDLoader
- [HDLGameInstaller](https://github.com/ps2homebrew/HDLGameInstaller) e [hdl-dump](https://github.com/ps2homebrew/hdl-dump) — referência do formato HDLoader (nenhum código copiado)

## Licença

[MIT](LICENSE), exceto `iop/ps2hdd-hdl/`, que é código do PS2SDK sob a Academic Free License 2.0 ([LICENSE-PS2SDK](iop/ps2hdd-hdl/LICENSE-PS2SDK)).
