# fishing_game
Jogo de Pescaria em C para DE1-SoC.

## Compilação
CPUlator ou via linha de comando:
```
gcc -O2 main.c -o fishing_game.elf
```

ou com arm-altera-eabi-gcc:
```
arm-altera-eabi-gcc -Wall -O2 main.c -o fishing_game.elf
```

## Estrutura do código (main.c)
- `HARDWARE`: constantes e funções de I/O (vsync, pixels, texto, entrada, 7-seg)
- `CORES` e `CONSTANTES`: definições do jogo
- `ESTRUTURAS`: Retangulo, Inimigo, Peixe
- `SPRITES`: bitmaps (anzol, peixe, mina)
- `DESENHO`: renderização de bitmap e círculo
- `COLISÕES`: verificação de hit
- `GAMEPLAY`: loop principal, spawn de inimigos/peixes
- `MENU/ARMAZÉM`: telas iniciais
- `MAIN`: máquina de estados

# Jogo de Pesca — CPUlator / DE1-SoC

## Principais funções

- `wait_for_vsync()` — sincroniza o buffer de vídeo (double buffering).
- `plot_pixel(x, y, cor)` — pinta um pixel no framebuffer ativo.
- `preencher_tela(cor)` — preenche toda a tela com uma cor.
- `limpar_texto()` — limpa o buffer de caracteres (texto na tela).
- `desenhar_texto(x, y, str)` — escreve texto no buffer de caracteres.
- `ler_botoes()` — lê KEY0/KEY1 com debounce simples.
- `atualizar_display(valor)` — atualiza os 4 displays de 7 segmentos com o placar.
- `desenhar_bitmap(x, y, bmp, w, h, cor, flip)` — desenha um bitmap monocromático com cor e flip horizontal.
- `desenhar_player(p, cor_override)` — desenha o anzol (player).
- `desenhar_peixe_sprite(p, cor_override)` — desenha um peixe com direção pelo `vx`.
- `desenhar_mina(ini, cor_override)` — desenha a mina como círculo sólido usando `r`.
- `limpar_anzol_frame/limpar_inimigos_frame/limpar_peixes_frame(buf)` — apagam as posições antigas no buffer anterior (double buffering).
- `ler_teclado_ps2(...)` — trata PS/2 (setas esq/dir com press/release).
- `inicializar_inimigo(ini)` — cria uma mina fora da tela com raio padrão.
- `mover_inimigo(ini)` — move a mina para cima.
- `tentar_spawn_inimigo()` — ocupa um slot livre de mina.
- `mover_peixe(p)` — move peixe para cima e rebate nas bordas.
- `tentar_spawn_peixe(score)` — cria peixe com cor/pontos/velocidade conforme pontuação.
- `atualizar_inimigos()` — move todas as minas ativas.
- `atualizar_peixes()` — move todos os peixes ativos.
- `desenhar_estado(anzol)` — desenha anzol, minas e peixes atuais.
- `verificar_colisao_inimigo(player, ini)` — colisão círculo-retângulo (mina vs. anzol).
- `verificar_colisao_peixe(player, peixe)` — colisão AABB (peixe vs. anzol).
- `inicializar_jogo_vars(...)` — reseta jogo, placar, buffers e skins.
- `executar_jogo()` — laço principal: entrada, spawn, movimento, colisão, placar, render e troca de buffer.
- `executar_menu()` — menu inicial (iniciar, armazém, sair) via KEY1/KEY0.
- `executar_armazem()` — seleção de skin do anzol via KEY1/KEY0.
- `main()` — inicializa buffers e roda a máquina de estados (menu → armazém → jogo → sair).