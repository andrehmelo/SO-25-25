#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4

// MAX_LEVELS is defined in board.h

// =============================================================================
// Level File Discovery
// =============================================================================

/**
 * Checks if a filename has the given extension.
 */
static int has_extension(const char* filename, const char* ext) {
    size_t fname_len = strlen(filename);
    size_t ext_len = strlen(ext);
    
    if (fname_len < ext_len + 1) return 0;
    
    return strcmp(filename + fname_len - ext_len, ext) == 0;
}

/**
 * Comparison function for qsort to sort strings alphabetically.
 */
static int compare_strings(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

/**
 * Scans a directory for .lvl files and returns them sorted alphabetically.
 * @param dir_path      Path to the directory.
 * @param level_files   Output: array of level file names (allocated).
 * @param max_levels    Maximum number of levels.
 * @return              Number of level files found, or -1 on error.
 */
static int scan_level_files(const char* dir_path, char* level_files[], int max_levels) {
    DIR* dir = opendir(dir_path);
    if (!dir) {
        return -1;
    }
    
    int count = 0;
    struct dirent* entry;
    
    while ((entry = readdir(dir)) != NULL && count < max_levels) {
        // Skip . and ..
        if (entry->d_name[0] == '.') continue;
        
        // Check for .lvl extension
        if (has_extension(entry->d_name, ".lvl")) {
            level_files[count] = strdup(entry->d_name);
            if (level_files[count]) {
                count++;
            }
        }
    }
    
    closedir(dir);
    
    // Sort level files alphabetically
    if (count > 1) {
        qsort(level_files, (size_t)count, sizeof(char*), compare_strings);
    }
    
    return count;
}

/**
 * Frees the level files array.
 */
static void free_level_files(char* level_files[], int count) {
    for (int i = 0; i < count; i++) {
        free(level_files[i]);
    }
}

// =============================================================================
// Game Logic
// =============================================================================

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

int play_board(board_t * game_board) {
    pacman_t* pacman = &game_board->pacmans[0];
    command_t* play;
    if (pacman->n_moves == 0) { // if is user input
        command_t c; 
        c.command = get_input();

        if(c.command == '\0')
            return CONTINUE_PLAY;

        c.turns = 1;
        play = &c;
    }
    else { // else if the moves are pre-defined in the file
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the pacman
        play = &pacman->moves[pacman->current_move%pacman->n_moves];
    }

    debug("KEY %c\n", play->command);

    if (play->command == 'Q') {
        return QUIT_GAME;
    }

    int result = move_pacman(game_board, 0, play);
    if (result == REACHED_PORTAL) {
        // Next level
        return NEXT_LEVEL;
    }

    if(result == DEAD_PACMAN) {
        return QUIT_GAME;
    }
    
    for (int i = 0; i < game_board->n_ghosts; i++) {
        ghost_t* ghost = &game_board->ghosts[i];
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the ghost
        move_ghost(game_board, i, &ghost->moves[ghost->current_move%ghost->n_moves]);
    }

    if (!game_board->pacmans[0].alive) {
        return QUIT_GAME;
    }      

    return CONTINUE_PLAY;  
}

int main(int argc, char** argv) {
    if (argc != 2) {
        // Use POSIX write instead of printf (avoid stdio)
        const char* usage_msg = "Usage: ./Pacmanist <level_directory>\n";
        write(STDERR_FILENO, usage_msg, strlen(usage_msg));
        return 1;
    }

    const char* level_dir = argv[1];
    
    // Scan directory for .lvl files
    char* level_files[MAX_LEVELS] = {0};
    int n_levels = scan_level_files(level_dir, level_files, MAX_LEVELS);
    
    if (n_levels < 0) {
        const char* err_msg = "Error: Cannot open level directory\n";
        write(STDERR_FILENO, err_msg, strlen(err_msg));
        return 1;
    }
    
    if (n_levels == 0) {
        const char* err_msg = "Error: No .lvl files found in directory\n";
        write(STDERR_FILENO, err_msg, strlen(err_msg));
        return 1;
    }

    // Random seed for any random movements
    srand((unsigned int)time(NULL));

    open_debug_file("debug.log");
    
    debug("Found %d level files:\n", n_levels);
    for (int i = 0; i < n_levels; i++) {
        debug("  [%d] %s\n", i, level_files[i]);
    }

    terminal_init();
    
    int accumulated_points = 0;
    bool game_won = false;
    bool game_over = false;
    board_t game_board = {0};
    int current_level = 0;

    while (!game_over && current_level < n_levels) {
        // Load level from file
        if (load_level_from_file(&game_board, level_dir, level_files[current_level], accumulated_points) < 0) {
            debug("Error loading level: %s\n", level_files[current_level]);
            break;
        }
        
        debug("Loaded level %d: %s\n", current_level, level_files[current_level]);
        print_board(&game_board);
        
        draw_board(&game_board, DRAW_MENU);
        refresh_screen();

        while (true) {
            int result = play_board(&game_board); 

            if (result == NEXT_LEVEL) {
                accumulated_points = game_board.pacmans[0].points;
                screen_refresh(&game_board, DRAW_WIN);
                sleep_ms(game_board.tempo);
                current_level++;
                
                // Check if this was the last level
                if (current_level >= n_levels) {
                    game_won = true;
                    game_over = true;
                }
                break;
            }

            if (result == QUIT_GAME) {
                screen_refresh(&game_board, DRAW_GAME_OVER); 
                sleep_ms(game_board.tempo);
                game_over = true;
                break;
            }
    
            screen_refresh(&game_board, DRAW_MENU); 

            accumulated_points = game_board.pacmans[0].points;      
        }
        
        print_board(&game_board);
        unload_level(&game_board);
    }
    
    // Show final result
    if (game_won) {
        debug("Game completed! Final score: %d\n", accumulated_points);
    } else {
        debug("Game over. Score: %d\n", accumulated_points);
    }

    terminal_cleanup();
    
    // Cleanup level files
    free_level_files(level_files, n_levels);

    close_debug_file();

    return 0;
}
