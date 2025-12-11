#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h> 
#include <time.h>   
#include <termios.h>
#include <math.h> 
#include <linux/input.h> 

#define KEYBOARD_DEVICE "/dev/input/event0" 

#define LW_BRIDGE_BASE      0xFF200000
#define LW_BRIDGE_SPAN      0x00005000 
#define SDRAM_BASE          0xC0000000
#define SDRAM_SPAN          0x0A000000 

// Offsets
#define OFFSET_PIXEL_CTRL   0x00003020 
#define OFFSET_HEX3_HEX0    0x00000020 
#define OFFSET_KEY          0x00000050 
#define OFFSET_BUFFER2      0x00000000 
#define OFFSET_BUFFER1      0x08000000 
#define OFFSET_CHAR_BUF     0x09000000 

// Configurações do Jogo
#define WIDTH  320
#define HEIGHT 240
#define STRIDE_REAL_MEMORIA 512 

#define BLUE    0x001F
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define LIME    0x07E0
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define ORANGE  0xFD20
#define GRAY    0x8410 

// Configs Gameplay
#define MAX_INIMIGOS 6
#define MAX_PEIXES 5
#define RAIO_INIMIGO 7
#define VELOCIDADE_ANZOL 6 
#define VELOCIDADE_INICIAL_SUBIDA 2
#define VELOCIDADE_MAXIMA_SUBIDA 8  
#define FATOR_DIFICULDADE 500       

typedef enum { 
    ESTADO_MENU, 
    ESTADO_ARMAZEM, 
    ESTADO_JOGO, 
    ESTADO_SAIR 
} GameState; 

const uint16_t sprite_anzol[16] = {
    0x03C0, 0x03C0, 0x03C0, 0x0180,
    0x0180, 0x0180, 0x0180, 0x0180,
    0x0180, 0x0180, 0x6180, 0x7180,
    0x7980, 0x3F80, 0x1F00, 0x0E00
};

const uint16_t sprite_peixe[10] = {
    0x0000,
    0x0030,
    0x0078,
    0x03FC,
    0x0FFE,
    0x3FFF,
    0x3FFF,
    0x0FFE,
    0x03FC,
    0x0110

};

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
    int old_vx[2]; 
} Peixe;

// Variáveis Globais de Hardware
volatile int * pixel_ctrl_ptr;
volatile int * hex3_hex0_ptr;
volatile int * key_ptr;
volatile char * char_buf_ptr;

void *virtual_base_lw;
void *virtual_base_sdram;
int fd = -1; 
int keyboard_fd = -1;

const int PHY_ADDR_BUFFER1 = 0xC8000000;
const int PHY_ADDR_BUFFER2 = 0xC0000000;

volatile int pixel_buffer_start;
const char seg7_table[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x67};
Inimigo inimigos[MAX_INIMIGOS];
Peixe peixes[MAX_PEIXES];
int skin_selecionada = 0;

struct termios orig_termios;

// funçoes estruturais 

void restaurar_terminal() {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    printf("\033[?25h"); 
    if (keyboard_fd != -1) close(keyboard_fd); 
}

void configurar_terminal_linux() {
    struct termios new_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(restaurar_terminal); 

    printf("\033[?25l"); 
    fflush(stdout);

    new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
}

void abrir_teclado_usb() {
    keyboard_fd = open(KEYBOARD_DEVICE, O_RDONLY | O_NONBLOCK);
}

void abre_memoria(){
    fd = open("/dev/mem", O_RDWR | O_SYNC);
}

void map_perifericos() {
    virtual_base_lw = mmap(NULL, LW_BRIDGE_SPAN, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, LW_BRIDGE_BASE);
    if (virtual_base_lw == MAP_FAILED) exit(EXIT_FAILURE);

    pixel_ctrl_ptr = (volatile int *) (virtual_base_lw + OFFSET_PIXEL_CTRL);
    hex3_hex0_ptr  = (volatile int *) (virtual_base_lw + OFFSET_HEX3_HEX0);
    key_ptr        = (volatile int *) (virtual_base_lw + OFFSET_KEY);

    virtual_base_sdram = mmap(NULL, SDRAM_SPAN, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, SDRAM_BASE);
    if (virtual_base_sdram == MAP_FAILED) exit(EXIT_FAILURE);
    char_buf_ptr = (volatile char *) (virtual_base_sdram + OFFSET_CHAR_BUF);
}

