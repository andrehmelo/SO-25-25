#ifndef THREADS_H
#define THREADS_H

#include <pthread.h>
#include <stdbool.h>
#include "board.h"

// =============================================================================
// Game State for Thread Synchronization (Exercise 3)
// =============================================================================

typedef enum {
    GAME_RUNNING,
    GAME_PAUSED,
    GAME_NEXT_LEVEL,
    GAME_WON,
    GAME_OVER,
    GAME_QUIT,
    GAME_CHECKPOINT,
    GAME_RESTORE
} game_state_t;

// Thread-safe game context
typedef struct {
    board_t* board;                     // Shared game board
    
    // Game state
    game_state_t state;                 // Current game state
    bool pacman_dead;                   // Flag: pacman died
    bool board_changed;                 // Flag: board needs redraw
    
    // Synchronization primitives
    pthread_rwlock_t board_lock;        // RW lock for board access (maximize parallelism)
    pthread_mutex_t state_mutex;        // Mutex for game state changes
    pthread_cond_t display_cond;        // Signal display thread to refresh
    pthread_cond_t game_cond;           // Signal game state changes
    
    // Input from display thread to pacman thread
    pthread_mutex_t input_mutex;
    pthread_cond_t input_cond;
    char pending_input;                 // Input character waiting to be processed
    bool has_input;
    
    // Thread handles
    pthread_t display_thread;
    pthread_t pacman_thread;
    pthread_t* ghost_threads;           // Array of ghost thread handles
    int n_ghost_threads;
    
    // Thread control
    volatile bool threads_running;      // Flag to signal threads to stop (volatile for multi-thread visibility)
    
} game_context_t;

// =============================================================================
// Thread Functions
// =============================================================================

// Initialize/cleanup game context
int init_game_context(game_context_t* ctx, board_t* board);
void cleanup_game_context(game_context_t* ctx);

// Start/stop game threads
int start_game_threads(game_context_t* ctx);
void stop_game_threads(game_context_t* ctx);

// Thread entry points
void* display_thread_func(void* arg);
void* pacman_thread_func(void* arg);
void* ghost_thread_func(void* arg);

// Thread-safe board operations
void request_display_refresh(game_context_t* ctx);
void set_game_state(game_context_t* ctx, game_state_t state);
game_state_t get_game_state(game_context_t* ctx);

// Input handling
void send_input(game_context_t* ctx, char input);
char wait_for_input(game_context_t* ctx, int timeout_ms);

#endif
