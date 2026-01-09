/**
 * display.c - Server version (no ncurses)
 * 
 * In the server, display is handled via FIFOs to clients.
 * These functions are stubs to satisfy the linker.
 */

#include "display.h"
#include "board.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

// Debug file descriptor
static int debug_fd = -1;

// =============================================================================
// Terminal functions - stubs (server doesn't use ncurses)
// =============================================================================

int terminal_init(void) {
    // No-op in server mode
    return 0;
}

void draw_board(board_t* board, int mode) {
    // No-op in server mode - board is sent via FIFO
    (void)board;
    (void)mode;
}

void draw(char c, int colour_i, int pos_x, int pos_y) {
    // No-op in server mode
    (void)c;
    (void)colour_i;
    (void)pos_x;
    (void)pos_y;
}

void refresh_screen(void) {
    // No-op in server mode
}

char get_input(void) {
    // No-op in server mode - input comes via FIFO
    return '\0';
}

void terminal_cleanup(void) {
    // No-op in server mode
}

// =============================================================================
// Debug logging - uses POSIX I/O
// =============================================================================

void open_debug_file(const char* filename) {
    debug_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

void close_debug_file(void) {
    if (debug_fd >= 0) {
        close(debug_fd);
        debug_fd = -1;
    }
}

void debug(const char* format, ...) {
    if (debug_fd < 0) {
        return;
    }
    
    char buffer[1024];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len > 0) {
        if (write(debug_fd, buffer, (size_t)len) < 0) {
            // Ignore write errors
        }
    }
}

void print_board(board_t* game_board) {
    debug("Board: %dx%d\n", game_board->width, game_board->height);
    
    for (int y = 0; y < game_board->height; y++) {
        char line[256];
        int pos = 0;
        for (int x = 0; x < game_board->width && pos < 255; x++) {
            line[pos++] = game_board->board[y * game_board->width + x].content;
        }
        line[pos] = '\0';
        debug("%s\n", line);
    }
}