void libera_memoria() {
    if (virtual_base_lw != MAP_FAILED) munmap(virtual_base_lw, LW_BRIDGE_SPAN);
    if (virtual_base_sdram != MAP_FAILED) munmap(virtual_base_sdram, SDRAM_SPAN);
    if (fd != -1) close(fd);
}

// funções do video

void wait_for_vsync() {
    volatile int * status_ptr = (volatile int *) ((char*)pixel_ctrl_ptr + 0xC);
    *pixel_ctrl_ptr = 1;
    while ((*status_ptr & 0x01) != 0);
}

volatile short* calcula_endereco(int x, int y) {
    int current_back_buffer_phy = *(pixel_ctrl_ptr + 1); 
    if (current_back_buffer_phy == PHY_ADDR_BUFFER1) {
        return (volatile short *)(virtual_base_sdram + OFFSET_BUFFER1 + (y << 10) + (x << 1));
    } else {
        return (volatile short *)(virtual_base_sdram + OFFSET_BUFFER2 + (y << 10) + (x << 1));
    }
}

void plota_pixel(int x, int y, uint16_t color) {
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
        volatile short *pixel_addr = calcula_endereco(x, y);
        *pixel_addr = color;
    }
}

void preencher_tela(uint16_t cor) {
    for (int y = 0; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++)
            plota_pixel(x, y, cor);
}

void colorir_telas(uint16_t cor) {
    volatile short *buf1 = (volatile short *)(virtual_base_sdram + OFFSET_BUFFER1);
    volatile short *buf2 = (volatile short *)(virtual_base_sdram + OFFSET_BUFFER2);

    for (int y = 0; y < HEIGHT; y++) {

        for (int x = 0; x < STRIDE_REAL_MEMORIA; x++) {
            int offset = (y << 10) + (x << 1); 
            *(volatile short *)((char*)buf1 + offset) = cor;
            *(volatile short *)((char*)buf2 + offset) = cor;
        }
    }
}

void limpar_texto() {
    for(int y=0; y<60; y++) {
        for(int x=0; x<80; x++) {
            volatile char * c = (volatile char *) (char_buf_ptr + (y<<7) + x);
            *c = 0;
        }
    }
}

void desenhar_texto(int x, int y, char * text) {
    volatile char * character_buffer = (volatile char *) (char_buf_ptr + (y<<7) + x);
    while (*text) {
        *character_buffer = *text;
        character_buffer++;
        text++;
    }
}

void plotar_sprite(int x, int y, const uint16_t *bitmap, int w, int h, uint16_t cor, int flip_x) {
    for (int i = 0; i < h; i++) {
        uint16_t linha = bitmap[i];
        for (int j = 0; j < w; j++) {
            int col_index = flip_x ? j : (w - 1 - j);
            if ((linha >> col_index) & 0x01) {
                plota_pixel(x + j, y + i, cor);
            }
        }
    }
}

void linha_anzol(int x, int y_bottom, uint16_t cor) {
    for (int y = 0; y < y_bottom; y++) {
        plota_pixel(x, y, cor);
    }
}

