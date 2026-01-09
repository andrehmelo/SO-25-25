#include "board.h"
#include "display.h"
#include "threads.h"
#include "session.h"
#include "protocol.h"
#include "game_manager.h"
#include "leaderboard.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

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
// Screen Refresh (stub for server mode)
// =============================================================================

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    // In server mode, we don't use ncurses - board updates are sent via FIFO
    (void)game_board;
    (void)mode;
}

// =============================================================================
// Main - Server Entry Point
// =============================================================================

// Global server context for signal handling
static server_context_t* g_server_ctx = NULL;

static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        debug("[Signal] Received signal %d, shutting down...\n", sig);
        if (g_server_ctx) {
            g_server_ctx->running = false;
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 4) {
        const char* usage_msg = "Usage: ./Pacmanist <level_directory> <max_games> <fifo_name>\n";
        if (write(STDERR_FILENO, usage_msg, strlen(usage_msg)) < 0) {
            // Silently ignore write error
        }
        return 1;
    }

    const char* level_dir = argv[1];
    int max_games = atoi(argv[2]);
    const char* server_fifo_path = argv[3];
    
    // Validate max_games
    if (max_games <= 0) {
        const char* err_msg = "Error: max_games must be positive\n";
        if (write(STDERR_FILENO, err_msg, strlen(err_msg)) < 0) {}
        return 1;
    }
    
    // Scan directory for .lvl files
    char* level_files[MAX_LEVELS] = {0};
    int n_levels = scan_level_files(level_dir, level_files, MAX_LEVELS);
    
    if (n_levels < 0) {
        const char* err_msg = "Error: Cannot open level directory\n";
        if (write(STDERR_FILENO, err_msg, strlen(err_msg)) < 0) {}
        return 1;
    }
    
    if (n_levels == 0) {
        const char* err_msg = "Error: No .lvl files found in directory\n";
        if (write(STDERR_FILENO, err_msg, strlen(err_msg)) < 0) {}
        return 1;
    }

    // Random seed
    srand((unsigned int)time(NULL));

    open_debug_file("server-debug.log");
    
    debug("=== PacmanIST Server Started (Multi-Session Mode) ===\n");
    debug("Level directory: %s\n", level_dir);
    debug("Max concurrent games: %d\n", max_games);
    debug("Server FIFO: %s\n", server_fifo_path);
    debug("Found %d level files:\n", n_levels);
    for (int i = 0; i < n_levels; i++) {
        debug("  [%d] %s\n", i, level_files[i]);
    }

    // Initialize server context
    server_context_t server_ctx;
    if (server_init(&server_ctx, max_games, level_dir, server_fifo_path, 
                    level_files, n_levels) < 0) {
        debug("Error: Failed to initialize server\n");
        free_level_files(level_files, n_levels);
        close_debug_file();
        return 1;
    }
    
    // Setup signal handlers
    g_server_ctx = &server_ctx;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Setup SIGUSR1 handler for leaderboard generation
    setup_sigusr1_handler();
    debug("SIGUSR1 handler installed - send 'kill -SIGUSR1 %d' to generate top5.txt\n", getpid());
    
    // Start game manager threads (consumers)
    if (server_start_managers(&server_ctx) < 0) {
        debug("Error: Failed to start game managers\n");
        server_cleanup(&server_ctx);
        free_level_files(level_files, n_levels);
        close_debug_file();
        return 1;
    }
    
    // Run host thread (producer) - this blocks until shutdown
    server_run_host(&server_ctx);
    
    // Shutdown and cleanup
    server_shutdown(&server_ctx);
    server_cleanup(&server_ctx);
    free_level_files(level_files, n_levels);
    close_debug_file();

    return 0;
}
