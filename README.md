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