void atualizar_estado_teclado(bool *up, bool *down, bool *left, bool *right, bool *sair, bool *enter, bool reset) {
    struct input_event ev;
    static bool s_up = false, s_down = false, s_left = false, s_right = false;

    // (tirando bug do reset)
    if (reset) {
        s_up = false; s_down = false; s_left = false; s_right = false;
        while (read(keyboard_fd, &ev, sizeof(ev)) > 0);
        return;
    }

    while (read(keyboard_fd, &ev, sizeof(ev)) > 0) {
        if (ev.type == EV_KEY) {
            bool pressionado = (ev.value == 1 || ev.value == 2);
            
            if (ev.code == KEY_UP || ev.code == KEY_W) s_up = pressionado;
            if (ev.code == KEY_DOWN || ev.code == KEY_S) s_down = pressionado;
            if (ev.code == KEY_LEFT || ev.code == KEY_A) s_left = pressionado;
            if (ev.code == KEY_RIGHT || ev.code == KEY_D) s_right = pressionado;
            
            if (ev.code == KEY_Q || ev.code == KEY_ESC) *sair = true;
            if (ev.code == KEY_ENTER) *enter = pressionado;
        }
    }

    *up = s_up; *down = s_down; *left = s_left; *right = s_right;
}

int le_inpt_menu() {
    struct input_event ev;
    int cmd = 0;
    if (*key_ptr & 0x01) cmd |= 0x01; 
    if (*key_ptr & 0x02) cmd |= 0x02; 
    while (read(keyboard_fd, &ev, sizeof(ev)) > 0) {
        if (ev.type == EV_KEY && ev.value == 1) { 
            if (ev.code == KEY_UP || ev.code == KEY_W || ev.code == KEY_LEFT) cmd |= 0x02; 
            if (ev.code == KEY_DOWN || ev.code == KEY_S || ev.code == KEY_RIGHT) cmd |= 0x02; 
            if (ev.code == KEY_ENTER || ev.code == KEY_SPACE) cmd |= 0x01; 
            if (ev.code == KEY_1) cmd |= 0x01;
            if (ev.code == KEY_2) cmd |= 0x02;
        }
    }
    if (cmd != 0) usleep(150000); 
    return cmd;
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


void desenhar_player(Retangulo *p, uint16_t cor_override) {
    uint16_t cor_final = (cor_override == BLUE) ? BLUE : p->cor;

    plotar_sprite(p->x, p->y, sprite_anzol, 16, 16, cor_final, 0);

    uint16_t cor_linha = (cor_override == BLUE) ? BLUE : WHITE;
    linha_anzol(p->x + 8, p->y, cor_linha);
}

void desenhar_peixe_sprite(Peixe *p, uint16_t cor_override) {
    uint16_t cor_final = (cor_override == BLUE) ? BLUE : p->cor;
    int direcao = (p->vx > 0) ? 1 : 0;
    plotar_sprite(p->x, p->y, sprite_peixe, 16, 10, cor_final, direcao);
}

void desenhar_mina(Inimigo *ini, uint16_t cor_override) {
    uint16_t cor_final = (cor_override == BLUE) ? BLUE : RED;
    for (int dy = -ini->r; dy <= ini->r; dy++) {
        for (int dx = -ini->r; dx <= ini->r; dx++) {
            if (dx * dx + dy * dy <= ini->r * ini->r) {
                plota_pixel(ini->x + dx, ini->y + dy, cor_final);
            }
        }
    }
}

void limpa_anzol(Retangulo *anzol, int buf) {
    Retangulo tmp = *anzol;
    tmp.x = anzol->old_x[buf];
    tmp.y = anzol->old_y[buf];
    desenhar_player(&tmp, BLUE);
}

void limpar_inimigos_frame(int buf) {
    for (int i = 0; i < MAX_INIMIGOS; i++) {
        if (inimigos[i].old_ativo[buf]) {
            Inimigo tmp = inimigos[i];
            tmp.x = inimigos[i].old_x[buf];
            tmp.y = inimigos[i].old_y[buf];
            desenhar_mina(&tmp, BLUE);
        }
    }
}

void limpar_peixes_frame(int buf) {
    for (int i = 0; i < MAX_PEIXES; i++) {
        if (peixes[i].old_ativo[buf]) {
            Peixe tmp = peixes[i];
            tmp.x = peixes[i].old_x[buf];
            tmp.y = peixes[i].old_y[buf];
            tmp.vx = peixes[i].old_vx[buf]; 
            desenhar_peixe_sprite(&tmp, BLUE);
        }
    }
}

void inicializar_inimigo(Inimigo *ini) {
    ini->x = (rand() % (WIDTH - 40)) + 20;
    ini->y = HEIGHT + 20;
    ini->ativo = true;
    ini->r = RAIO_INIMIGO;
}

void mover_inimigo(Inimigo *ini, int velocidade_atual) {
    ini->y -= velocidade_atual;
}

void tenta_spawnar_inimigo(void) {
    for (int i = 0; i < MAX_INIMIGOS; i++) {
        if (!inimigos[i].ativo) {
            inicializar_inimigo(&inimigos[i]);
            break;
        }
    }
}

void mover_peixe(Peixe *p, int velocidade_atual) {
    p->y -= velocidade_atual;
    p->x += p->vx;
    if (p->x <= 0) {
        p->x = 0; 
        if (p->vx < 0) p->vx = -p->vx; 
    }
    else if (p->x + p->w >= WIDTH) {
        p->x = WIDTH - p->w; 
        if (p->vx > 0) p->vx = -p->vx;
    }
}

void tenta_spawnar_peixe(int score) {
    for (int i = 0; i < MAX_PEIXES; i++) {
        if (!peixes[i].ativo) {
            peixes[i].x = (rand() % (WIDTH - 40)) + 20;
            peixes[i].y = HEIGHT + 20;
            peixes[i].w = 16; peixes[i].h = 10;
            peixes[i].vx = (rand() % 2 == 0) ? 2 : -2;
            peixes[i].ativo = true;

            int chance = rand() % 100;
            if (score > 3000 && chance < 5) {
                peixes[i].cor = YELLOW;
                peixes[i].pontos = 1000;
                peixes[i].vx *= 3;
            } else if (score > 1500 && chance < 15) {
                peixes[i].cor = MAGENTA;
                peixes[i].pontos = 300;
                peixes[i].vx *= 2;
            } else if (score > 500 && chance < 40) {
                peixes[i].cor = LIME;
                peixes[i].pontos = 150;
            } else {
                peixes[i].cor = WHITE;
                peixes[i].pontos = 50;
            }
            break;
        }
    }
}

void atualizar_inimigos(int velocidade_atual) {
    for (int i = 0; i < MAX_INIMIGOS; i++) {
        if (inimigos[i].ativo) {
            mover_inimigo(&inimigos[i], velocidade_atual);
        }
    }
}

void atualizar_peixes(int velocidade_atual) {
    for (int i = 0; i < MAX_PEIXES; i++) {
        if (peixes[i].ativo) {
            mover_peixe(&peixes[i], velocidade_atual);
        }
    }
}

void desenhar_estado(Retangulo *anzol) {
    desenhar_player(anzol, LIME);
    for (int i = 0; i < MAX_INIMIGOS; i++) {
        if (inimigos[i].ativo) {
            desenhar_mina(&inimigos[i], RED);
        }
    }
    for (int i = 0; i < MAX_PEIXES; i++) {
        if (peixes[i].ativo) {
            desenhar_peixe_sprite(&peixes[i], peixes[i].cor);
        }
    }
}

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
        peixes[i].old_vx[0] = peixes[i].old_vx[1] = 0; 
    }
    *score = 0; *frame_counter = 0;
    *timer_pontuacao = 0;
    atualizar_display(0);
}

