#ifndef GAME_2048_H
#define GAME_2048_H

#include <stdint.h>

#define DIR_UP 0
#define DIR_DOWN 1
#define DIR_LEFT 2
#define DIR_RIGHT 3

extern int board_2048[4][4];
extern int score_2048;
extern int game_over_2048;

void init_2048(void);
void move_2048(int dir);

#endif
