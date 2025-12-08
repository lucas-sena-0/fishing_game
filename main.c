/*
 * JOGO DE PESCA - VERSÃO VISUAL PREMIUM (MINA GORDINHA)
 * Atualizações: 
 * - Sprite da Mina alterado para parecer uma esfera de ferro pesada
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// --- ENDEREÇOS ---
#define PIXEL_BUF_CTRL_BASE 0xFF203020
#define PS2_DATA_REG        0xFF200100
#define HEX3_HEX0_BASE      0xFF200020
#define KEY_BASE            0xFF200050 
#define CHAR_BUF_BASE       0xC9000000 

#define WIDTH  320
#define HEIGHT 240

// --- CORES ---
#define BLUE   0x001F  // Fundo
#define BLACK  0x0000
#define WHITE  0xFFFF  // Peixe Comum
#define RED    0xF800  // Minas
#define LIME   0x07E0  // Peixe Médio
#define MAGENTA 0xF81F // Peixe Raro
#define YELLOW 0xFFE0  // Peixe Lendário
#define ORANGE 0xFD20  // Skin Neon

// --- CONFIG ---
#define MAX_INIMIGOS 6
#define MAX_PEIXES 5
#define RAIO_INIMIGO 7  
#define VELOCIDADE_SUBIDA 3

const int ADDR_BUFFER1 = 0xC8000000;
const int ADDR_BUFFER2 = 0xC0000000;

typedef enum { ESTADO_MENU, ESTADO_ARMAZEM, ESTADO_JOGO, ESTADO_SAIR } GameState;

// --- BITMAPS (SPRITES) ---

// SPRITE ANZOL (16x16)
const uint16_t sprite_anzol[16] = {
    0x03C0, 0x03C0, 0x03C0, 0x0180, 
    0x0180, 0x0180, 0x0180, 0x0180, 
    0x0180, 0x0180, 0x6180, 0x7180, 
    0x7980, 0x3F80, 0x1F00, 0x0E00  
};

// SPRITE PEIXE (16x10)
const uint16_t sprite_peixe[10] = {
    0x0000, 0x0030, 0x0078, 0x03FC, 0x0FFE, 
    0x3FFF, 0x3FFF, 0x0FFE, 0x03FC, 0x0110  
};

// NOVO SPRITE MINA "GORDINHA" (16x16)
// Uma esfera sólida ocupando quase todo o espaço, com pinos curtos.
const uint16_t sprite_mina[16] = {
    0x0180, //        XX        (Pino Topo)
    0x03C0, //       XXXX
    0x0FF0, //      XXXXXX
    0x1FF8, //    XXXXXXXXX
    0x3FFC, //   XXXXXXXXXX
    0x7FFE, //  XXXXXXXXXXX     (Corpo gordo)
    0xFFFF, // XXXXXXXXXXXXXX
    0xFFFF, // XXXXXXXXXXXXXX   (Centro Sólido)
    0xFFFF, // XXXXXXXXXXXXXX
    0xFFFF, // XXXXXXXXXXXXXX
    0x7FFE, //  XXXXXXXXXXX
    0x3FFC, //   XXXXXXXXXX
    0x1FF8, //    XXXXXXXXX
    0x0FF0, //      XXXXXX
    0x03C0, //       XXXX
    0x0180  //        XX        (Pino Fundo)
};

// --- ESTRUTURAS ---
typedef struct {
    int x, y, w, h;
    int old_x[2], old_y[2]; 
    uint16_t cor; 
    int tipo_skin; 
} Retangulo;

typedef struct {
    int x, y, r;
    bool ativo;
    int old_x[2], old_y[2];
    bool old_ativo[2];
} Inimigo;

typedef struct {
    int x, y, w, h;
    int vx;
    uint16_t cor;
    int pontos;
    bool ativo;
    int old_x[2], old_y[2];
    bool old_ativo[2];
    int facing_right; 
} Peixe;

// --- GLOBAIS ---
volatile int * pixel_ctrl_ptr = (volatile int *) PIXEL_BUF_CTRL_BASE;
volatile int * ps2_ptr        = (volatile int *) PS2_DATA_REG;
volatile int * hex3_hex0_ptr  = (volatile int *) HEX3_HEX0_BASE;
volatile int * key_ptr        = (volatile int *) KEY_BASE;
volatile int pixel_buffer_start;
const char seg7_table[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x67};
Inimigo inimigos[MAX_INIMIGOS];
Peixe peixes[MAX_PEIXES];
int skin_selecionada = 0; 

// --- FUNÇÕES BÁSICAS ---

void wait_for_vsync() {
    volatile int * status_ptr = (volatile int *) (PIXEL_BUF_CTRL_BASE + 0xC);
    *pixel_ctrl_ptr = 1; 
    while ((*status_ptr & 0x01) != 0);
}

void plot_pixel(int x, int y, uint16_t color) {
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
        volatile short *pixel_addr = (volatile short *)(pixel_buffer_start + (y << 10) + (x << 1));
        *pixel_addr = color;
    }
}

void preencher_tela(uint16_t cor) {
    for (int y = 0; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++)
            plot_pixel(x, y, cor);
}

void limpar_texto() {
    for(int y=0; y<60; y++) {
        for(int x=0; x<80; x++) {
            volatile char * c = (char *) (CHAR_BUF_BASE + (y<<7) + x);
            *c = 0; 
        }
    }
}

void desenhar_texto(int x, int y, char * text) {
    volatile char * character_buffer = (char *) (CHAR_BUF_BASE + (y<<7) + x);
    while (*text) {
        *character_buffer = *text;
        character_buffer++;
        text++;
    }
}

int ler_botoes() {
    int keys = *key_ptr;
    if (keys != 0) {
        for(volatile int i=0; i<300000; i++); 
        return keys;
    }
    return 0;
}

void atualizar_display(int valor) {
    if (valor > 9999) valor = 9999;
    int d0 = valor % 10;
    int d1 = (valor / 10) % 10;
    int d2 = (valor / 100) % 10;
    int d3 = (valor / 1000);
    int hex_code = seg7_table[d0] |
                   (seg7_table[d1] << 8) | (seg7_table[d2] << 16) | (seg7_table[d3] << 24);
    *hex3_hex0_ptr = hex_code;
}

// --- DESENHO DE BITMAPS ---

void desenhar_bitmap(int x, int y, const uint16_t *bitmap, int w, int h, uint16_t cor, int flip_x) {
    for (int i = 0; i < h; i++) {
        uint16_t linha = bitmap[i];
        for (int j = 0; j < w; j++) {
            int col_index = flip_x ? j : (w - 1 - j);
            if ((linha >> col_index) & 0x01) {
                plot_pixel(x + j, y + i, cor);
            }
        }
    }
}

void desenhar_circulo(int cx, int cy, int r, uint16_t cor) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if ((x*x) + (y*y) <= (r*r)) {
                plot_pixel(cx + x, cy + y, cor);
            }
        }
    }
}

void desenhar_player(Retangulo *p, uint16_t cor_override) {
    uint16_t cor_final = (cor_override == BLUE) ? BLUE : p->cor;
    desenhar_bitmap(p->x, p->y, sprite_anzol, 16, 16, cor_final, 0);
}

void desenhar_peixe_sprite(Peixe *p, uint16_t cor_override) {
    uint16_t cor_final = (cor_override == BLUE) ? BLUE : p->cor;
    int direcao = (p->vx > 0) ? 1 : 0;
    desenhar_bitmap(p->x, p->y, sprite_peixe, 16, 10, cor_final, direcao);
}

void desenhar_mina(Inimigo *ini, uint16_t cor_override) {
    uint16_t cor_final = (cor_override == BLUE) ? BLUE : RED;
    desenhar_bitmap(ini->x - 8, ini->y - 8, sprite_mina, 16, 16, cor_final, 0);
}

// --- COLISÕES ---
bool verificar_colisao_inimigo(Retangulo player, Inimigo ini) {
    int testX = ini.x; int testY = ini.y;
    
    if (ini.x < player.x) testX = player.x;
    else if (ini.x > player.x + player.w) testX = player.x + player.w;
    
    if (ini.y < player.y) testY = player.y;
    else if (ini.y > player.y + player.h) testY = player.y + player.h;
    
    int distX = ini.x - testX; int distY = ini.y - testY;
    
    return ((distX * distX) + (distY * distY)) <= (ini.r * ini.r);
}

bool verificar_colisao_peixe(Retangulo p, Peixe f) {
    if (p.x + p.w < f.x || p.x > f.x + f.w || p.y + p.h < f.y || p.y > f.y + f.h) return false;
    return true;
}

// --- INICIALIZAÇÃO DO JOGO ---
void inicializar_jogo_vars(Retangulo *anzol, int *score, int *frame_counter, int *timer_pontuacao) {
    if (skin_selecionada == 0)      anzol->cor = WHITE;
    else if (skin_selecionada == 1) anzol->cor = YELLOW;
    else if (skin_selecionada == 2) anzol->cor = ORANGE;

    anzol->w = 16; 
    anzol->h = 16; 
    anzol->tipo_skin = skin_selecionada;

    anzol->x = 150; anzol->y = 20;
    anzol->old_x[0] = anzol->old_x[1] = 150;
    anzol->old_y[0] = anzol->old_y[1] = 20;

    for(int i=0; i<MAX_INIMIGOS; i++) { 
        inimigos[i].ativo = false;
        inimigos[i].old_ativo[0] = inimigos[i].old_ativo[1] = false; 
    }
    for(int i=0; i<MAX_PEIXES; i++) { 
        peixes[i].ativo = false;
        peixes[i].old_ativo[0] = peixes[i].old_ativo[1] = false; 
    }
    
    *score = 0; *frame_counter = 0;
    *timer_pontuacao = 0;
    atualizar_display(0);
}

// --- GAMEPLAY ---
void executar_jogo() {
    Retangulo anzol;
    int score, frame_counter, timer_pontuacao;
    pixel_buffer_start = ADDR_BUFFER1;
    preencher_tela(BLUE);
    *(pixel_ctrl_ptr + 1) = ADDR_BUFFER2;
    wait_for_vsync();
    
    pixel_buffer_start = ADDR_BUFFER2;
    preencher_tela(BLUE);
    int buffer_index = (pixel_buffer_start == ADDR_BUFFER2) ? 1 : 0;
    
    inicializar_jogo_vars(&anzol, &score, &frame_counter, &timer_pontuacao);

    bool rodando = true;
    bool key_left = false, key_right = false, aguardando_soltar = false;
    int velocidade = 4;
    
    while (rodando) {
        // --- LIMPEZA ---
        Retangulo temp_clean_anzol = anzol;
        temp_clean_anzol.x = anzol.old_x[buffer_index];
        temp_clean_anzol.y = anzol.old_y[buffer_index];
        desenhar_player(&temp_clean_anzol, BLUE); 

        for(int i=0; i<MAX_INIMIGOS; i++) {
            if(inimigos[i].old_ativo[buffer_index]) {
                Inimigo temp_clean = inimigos[i];
                temp_clean.x = inimigos[i].old_x[buffer_index];
                temp_clean.y = inimigos[i].old_y[buffer_index];
                desenhar_mina(&temp_clean, BLUE); 
            }
        }
        
        for(int i=0; i<MAX_PEIXES; i++) {
            if(peixes[i].old_ativo[buffer_index]) {
                Peixe temp_clean_peixe = peixes[i];
                temp_clean_peixe.x = peixes[i].old_x[buffer_index];
                temp_clean_peixe.y = peixes[i].old_y[buffer_index];
                desenhar_peixe_sprite(&temp_clean_peixe, BLUE);
            }
        }

        // --- INPUT ---
        int PS2_data = *ps2_ptr;
        int RVALID = PS2_data & 0x8000;
        while (RVALID) {
            unsigned char code = PS2_data & 0xFF;
            if (code == 0xF0) aguardando_soltar = true;
            else if (code != 0xE0) {
                if (aguardando_soltar) {
                    if (code == 0x6B) key_left = false;
                    if (code == 0x74) key_right = false;
                    aguardando_soltar = false;
                } else {
                    if (code == 0x6B) key_left = true;
                    if (code == 0x74) key_right = true;
                }
            }
            PS2_data = *ps2_ptr;
            RVALID = PS2_data & 0x8000;
        }

        if (*key_ptr & 0x01) rodando = false;
        
        // --- LÓGICA ---
        timer_pontuacao++;
        if (timer_pontuacao >= 30) { score++; atualizar_display(score);
        timer_pontuacao = 0; }

        if (key_left) anzol.x -= velocidade;
        if (key_right) anzol.x += velocidade;
        if (anzol.x < 0) anzol.x = 0;
        if (anzol.x + anzol.w >= WIDTH) anzol.x = WIDTH - anzol.w;

        frame_counter++;
        if (frame_counter > 20) { 
            frame_counter = 0;
            // Spawn Inimigos
            for(int i=0; i<MAX_INIMIGOS; i++) if(!inimigos[i].ativo) {
                inimigos[i].x = (rand()%(WIDTH-40))+20;
                inimigos[i].y = HEIGHT+20; 
                inimigos[i].ativo = true; 
                inimigos[i].r = RAIO_INIMIGO; 
                break;
            }
            // Spawn Peixes
            if (rand() % 3 == 0) {
                for(int i=0; i<MAX_PEIXES; i++) if(!peixes[i].ativo) {
                    peixes[i].x = (rand()%(WIDTH-40))+20;
                    peixes[i].y = HEIGHT+20; 
                    peixes[i].w = 16; peixes[i].h = 10; 
                    peixes[i].vx = (rand()%2==0)?2:-2; 
                    peixes[i].ativo = true;
                    
                    int chance = rand() % 100;
                    if (score > 3000 && chance < 5) { 
                        peixes[i].cor = YELLOW; 
                        peixes[i].pontos = 1000; 
                        peixes[i].vx *= 3;
                    }
                    else if (score > 1500 && chance < 15) { 
                        peixes[i].cor = MAGENTA; 
                        peixes[i].pontos = 300; 
                        peixes[i].vx *= 2; 
                    }
                    else if (score > 500 && chance < 40) { 
                        peixes[i].cor = LIME; 
                        peixes[i].pontos = 150; 
                    }
                    else { 
                        peixes[i].cor = WHITE; 
                        peixes[i].pontos = 50; 
                    }
                    break;
                }
            }
        }

        // Movimento Inimigos
        for(int i=0; i<MAX_INIMIGOS; i++) if(inimigos[i].ativo) {
            inimigos[i].y -= VELOCIDADE_SUBIDA;
            if (verificar_colisao_inimigo(anzol, inimigos[i])) { 
                preencher_tela(RED); 
                wait_for_vsync(); preencher_tela(BLUE); 
                rodando = false; 
            }
            if (inimigos[i].y < -20) inimigos[i].ativo = false;
        }

        // Movimento Peixes
        for(int i=0; i<MAX_PEIXES; i++) if(peixes[i].ativo) {
            peixes[i].y -= VELOCIDADE_SUBIDA;
            peixes[i].x += peixes[i].vx;
            if (peixes[i].x <= 0 || peixes[i].x + peixes[i].w >= WIDTH) peixes[i].vx = -peixes[i].vx;
            
            if (verificar_colisao_peixe(anzol, peixes[i])) { 
                score += peixes[i].pontos; 
                atualizar_display(score); 
                peixes[i].ativo = false;
            }
            if (peixes[i].y < -20) peixes[i].ativo = false;
        }

        // --- DESENHO ---
        desenhar_player(&anzol, 0);
        anzol.old_x[buffer_index] = anzol.x; 
        anzol.old_y[buffer_index] = anzol.y;

        for(int i=0; i<MAX_INIMIGOS; i++) if(inimigos[i].ativo) {
            desenhar_mina(&inimigos[i], 0); 
            inimigos[i].old_x[buffer_index] = inimigos[i].x; 
            inimigos[i].old_y[buffer_index] = inimigos[i].y; 
            inimigos[i].old_ativo[buffer_index] = true;
        } else inimigos[i].old_ativo[buffer_index] = false;

        for(int i=0; i<MAX_PEIXES; i++) if(peixes[i].ativo) {
            desenhar_peixe_sprite(&peixes[i], 0);
            peixes[i].old_x[buffer_index] = peixes[i].x; 
            peixes[i].old_y[buffer_index] = peixes[i].y; 
            peixes[i].old_ativo[buffer_index] = true;
        } else peixes[i].old_ativo[buffer_index] = false;

        // --- SWAP ---
        wait_for_vsync();
        pixel_buffer_start = *(pixel_ctrl_ptr + 1);
        buffer_index = (pixel_buffer_start == ADDR_BUFFER2) ? 1 : 0;
    }
}

// --- MENU ---
GameState executar_menu() {
    preencher_tela(BLACK); 
    wait_for_vsync();
    preencher_tela(BLACK); 
    limpar_texto();

    int opcao = 0; 
    int opcao_antiga = -1; 

    desenhar_texto(32, 10, "== JOGO DE PESCA ==");
    desenhar_texto(37, 20, "INICIAR JOGO");
    desenhar_texto(37, 22, "ARMAZEM");
    desenhar_texto(37, 24, "SAIR");
    desenhar_texto(20, 50, "KEY1: Mudar | KEY0: Selecionar");
    while (1) {
        if (opcao != opcao_antiga) {
            desenhar_texto(35, 20, " ");
            desenhar_texto(35, 22, " "); desenhar_texto(35, 24, " ");
            if (opcao == 0) desenhar_texto(35, 20, ">");
            if (opcao == 1) desenhar_texto(35, 22, ">");
            if (opcao == 2) desenhar_texto(35, 24, ">");
            opcao_antiga = opcao;
        }

        int keys = ler_botoes();
        if (keys & 0x02) { opcao++;
            if (opcao > 2) opcao = 0; }
        if (keys & 0x01) {
            if (opcao == 0) return ESTADO_JOGO;
            if (opcao == 1) return ESTADO_ARMAZEM;
            if (opcao == 2) return ESTADO_SAIR;
        }
    }
}

// --- ARMAZÉM ---
void executar_armazem() {
    preencher_tela(BLACK);
    wait_for_vsync(); preencher_tela(BLACK);
    limpar_texto();
    int selecao = skin_selecionada;
    int selecao_antiga = -1;

    desenhar_bitmap(85, 100, sprite_anzol, 16, 16, WHITE, 0);
    desenhar_bitmap(150, 100, sprite_anzol, 16, 16, YELLOW, 0);
    desenhar_bitmap(215, 100, sprite_anzol, 16, 16, ORANGE, 0);

    desenhar_texto(32, 5, "== ARMAZEM ==");
    desenhar_texto(25, 7, "Escolha o visual do seu anzol:");
    desenhar_texto(12, 22, "PADRAO");
    desenhar_texto(34, 22, "OURO");
    desenhar_texto(54, 22, "NEON");
    desenhar_texto(20, 50, "KEY1: Mudar | KEY0: Confirmar");

    while (1) {
        if (selecao != selecao_antiga) {
            desenhar_texto(12, 20, "   ");
            desenhar_texto(34, 20, "   "); desenhar_texto(54, 20, "   ");
            if (selecao == 0) desenhar_texto(12, 20, "[x]");
            if (selecao == 1) desenhar_texto(34, 20, "[x]");
            if (selecao == 2) desenhar_texto(54, 20, "[x]");
            selecao_antiga = selecao;
        }

        int keys = ler_botoes();
        if (keys & 0x02) { selecao++;
            if (selecao > 2) selecao = 0; }
        if (keys & 0x01) { skin_selecionada = selecao;
            return; }
    }
}

// --- MAIN ---
int main() {
    *(pixel_ctrl_ptr + 1) = ADDR_BUFFER2;
    pixel_buffer_start = ADDR_BUFFER1;
    preencher_tela(BLACK);
    
    GameState estado_atual = ESTADO_MENU;

    while (1) {
        switch (estado_atual) {
            case ESTADO_MENU: estado_atual = executar_menu();
                break;
            case ESTADO_ARMAZEM: executar_armazem(); estado_atual = ESTADO_MENU; break;
            case ESTADO_JOGO: limpar_texto(); executar_jogo(); estado_atual = ESTADO_MENU; break;
            case ESTADO_SAIR:
                limpar_texto(); preencher_tela(BLACK); wait_for_vsync();
                preencher_tela(BLACK);
                desenhar_texto(35, 30, "JOGO ENCERRADO"); return 0;
        }
    }
}