void executar_jogo() {
    Retangulo anzol;
    int score, frame_counter, timer_pontuacao;
    int velocidade_vertical_atual = VELOCIDADE_INICIAL_SUBIDA;
    colorir_telas(BLUE);
    *(pixel_ctrl_ptr + 1) = PHY_ADDR_BUFFER1; 
    wait_for_vsync(); 
    *(pixel_ctrl_ptr + 1) = PHY_ADDR_BUFFER2;
    preencher_tela(BLUE); 
    wait_for_vsync(); 
    *(pixel_ctrl_ptr + 1) = PHY_ADDR_BUFFER1;
    preencher_tela(BLUE);

    int buffer_index = (*(pixel_ctrl_ptr + 1) == PHY_ADDR_BUFFER2) ? 1 : 0;
    inicializar_jogo_vars(&anzol, &score, &frame_counter, &timer_pontuacao);
    bool rodando = true;
    bool k_up = false, k_down = false, k_left = false, k_right = false, sair_cmd = false, k_enter = false;
    const int velocidade_player = VELOCIDADE_ANZOL;

    atualizar_estado_teclado(NULL, NULL, NULL, NULL, NULL, NULL, true);

    while (rodando) {
        int current_back = *(pixel_ctrl_ptr + 1);
        buffer_index = (current_back == PHY_ADDR_BUFFER2) ? 1 : 0;

        velocidade_vertical_atual = VELOCIDADE_INICIAL_SUBIDA + (score / FATOR_DIFICULDADE);
        if (velocidade_vertical_atual > VELOCIDADE_MAXIMA_SUBIDA) {
            velocidade_vertical_atual = VELOCIDADE_MAXIMA_SUBIDA;
        }

        limpa_anzol(&anzol, buffer_index);
        limpar_inimigos_frame(buffer_index);
        limpar_peixes_frame(buffer_index); 
        atualizar_estado_teclado(&k_up, &k_down, &k_left, &k_right, &sair_cmd, &k_enter, false);
        
        if (sair_cmd || (*key_ptr & 0x01)) rodando = false;

        timer_pontuacao++;
        if (timer_pontuacao >= 30) {
            score++; 
            atualizar_display(score);
            timer_pontuacao = 0;
        }

        if (k_left)  anzol.x -= velocidade_player;
        if (k_right) anzol.x += velocidade_player;
        if (k_up)    anzol.y -= velocidade_player; 
        if (k_down)  anzol.y += velocidade_player; 

        if (anzol.x < 0) anzol.x = 0;
        if (anzol.x + anzol.w >= WIDTH) anzol.x = WIDTH - anzol.w;
        if (anzol.y < 0) anzol.y = 0;
        if (anzol.y + anzol.h >= HEIGHT) anzol.y = HEIGHT - anzol.h;

        frame_counter++;
        int delay_spawn = 20 - (velocidade_vertical_atual); 
        if (delay_spawn < 5) delay_spawn = 5;

        if (frame_counter > delay_spawn) {
            frame_counter = 0;
            tenta_spawnar_inimigo();
            if (rand() % 3 == 0) {
                tenta_spawnar_peixe(score);
            }
        }

        atualizar_inimigos(velocidade_vertical_atual);
        for (int i = 0; i < MAX_INIMIGOS; i++) {
            if (inimigos[i].ativo && verificar_colisao_inimigo(anzol, inimigos[i])) {
                preencher_tela(RED);
                wait_for_vsync();
                preencher_tela(BLUE);
                rodando = false;
            }
            if (inimigos[i].ativo && inimigos[i].y < -20) inimigos[i].ativo = false;
        }

        atualizar_peixes(velocidade_vertical_atual);
        for (int i = 0; i < MAX_PEIXES; i++) {
            if (peixes[i].ativo && verificar_colisao_peixe(anzol, peixes[i])) {
                score += peixes[i].pontos;
                atualizar_display(score);
                peixes[i].ativo = false;
            }
            if (peixes[i].ativo && peixes[i].y < -20) peixes[i].ativo = false;
        }

        desenhar_estado(&anzol);
        anzol.old_x[buffer_index] = anzol.x;
        anzol.old_y[buffer_index] = anzol.y;

        for (int i = 0; i < MAX_INIMIGOS; i++) {
            inimigos[i].old_x[buffer_index] = inimigos[i].x;
            inimigos[i].old_y[buffer_index] = inimigos[i].y;
            inimigos[i].old_ativo[buffer_index] = inimigos[i].ativo;
        }
        for (int i = 0; i < MAX_PEIXES; i++) {
            peixes[i].old_x[buffer_index] = peixes[i].x;
            peixes[i].old_y[buffer_index] = peixes[i].y;
            peixes[i].old_ativo[buffer_index] = peixes[i].ativo;
            peixes[i].old_vx[buffer_index] = peixes[i].vx; 
        }

        wait_for_vsync();
        
        if (current_back == PHY_ADDR_BUFFER1)
            *(pixel_ctrl_ptr + 1) = PHY_ADDR_BUFFER2;
        else
            *(pixel_ctrl_ptr + 1) = PHY_ADDR_BUFFER1;
    }
}

