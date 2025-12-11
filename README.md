# Jogo de Pesca — DE1-SoC (Linux embarcado)

Este projeto implementa um jogo de pesca rodando diretamente no hardware da DE1-SoC via Linux embarcado, usando acesso a memória mapeada e double buffering real.

**Arquitetura**
- **Memória mapeada:** `/dev/mem` para Lightweight Bridge (VGA, HEX, KEY) e SDRAM (framebuffers e buffer de caracteres).
- **Vídeo:** framebuffer RGB565 320x240 com stride físico de 512; sincronização com `wait_for_vsync()` alternando buffers.
- **Entrada:** teclado USB (`/dev/input/event0`) e botões KEY0/KEY1.

**Game Loop**
- **Entrada:** lê teclado e KEYs com debounce nos menus.
- **Atualização:** movimento do anzol em 4 direções; peixes/minas sobem com velocidade dinâmica proporcional ao placar; colisões (AABB para peixes, círculo-retângulo para minas).
- **Render:** limpa posições antigas no back-buffer e desenha todas as entidades; troca de buffers em vsync.

**Controles**
- `W/A/S/D` ou setas: mover o anzol.
- `Enter`/`KEY0`: confirmar.
- `ESC`/`Q`/`KEY1`: sair.

**Compilação e Execução (na placa)**
```bash
arm-linux-gnueabihf-gcc -O2 De1-Soc.c -o fishing_game -lm
sudo ./fishing_game
```

**Dificuldade e Pontuação**
- Brancos: 50 pts; Verdes: 150 pts (após 500); Magentas: 300 pts e 2x velocidade (após 1500); Amarelos: 1000 pts e 3x velocidade (após 3000).
- Velocidade vertical cresce com o placar até um teto; spawn acelera junto.

**Funções Principais (De1-Soc.c)**
- Vídeo: `wait_for_vsync()`, `calcula_endereco()`, `plota_pixel()`, `preencher_tela()`, `colorir_telas()`, `plotar_sprite()`.
- Entrada: `configurar_terminal_linux()`, `abrir_teclado_usb()`, `atualizar_estado_teclado()`, `le_inpt_menu()`.
- HUD/UI: `limpar_texto()`, `desenhar_texto()`, `atualizar_display()`.
- Entidades: `inicializar_inimigo()`, `mover_inimigo()`, `tenta_spawnar_inimigo()`, `mover_peixe()`, `tenta_spawnar_peixe()`.
- Jogo: `inicializar_jogo_vars()`, `desenhar_player()`, `desenhar_peixe_sprite()`, `desenhar_mina()` (círculo pelo raio), `verificar_colisao_inimigo()`, `verificar_colisao_peixe()`, `desenhar_estado()`, `executar_jogo()`.
- Estados: `executar_menu()`, `executar_armazem()`, `main()`.

Arquivos: `De1-Soc.c` (placa, Linux embarcado), `main.c` (CPUlator). Use o arquivo certo para o alvo.