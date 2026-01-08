#include "board.h"
#include "display.h"
#include "threads.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <pthread.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4
#define PACMAN_DIED 5

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
// Game Logic (Exercise 3: Threaded version)
// =============================================================================

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

/**
 * Play one level using threads.
 * Returns: NEXT_LEVEL, QUIT_GAME, PACMAN_DIED, or CREATE_BACKUP
 */
static int play_level_threaded(board_t* game_board) {
    game_context_t ctx;
    
    if (init_game_context(&ctx, game_board) < 0) {
        debug("Error: Failed to initialize game context\n");
        return QUIT_GAME;
    }
    
    // Initial board draw
    draw_board(game_board, DRAW_MENU);
    refresh_screen();
    
    // Start all game threads
    if (start_game_threads(&ctx) < 0) {
        debug("Error: Failed to start game threads\n");
        cleanup_game_context(&ctx);
        return QUIT_GAME;
    }
    
    // Wait for game to end (threads will set state)
    game_state_t final_state;
    while (ctx.threads_running) {
        sleep_ms(50);  // Check periodically
        
        final_state = get_game_state(&ctx);
        if (final_state != GAME_RUNNING) {
            break;
        }
    }
    
    // Get final state before stopping threads
    final_state = get_game_state(&ctx);
    bool pacman_died = ctx.pacman_dead;
    
    // Stop all threads
    stop_game_threads(&ctx);
    cleanup_game_context(&ctx);
    
    // Convert game state to return value
    switch (final_state) {
        case GAME_NEXT_LEVEL:
            return NEXT_LEVEL;
        case GAME_QUIT:
            return QUIT_GAME;
        case GAME_CHECKPOINT:
            return CREATE_BACKUP;
        case GAME_OVER:
            return pacman_died ? PACMAN_DIED : QUIT_GAME;
        case GAME_WON:
            return NEXT_LEVEL;
        default:
            return QUIT_GAME;
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        // Use POSIX write instead of printf (avoid stdio)
        const char* usage_msg = "Usage: ./Pacmanist <level_directory>\n";
        if (write(STDERR_FILENO, usage_msg, strlen(usage_msg)) < 0) {
            // Silently ignore write error
        }
        return 1;
    }

    const char* level_dir = argv[1];
    
    // Scan directory for .lvl files
    char* level_files[MAX_LEVELS] = {0};
    int n_levels = scan_level_files(level_dir, level_files, MAX_LEVELS);
    
    if (n_levels < 0) {
        const char* err_msg = "Error: Cannot open level directory\n";
        if (write(STDERR_FILENO, err_msg, strlen(err_msg)) < 0) {
            // Silently ignore write error
        }
        return 1;
    }
    
    if (n_levels == 0) {
        const char* err_msg = "Error: No .lvl files found in directory\n";
        if (write(STDERR_FILENO, err_msg, strlen(err_msg)) < 0) {
            // Silently ignore write error
        }
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
    bool has_checkpoint = false;  // Exercise 2: only one checkpoint allowed

    while (!game_over && current_level < n_levels) {
        // Load level from file
        if (load_level_from_file(&game_board, level_dir, level_files[current_level], accumulated_points) < 0) {
            debug("Error loading level: %s\n", level_files[current_level]);
            break;
        }
        
        debug("Loaded level %d: %s\n", current_level, level_files[current_level]);
        print_board(&game_board);
        
        // Inner loop: play same level (may repeat after checkpoint/restore)
        bool level_done = false;
        while (!level_done && !game_over) {
            // Exercise 3: Play level using threads
            int result = play_level_threaded(&game_board);

            if (result == NEXT_LEVEL) {
                accumulated_points = game_board.pacmans[0].points;
                screen_refresh(&game_board, DRAW_WIN);
                sleep_ms(game_board.tempo);
                current_level++;
                level_done = true;
                
                // Check if this was the last level
                if (current_level >= n_levels) {
                    game_won = true;
                    game_over = true;
                }
            }
            else if (result == QUIT_GAME) {
                screen_refresh(&game_board, DRAW_GAME_OVER); 
                sleep_ms(game_board.tempo);
                game_over = true;
                level_done = true;
            }
            // Exercise 2: Handle checkpoint creation with fork
            else if (result == CREATE_BACKUP) {
                if (!has_checkpoint) {
                    debug("[%d] Creating checkpoint...\n", getpid());
                    
                    pid_t pid = fork();
                    
                    if (pid < 0) {
                        // Fork failed
                        debug("[%d] Fork failed!\n", getpid());
                    }
                    else if (pid == 0) {
                        // CHILD: continues playing the game from current state
                        debug("[%d] Child process - continuing game\n", getpid());
                        has_checkpoint = true;  // Child knows there's a backup
                        
                        // Advance pacman to next command (skip the 'G' we just processed)
                        if (game_board.pacmans[0].n_moves > 0) {
                            game_board.pacmans[0].current_move++;
                        }
                        
                        // Reinitialize ncurses in child (fork duplicates but needs refresh)
                        terminal_cleanup();
                        terminal_init();
                        
                        // Continue inner loop - same board state preserved by fork!
                        continue;
                    }
                    else {
                        // PARENT: becomes the checkpoint, waits for child
                        debug("[%d] Parent process - waiting as checkpoint (child=%d)\n", getpid(), pid);
                        
                        int status;
                        waitpid(pid, &status, 0);
                        
                        // Child finished - check why
                        if (WIFEXITED(status)) {
                            int exit_code = WEXITSTATUS(status);
                            debug("[%d] Child exited with code %d\n", getpid(), exit_code);
                            
                            if (exit_code == 0) {
                                // Child won or quit normally - parent also exits
                                debug("[%d] Child completed successfully, parent exiting\n", getpid());
                                game_over = true;
                                level_done = true;
                            }
                            else {
                                // Child died (Pacman died) - parent resumes from checkpoint
                                debug("[%d] Pacman died! Restoring from checkpoint...\n", getpid());
                                has_checkpoint = false;  // Can create new checkpoint
                                
                                // Reinitialize ncurses (was inherited but child used it)
                                terminal_cleanup();
                                terminal_init();
                                
                                // Parent already has the game state from the fork moment
                                // Continue inner loop with preserved board state
                                continue;
                            }
                        }
                    }
                }
                else {
                    debug("[%d] Checkpoint already exists, ignoring 'G'\n", getpid());
                    // Continue playing same level - inner loop continues
                    continue;
                }
            }
            // Exercise 2: Pacman died - if we're a child, exit with error
            else if (result == PACMAN_DIED) {
                screen_refresh(&game_board, DRAW_GAME_OVER);
                sleep_ms(game_board.tempo);
                
                if (has_checkpoint) {
                    // We're the child process - exit with error so parent can restore
                    debug("[%d] Child: Pacman died, exiting for restore\n", getpid());
                    unload_level(&game_board);
                    free_level_files(level_files, n_levels);
                    terminal_cleanup();
                    close_debug_file();
                    _exit(1);  // Use _exit to avoid flushing parent's buffers
                }
                else {
                    // No checkpoint - game over
                    game_over = true;
                    level_done = true;
                }
            }
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