GameState executar_menu() {
    colorir_telas(BLACK); 
    preencher_tela(BLACK);
    wait_for_vsync();
    preencher_tela(BLACK);
    limpar_texto();

    int opcao = 0;
    int opcao_antiga = -1;

    desenhar_texto(32, 10, "Caiu na Vila o Peixe Fuzila");
    desenhar_texto(37, 20, "INICIAR JOGO");
    desenhar_texto(37, 22, "ARMAZEM");
    desenhar_texto(37, 24, "SAIR");
    desenhar_texto(20, 50, "KEY1/2 ou ENTER: Confirmar");
    desenhar_texto(20, 52, "Setas/WASD: Selecionar");
    
    // delay para evitar "clique duplo" se veio de outro estado
    usleep(200000); 

    while (1) {
        if (opcao != opcao_antiga) {
            desenhar_texto(35, 20, " ");
            desenhar_texto(35, 22, " "); desenhar_texto(35, 24, " ");
            if (opcao == 0) desenhar_texto(35, 20, ">");
            if (opcao == 1) desenhar_texto(35, 22, ">");
            if (opcao == 2) desenhar_texto(35, 24, ">");
            opcao_antiga = opcao;
        }

        int keys = le_inpt_menu();
        
        if (keys & 0x02) { opcao++; 
            if (opcao > 2) opcao = 0; }
        if (keys & 0x01) { 
            if (opcao == 0) return ESTADO_JOGO;
            if (opcao == 1) return ESTADO_ARMAZEM;
            if (opcao == 2) return ESTADO_SAIR;
        }
    }
}

