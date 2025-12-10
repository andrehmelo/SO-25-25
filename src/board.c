#include "board.h"
#include "parser.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>

FILE * debugfile;

// Helper private function to find and kill pacman at specific position
static int find_and_kill_pacman(board_t* board, int new_x, int new_y) {
    for (int p = 0; p < board->n_pacmans; p++) {
        pacman_t* pac = &board->pacmans[p];
        if (pac->pos_x == new_x && pac->pos_y == new_y && pac->alive) {
            pac->alive = 0;
            kill_pacman(board, p);
            return DEAD_PACMAN;
        }
    }
    return VALID_MOVE;
}

// Helper private function for getting board position index
static inline int get_board_index(board_t* board, int x, int y) {
    return y * board->width + x;
}

// Helper private function for checking valid position
static inline int is_valid_position(board_t* board, int x, int y) {
    return (x >= 0 && x < board->width) && (y >= 0 && y < board->height); // Inside of the board boundaries
}

void sleep_ms(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

int move_pacman(board_t* board, int pacman_index, command_t* command) {
    if (pacman_index < 0 || !board->pacmans[pacman_index].alive) {
        return DEAD_PACMAN; // Invalid or dead pacman
    }

    pacman_t* pac = &board->pacmans[pacman_index];
    int new_x = pac->pos_x;
    int new_y = pac->pos_y;

    // check passo
    if (pac->waiting > 0) {
        pac->waiting -= 1;
        return VALID_MOVE;        
    }
    pac->waiting = pac->passo;

    char direction = command->command;

    if (direction == 'R') {
        char directions[] = {'W', 'S', 'A', 'D'};
        direction = directions[rand() % 4];
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
            new_y--;
            break;
        case 'S': // Down
            new_y++;
            break;
        case 'A': // Left
            new_x--;
            break;
        case 'D': // Right
            new_x++;
            break;
        case 'T': // Wait
            if (command->turns_left == 1) {
                pac->current_move += 1; // move on
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        default:
            return INVALID_MOVE; // Invalid direction
    }

    // Logic for the WASD movement
    pac->current_move+=1;

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, pac->pos_x, pac->pos_y);
    char target_content = board->board[new_index].content;

    if (board->board[new_index].has_portal) {
        board->board[old_index].content = ' ';
        board->board[new_index].content = 'P';
        return REACHED_PORTAL;
    }

    // Check for walls
    if (target_content == 'W') {
        return INVALID_MOVE;
    }

    // Check for ghosts
    if (target_content == 'M') {
        kill_pacman(board, pacman_index);
        return DEAD_PACMAN;
    }

    // Collect points
    if (board->board[new_index].has_dot) {
        pac->points++;
        board->board[new_index].has_dot = 0;
    }

    board->board[old_index].content = ' ';
    pac->pos_x = new_x;
    pac->pos_y = new_y;
    board->board[new_index].content = 'P';

    return VALID_MOVE;
}

// Helper private function for charged ghost movement in one direction
static int move_ghost_charged_direction(board_t* board, ghost_t* ghost, char direction, int* new_x, int* new_y) {
    int x = ghost->pos_x;
    int y = ghost->pos_y;
    *new_x = x;
    *new_y = y;
    
    switch (direction) {
        case 'W': // Up
            if (y == 0) return INVALID_MOVE;
            *new_y = 0; // In case there is no colision
            for (int i = y - 1; i >= 0; i--) {
                char target_content = board->board[get_board_index(board, x, i)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_y = i + 1; // stop before colision
                    return VALID_MOVE;
                }
                else if (target_content == 'P') {
                    *new_y = i;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'S': // Down
            if (y == board->height - 1) return INVALID_MOVE;
            *new_y = board->height - 1; // In case there is no colision
            for (int i = y + 1; i < board->height; i++) {
                char target_content = board->board[get_board_index(board, x, i)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_y = i - 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_y = i;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'A': // Left
            if (x == 0) return INVALID_MOVE;
            *new_x = 0; // In case there is no colision
            for (int j = x - 1; j >= 0; j--) {
                char target_content = board->board[get_board_index(board, j, y)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_x = j + 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_x = j;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'D': // Right
            if (x == board->width - 1) return INVALID_MOVE;
            *new_x = board->width - 1; // In case there is no colision
            for (int j = x + 1; j < board->width; j++) {
                char target_content = board->board[get_board_index(board, j, y)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_x = j - 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_x = j;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;
        default:
            debug("DEFAULT CHARGED MOVE - direction = %c\n", direction);
            return INVALID_MOVE;
    }
    return VALID_MOVE;
}   

int move_ghost_charged(board_t* board, int ghost_index, char direction) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int x = ghost->pos_x;
    int y = ghost->pos_y;
    int new_x = x;
    int new_y = y;

    ghost->charged = 0; //uncharge
    int result = move_ghost_charged_direction(board, ghost, direction, &new_x, &new_y);
    if (result == INVALID_MOVE) {
        debug("DEFAULT CHARGED MOVE - direction = %c\n", direction);
        return INVALID_MOVE;
    }

    // Get board indices
    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    int new_index = get_board_index(board, new_x, new_y);

    // Update board - clear old position (restore what was there)
    board->board[old_index].content = ' '; // Or restore the dot if ghost was on one
    // Update ghost position
    ghost->pos_x = new_x;
    ghost->pos_y = new_y;
    // Update board - set new position
    board->board[new_index].content = 'M';
    return result;
}

int move_ghost(board_t* board, int ghost_index, command_t* command) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int new_x = ghost->pos_x;
    int new_y = ghost->pos_y;

    // check passo
    if (ghost->waiting > 0) {
        ghost->waiting -= 1;
        return VALID_MOVE;
    }
    ghost->waiting = ghost->passo;

    char direction = command->command;
    
    if (direction == 'R') {
        char directions[] = {'W', 'S', 'A', 'D'};
        direction = directions[rand() % 4];
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
            new_y--;
            break;
        case 'S': // Down
            new_y++;
            break;
        case 'A': // Left
            new_x--;
            break;
        case 'D': // Right
            new_x++;
            break;
        case 'C': // Charge
            ghost->current_move += 1;
            ghost->charged = 1;
            return VALID_MOVE;
        case 'T': // Wait
            if (command->turns_left == 1) {
                ghost->current_move += 1; // move on
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        default:
            return INVALID_MOVE; // Invalid direction
    }

    // Logic for the WASD movement
    ghost->current_move++;
    if (ghost->charged)
        return move_ghost_charged(board, ghost_index, direction);

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    // Check board position
    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    char target_content = board->board[new_index].content;

    // Check for walls and ghosts
    if (target_content == 'W' || target_content == 'M') {
        return INVALID_MOVE;
    }

    int result = VALID_MOVE;
    // Check for pacman
    if (target_content == 'P') {
        result = find_and_kill_pacman(board, new_x, new_y);
    }

    // Update board - clear old position (restore what was there)
    board->board[old_index].content = ' '; // Or restore the dot if ghost was on one

    // Update ghost position
    ghost->pos_x = new_x;
    ghost->pos_y = new_y;

    // Update board - set new position
    board->board[new_index].content = 'M';
    return result;
}

void kill_pacman(board_t* board, int pacman_index) {
    debug("Killing %d pacman\n\n", pacman_index);
    pacman_t* pac = &board->pacmans[pacman_index];
    int index = pac->pos_y * board->width + pac->pos_x;

    // Remove pacman from the board
    board->board[index].content = ' ';

    // Mark pacman as dead
    pac->alive = 0;
}

void unload_level(board_t * board) {
    // Safe cleanup - check for NULL pointers
    if (board->board) {
        free(board->board);
        board->board = NULL;
    }
    if (board->pacmans) {
        free(board->pacmans);
        board->pacmans = NULL;
    }
    if (board->ghosts) {
        free(board->ghosts);
        board->ghosts = NULL;
    }
}

// =============================================================================
// File-based Loading Functions (Exercise 1)
// =============================================================================

// Helper: cleanup is now just an alias to unload_level (both are NULL-safe)
#define cleanup_board unload_level

// Helper: load behavior from .m or .p file (DRY - Don't Repeat Yourself)
static int load_agent_behavior(const char* dir_path, const char* filename,
                               int* passo, int* row, int* col,
                               command_t* moves, int* n_moves) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, filename);
    
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    
    char commands[MAX_MOVES];
    int turns[MAX_MOVES];
    int n_cmds;
    
    if (parse_behavior_file(fd, passo, row, col, commands, turns, MAX_MOVES, &n_cmds) < 0) {
        close(fd);
        return -1;
    }
    close(fd);
    
    *n_moves = n_cmds;
    for (int i = 0; i < n_cmds; i++) {
        moves[i].command = commands[i];
        moves[i].turns = turns[i];
        moves[i].turns_left = turns[i];
    }
    
    return 0;
}

int load_ghost_from_file(board_t* board, const char* dir_path, const char* filename, int ghost_index) {
    int passo, row, col, n_moves;
    command_t moves[MAX_MOVES];
    
    if (load_agent_behavior(dir_path, filename, &passo, &row, &col, moves, &n_moves) < 0) {
        return -1;
    }
    
    ghost_t* ghost = &board->ghosts[ghost_index];
    ghost->pos_x = col;
    ghost->pos_y = row;
    ghost->passo = passo;
    ghost->waiting = passo;
    ghost->current_move = 0;
    ghost->n_moves = n_moves;
    
    for (int i = 0; i < n_moves; i++) {
        ghost->moves[i] = moves[i];
    }
    
    // Place ghost on board
    int index = row * board->width + col;
    board->board[index].content = 'M';
    
    return 0;
}

int load_pacman_from_file(board_t* board, const char* dir_path, const char* filename, int points) {
    int passo, row, col, n_moves;
    command_t moves[MAX_MOVES];
    
    if (load_agent_behavior(dir_path, filename, &passo, &row, &col, moves, &n_moves) < 0) {
        return -1;
    }
    
    // Only one pacman, always index 0
    pacman_t* pac = &board->pacmans[0];
    pac->pos_x = col;
    pac->pos_y = row;
    pac->passo = passo;
    pac->waiting = passo;
    pac->alive = 1;
    pac->points = points;
    pac->current_move = 0;
    pac->n_moves = n_moves;
    
    for (int i = 0; i < n_moves; i++) {
        pac->moves[i] = moves[i];
    }
    
    // Place pacman on board
    int index = row * board->width + col;
    board->board[index].content = 'P';
    
    return 0;
}

int load_level_from_file(board_t* board, const char* dir_path, const char* level_file, int accumulated_points) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, level_file);
    
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    
    int rows, cols, tempo;
    char pac_file[256] = {0};
    char mon_files[MAX_GHOSTS][256];
    int n_mons;
    char board_data[4096];  // Should be enough for reasonable boards
    
    if (parse_level_file(fd, &rows, &cols, &tempo, pac_file, mon_files, MAX_GHOSTS, &n_mons, board_data, sizeof(board_data)) < 0) {
        close(fd);
        return -1;
    }
    close(fd);
    
    // Setup board dimensions
    board->height = rows;
    board->width = cols;
    board->tempo = tempo;
    board->n_ghosts = n_mons;
    board->n_pacmans = 1;
    
    // Copy level name from level file (without extension)
    strncpy(board->level_name, level_file, sizeof(board->level_name) - 1);
    char* dot = strrchr(board->level_name, '.');
    if (dot) *dot = '\0';
    
    // Allocate memory
    board->board = calloc(cols * rows, sizeof(board_pos_t));
    board->pacmans = calloc(1, sizeof(pacman_t));
    board->ghosts = calloc(n_mons, sizeof(ghost_t));
    
    if (!board->board || !board->pacmans || !board->ghosts) {
        cleanup_board(board);
        return -1;
    }
    
    // Parse board content
    // Legend: 'X' = wall, '@' = portal, 'o' = walkable space (with dot)
    int dot_count = 0, portal_count = 0, wall_count = 0;
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            int board_idx = i * cols + j;
            char c = board_data[board_idx];
            
            switch (c) {
                case 'X':  // Wall
                    board->board[board_idx].content = 'W';
                    wall_count++;
                    break;
                case '@':  // Portal
                    board->board[board_idx].content = ' ';
                    board->board[board_idx].has_portal = 1;
                    portal_count++;
                    break;
                case 'o':  // Walkable space (with dot)
                    board->board[board_idx].content = ' ';
                    board->board[board_idx].has_dot = 1;
                    dot_count++;
                    break;
                default:   // Anything else is empty
                    board->board[board_idx].content = ' ';
                    break;
            }
        }
    }
    
    debug("Board parsed: %d walls, %d dots, %d portals\n", wall_count, dot_count, portal_count);
    
    // Store file references
    strncpy(board->pacman_file, pac_file, sizeof(board->pacman_file) - 1);
    for (int i = 0; i < n_mons && i < MAX_GHOSTS; i++) {
        strncpy(board->ghosts_files[i], mon_files[i], sizeof(board->ghosts_files[i]) - 1);
    }
    
    // Load entities from their behavior files
    for (int i = 0; i < n_mons; i++) {
        if (load_ghost_from_file(board, dir_path, mon_files[i], i) < 0) {
            cleanup_board(board);
            return -1;
        }
    }
    
    if (pac_file[0] != '\0') {
        // Load Pacman from behavior file (automatic movement)
        if (load_pacman_from_file(board, dir_path, pac_file, accumulated_points) < 0) {
            cleanup_board(board);
            return -1;
        }
    } else {
        // No .p file - create manual Pacman at default position
        // Find first walkable position (has_dot or empty, not wall/portal)
        pacman_t* pac = &board->pacmans[0];
        pac->alive = 1;
        pac->points = accumulated_points;
        pac->passo = 0;
        pac->waiting = 0;
        pac->current_move = 0;
        pac->n_moves = 0;  // 0 = manual control
        
        // Find starting position: first 'o' cell (row-major order)
        int found = 0;
        for (int i = 0; i < rows && !found; i++) {
            for (int j = 0; j < cols && !found; j++) {
                int idx = i * cols + j;
                if (board->board[idx].has_dot && board->board[idx].content == ' ') {
                    pac->pos_x = j;
                    pac->pos_y = i;
                    board->board[idx].content = 'P';
                    board->board[idx].has_dot = 0;  // Pacman "eats" starting dot
                    found = 1;
                }
            }
        }
        
        if (!found) {
            debug("Error: No valid starting position for manual Pacman\n");
            cleanup_board(board);
            return -1;
        }
        
        debug("Manual Pacman placed at (%d, %d)\n", pac->pos_x, pac->pos_y);
    }
    
    return 0;
}

void open_debug_file(char *filename) {
    debugfile = fopen(filename, "w");
    // Note: debugfile may be NULL if fopen fails
    // debug() function handles this gracefully
}

void close_debug_file() {
    if (debugfile) {
        fclose(debugfile);
        debugfile = NULL;
    }
}

void debug(const char * format, ...) {
    if (!debugfile) return;  // Guard against NULL debugfile
    
    va_list args;
    va_start(args, format);
    vfprintf(debugfile, format, args);
    va_end(args);

    fflush(debugfile);
}

void print_board(board_t *board) {
    if (!board || !board->board) {
        debug("[%d] Board is empty or not initialized.\n", getpid());
        return;
    }

    // Large buffer to accumulate the whole output
    char buffer[8192];
    size_t offset = 0;

    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "=== [%d] LEVEL INFO ===\n"
                       "Dimensions: %d x %d\n"
                       "Tempo: %d\n"
                       "Pacman file: %s\n",
                       getpid(), board->height, board->width, board->tempo, board->pacman_file);

    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "Monster files (%d):\n", board->n_ghosts);

    for (int i = 0; i < board->n_ghosts; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                           "  - %s\n", board->ghosts_files[i]);
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n=== BOARD ===\n");

    for (int y = 0; y < board->height; y++) {
        for (int x = 0; x < board->width; x++) {
            int idx = y * board->width + x;
            if (offset < sizeof(buffer) - 2) {
                buffer[offset++] = board->board[idx].content;
            }
        }
        if (offset < sizeof(buffer) - 2) {
            buffer[offset++] = '\n';
        }
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "==================\n");

    buffer[offset] = '\0';

    debug("%s", buffer);
}
