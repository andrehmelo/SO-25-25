#ifndef GAME_MANAGER_H
#define GAME_MANAGER_H

#include "pc_buffer.h"
#include "session.h"
#include "leaderboard.h"
#include <pthread.h>
#include <stdbool.h>

// Maximum number of concurrent games
#define MAX_CONCURRENT_GAMES 64

// Forward declaration
struct server_context_s;

/**
 * Context for a game manager thread.
 * Each manager thread handles one client session at a time.
 */
typedef struct {
    int id;                             // Manager thread ID (0 to max_games-1)
    pthread_t thread;                   // Thread handle
    pc_buffer_t* request_buffer;        // Shared buffer for connection requests
    
    // Level information (shared, read-only)
    char** level_files;                 // Array of level file names
    int n_levels;                       // Number of levels
    const char* level_dir;              // Level directory path
    
    // Leaderboard reference
    leaderboard_t* leaderboard;         // Shared leaderboard for tracking scores
    
    // State
    bool active;                        // Currently handling a session
    bool running;                       // Thread should keep running
} game_manager_t;

/**
 * Server context - holds all shared state.
 */
typedef struct server_context_s {
    // Configuration
    int max_games;
    const char* server_fifo_path;
    const char* level_dir;
    
    // Level information
    char** level_files;
    int n_levels;
    
    // Producer-consumer buffer for connection requests
    pc_buffer_t request_buffer;
    
    // Leaderboard for tracking active sessions and scores
    leaderboard_t leaderboard;
    
    // Game manager threads
    game_manager_t managers[MAX_CONCURRENT_GAMES];
    int n_managers;
    
    // Server state
    bool running;
    int server_fd;
} server_context_t;

/**
 * Initialize the server context.
 */
int server_init(server_context_t* ctx, int max_games, const char* level_dir, 
                const char* server_fifo_path, char** level_files, int n_levels);

/**
 * Start all game manager threads.
 */
int server_start_managers(server_context_t* ctx);

/**
 * Run the host thread (reads connection requests from FIFO).
 * This function runs in the main thread and blocks until shutdown.
 */
void server_run_host(server_context_t* ctx);

/**
 * Shutdown the server and all threads.
 */
void server_shutdown(server_context_t* ctx);

/**
 * Cleanup server resources.
 */
void server_cleanup(server_context_t* ctx);

/**
 * Game manager thread function.
 * Consumes connection requests and handles client sessions.
 */
void* game_manager_thread_func(void* arg);

#endif