void executar_armazem() {
    colorir_telas(BLACK); 
    preencher_tela(BLACK);
    wait_for_vsync(); preencher_tela(BLACK);
    limpar_texto();
    int selecao = skin_selecionada;
    int selecao_antiga = -1;

    plotar_sprite(85, 100, sprite_anzol, 16, 16, WHITE, 0);
    plotar_sprite(150, 100, sprite_anzol, 16, 16, YELLOW, 0);
    plotar_sprite(215, 100, sprite_anzol, 16, 16, ORANGE, 0);

    desenhar_texto(32, 5, "== ARMAZEM ==");
    desenhar_texto(25, 7, "Escolha o visual do seu anzol:");
    desenhar_texto(12, 22, "PADRAO");
    desenhar_texto(34, 22, "OURO");
    desenhar_texto(54, 22, "NEON");
    desenhar_texto(20, 50, "ENTER: Selecionar | SETAS: Mover");

    usleep(200000); 

    while (1) {
        if (selecao != selecao_antiga) {
            desenhar_texto(12, 20, "   ");
            desenhar_texto(34, 20, "   "); desenhar_texto(54, 20, "   ");
            if (selecao == 0) desenhar_texto(12, 20, "[x]");
            if (selecao == 1) desenhar_texto(34, 20, "[x]");
            if (selecao == 2) desenhar_texto(54, 20, "[x]");
            selecao_antiga = selecao;
        }

        int keys = le_inpt_menu();
        if (keys & 0x02) { selecao++;
            if (selecao > 2) selecao = 0; }
        if (keys & 0x01) { skin_selecionada = selecao;
            return; }
    }
}

int main() {
    srand(time(NULL)); 
    abre_memoria();
    map_perifericos();
    
    abrir_teclado_usb();
    configurar_terminal_linux();

    *(pixel_ctrl_ptr + 1) = PHY_ADDR_BUFFER2;
    wait_for_vsync();
    pixel_buffer_start = (int)virtual_base_sdram + OFFSET_BUFFER1; 
    
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
                desenhar_texto(35, 30, "JOGO ENCERRADO"); 
                libera_memoria();
                if (keyboard_fd != -1) close(keyboard_fd);
                return 0;
        }
    }
}