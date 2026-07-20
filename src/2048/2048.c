#include "2048.h"
#include "../libc/stdlib.h"

int board_2048[4][4];
int score_2048;
int game_over_2048;

static void spawn_tile(void) {
    int empty_spots[16][2];
    int empty_count = 0;
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (board_2048[y][x] == 0) {
                empty_spots[empty_count][0] = x;
                empty_spots[empty_count][1] = y;
                empty_count++;
            }
        }
    }
    if (empty_count == 0) {
        return;
    }
    int rand_index = rand() % empty_count;
    int x = empty_spots[rand_index][0];
    int y = empty_spots[rand_index][1];
    board_2048[y][x] = (rand() % 10 < 9) ? 2 : 4;
}

static int check_game_over(void) {
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (board_2048[y][x] == 0) return 0;
            if (x < 3 && board_2048[y][x] == board_2048[y][x + 1]) return 0;
            if (y < 3 && board_2048[y][x] == board_2048[y + 1][x]) return 0;
        }
    }
    return 1;
}

void init_2048(void) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            board_2048[i][j] = 0;
        }
    }
    score_2048 = 0;
    game_over_2048 = 0;
    spawn_tile();
    spawn_tile();
}

static int slide_array(int array[4]) {
    int moved = 0;
    int merged[4] = {0, 0, 0, 0};
    int j = 0;
    for (int i = 0; i < 4; i++) {
        if (array[i] != 0) {
            if (j > 0 && array[i] == array[j - 1] && !merged[j - 1]) {
                array[j - 1] *= 2;
                score_2048 += array[j - 1];
                merged[j - 1] = 1;
                array[i] = 0;
                moved = 1;
            } else {
                if (i != j) {
                    array[j] = array[i];
                    array[i] = 0;
                    moved = 1;
                }
                j++;
            }
        }
    }
    return moved;
}

void move_2048(int dir) {
    if (game_over_2048) {
        init_2048();
        return;
    }
    
    int moved = 0;
    int temp[4];

    if (dir == DIR_LEFT || dir == DIR_RIGHT) {
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                temp[x] = board_2048[y][dir == DIR_LEFT ? x : (3 - x)];
            }
            if (slide_array(temp)) moved = 1;
            for (int x = 0; x < 4; x++) {
                board_2048[y][dir == DIR_LEFT ? x : (3 - x)] = temp[x];
            }
        }
    } else if (dir == DIR_UP || dir == DIR_DOWN) {
        for (int x = 0; x < 4; x++) {
            for (int y = 0; y < 4; y++) {
                temp[y] = board_2048[dir == DIR_UP ? y : (3 - y)][x];
            }
            if (slide_array(temp)) moved = 1;
            for (int y = 0; y < 4; y++) {
                board_2048[dir == DIR_UP ? y : (3 - y)][x] = temp[y];
            }
        }
    }

    if (moved) {
        spawn_tile();
        if (check_game_over()) {
            game_over_2048 = 1;
        }
    }
}
