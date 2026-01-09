#ifndef THREADS_H
#define THREADS_H

#include <pthread.h>
#include <stdbool.h>
#include "board.h"
#include "session.h"
#include "leaderboard.h"

// =============================================================================
// Game State for Thread Synchronization
// =============================================================================

typedef enum {
    GAME_RUNNING,
    GAME_PAUSED,
    GAME_NEXT_LEVEL,
    GAME_WON,
    GAME_OVER,
    GAME_QUIT,
    GAME_CLIENT_DISCONNECTED  // Client closed connection
} game_state_t;

// Thread-safe game context
typedef struct {
    board_t* board;                     // Shared game board
    client_session_t* session;          // Client session (for network communication)
    
    // Game state
    game_state_t state;                 // Current game state
    bool pacman_dead;                   // Flag: pacman died
    bool board_changed;                 // Flag: board needs redraw
    
    // Leaderboard tracking (for real-time updates)
    leaderboard_t* leaderboard;         // Pointer to global leaderboard
    int leaderboard_index;              // Index of this session in leaderboard
    
    // Synchronization primitives
    pthread_rwlock_t board_lock;        // RW lock for board access (maximize parallelism)
    pthread_mutex_t state_mutex;        // Mutex for game state changes
    pthread_cond_t display_cond;        // Signal session thread to send update
    pthread_cond_t game_cond;           // Signal game state changes
    
    // Thread handles
    pthread_t session_thread;           // Thread that sends board updates to client
    pthread_t pacman_thread;            // Thread that processes pacman commands
    pthread_t* ghost_threads;           // Array of ghost thread handles
    int n_ghost_threads;
    
    // Thread control
    volatile bool threads_running;      // Flag to signal threads to stop
    
} game_context_t;

// =============================================================================
// Thread Functions
// =============================================================================

// Initialize/cleanup game context
int init_game_context(game_context_t* ctx, board_t* board, client_session_t* session);
void cleanup_game_context(game_context_t* ctx);

// Set leaderboard for real-time updates
void set_game_leaderboard(game_context_t* ctx, leaderboard_t* lb, int lb_index);

// Start/stop game threads
int start_game_threads(game_context_t* ctx);
void stop_game_threads(game_context_t* ctx);

// Thread entry points
void* session_thread_func(void* arg);   // Sends board updates to client
void* pacman_thread_func(void* arg);    // Reads commands from client FIFO
void* ghost_thread_func(void* arg);     // Controls ghost movement

// Thread-safe board operations
void request_display_refresh(game_context_t* ctx);
void set_game_state(game_context_t* ctx, game_state_t state);
game_state_t get_game_state(game_context_t* ctx);

#endif
