#ifndef DISPLAY_H
#define DISPLAY_H

#include "board.h"

#define DRAW_GAME_OVER 0
#define DRAW_WIN 1
#define DRAW_MENU 2

// Terminal functions (stubs in server mode - no ncurses)
int terminal_init(void);
void draw_board(board_t* board, int mode);
void draw(char c, int colour_i, int pos_x, int pos_y);
void refresh_screen(void);
char get_input(void);
void terminal_cleanup(void);

// Debug logging functions
void open_debug_file(const char* filename);
void close_debug_file(void);
void debug(const char* format, ...);
void print_board(board_t* game_board);

#endif
