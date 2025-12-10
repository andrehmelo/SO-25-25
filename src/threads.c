#include "threads.h"
#include "display.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <ncurses.h>

// =============================================================================
// Internal Structures
// =============================================================================

// Data passed to each ghost thread
typedef struct {
    game_context_t* ctx;
    int ghost_index;
} ghost_thread_data_t;

// =============================================================================
// Context Initialization / Cleanup
// =============================================================================

int init_game_context(game_context_t* ctx, board_t* board) {
    memset(ctx, 0, sizeof(game_context_t));
    ctx->board = board;
    ctx->state = GAME_PAUSED;
    ctx->pacman_dead = false;
    ctx->board_changed = true;  // Initial draw needed
    ctx->threads_running = false;
    ctx->pending_input = '\0';
    ctx->has_input = false;
    
    // Initialize RW lock for board access
    if (pthread_rwlock_init(&ctx->board_lock, NULL) != 0) {
        return -1;
    }
    
    // Initialize state mutex
    if (pthread_mutex_init(&ctx->state_mutex, NULL) != 0) {
        pthread_rwlock_destroy(&ctx->board_lock);
        return -1;
    }
    
    // Initialize display condition variable
    if (pthread_cond_init(&ctx->display_cond, NULL) != 0) {
        pthread_mutex_destroy(&ctx->state_mutex);
        pthread_rwlock_destroy(&ctx->board_lock);
        return -1;
    }
    
    // Initialize game condition variable
    if (pthread_cond_init(&ctx->game_cond, NULL) != 0) {
        pthread_cond_destroy(&ctx->display_cond);
        pthread_mutex_destroy(&ctx->state_mutex);
        pthread_rwlock_destroy(&ctx->board_lock);
        return -1;
    }
    
    // Initialize input mutex and condition
    if (pthread_mutex_init(&ctx->input_mutex, NULL) != 0) {
        pthread_cond_destroy(&ctx->game_cond);
        pthread_cond_destroy(&ctx->display_cond);
        pthread_mutex_destroy(&ctx->state_mutex);
        pthread_rwlock_destroy(&ctx->board_lock);
        return -1;
    }
    
    if (pthread_cond_init(&ctx->input_cond, NULL) != 0) {
        pthread_mutex_destroy(&ctx->input_mutex);
        pthread_cond_destroy(&ctx->game_cond);
        pthread_cond_destroy(&ctx->display_cond);
        pthread_mutex_destroy(&ctx->state_mutex);
        pthread_rwlock_destroy(&ctx->board_lock);
        return -1;
    }
    
    // Allocate ghost thread handles
    ctx->n_ghost_threads = board->n_ghosts;
    if (ctx->n_ghost_threads > 0) {
        ctx->ghost_threads = calloc((size_t)ctx->n_ghost_threads, sizeof(pthread_t));
        if (!ctx->ghost_threads) {
            pthread_cond_destroy(&ctx->input_cond);
            pthread_mutex_destroy(&ctx->input_mutex);
            pthread_cond_destroy(&ctx->game_cond);
            pthread_cond_destroy(&ctx->display_cond);
            pthread_mutex_destroy(&ctx->state_mutex);
            pthread_rwlock_destroy(&ctx->board_lock);
            return -1;
        }
    }
    
    return 0;
}

void cleanup_game_context(game_context_t* ctx) {
    if (ctx->ghost_threads) {
        free(ctx->ghost_threads);
        ctx->ghost_threads = NULL;
    }
    
    pthread_cond_destroy(&ctx->input_cond);
    pthread_mutex_destroy(&ctx->input_mutex);
    pthread_cond_destroy(&ctx->game_cond);
    pthread_cond_destroy(&ctx->display_cond);
    pthread_mutex_destroy(&ctx->state_mutex);
    pthread_rwlock_destroy(&ctx->board_lock);
}

// =============================================================================
// State Management (Thread-Safe)
// =============================================================================

void set_game_state(game_context_t* ctx, game_state_t state) {
    pthread_mutex_lock(&ctx->state_mutex);
    ctx->state = state;
    pthread_cond_broadcast(&ctx->game_cond);  // Wake all waiting threads
    pthread_mutex_unlock(&ctx->state_mutex);
}

game_state_t get_game_state(game_context_t* ctx) {
    pthread_mutex_lock(&ctx->state_mutex);
    game_state_t state = ctx->state;
    pthread_mutex_unlock(&ctx->state_mutex);
    return state;
}

void request_display_refresh(game_context_t* ctx) {
    pthread_mutex_lock(&ctx->state_mutex);
    ctx->board_changed = true;
    pthread_cond_signal(&ctx->display_cond);
    pthread_mutex_unlock(&ctx->state_mutex);
}

// =============================================================================
// Input Handling (Display -> Pacman Thread)
// =============================================================================

void send_input(game_context_t* ctx, char input) {
    pthread_mutex_lock(&ctx->input_mutex);
    ctx->pending_input = input;
    ctx->has_input = true;
    pthread_cond_signal(&ctx->input_cond);
    pthread_mutex_unlock(&ctx->input_mutex);
}

// Wait for input with timeout (returns '\0' if timeout)
char wait_for_input(game_context_t* ctx, int timeout_ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += timeout_ms * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += ts.tv_nsec / 1000000000L;
        ts.tv_nsec %= 1000000000L;
    }
    
    pthread_mutex_lock(&ctx->input_mutex);
    
    while (!ctx->has_input && ctx->threads_running) {
        int rc = pthread_cond_timedwait(&ctx->input_cond, &ctx->input_mutex, &ts);
        if (rc == ETIMEDOUT) {
            break;
        }
    }
    
    char input = '\0';
    if (ctx->has_input) {
        input = ctx->pending_input;
        ctx->has_input = false;
        ctx->pending_input = '\0';
    }
    
    pthread_mutex_unlock(&ctx->input_mutex);
    return input;
}

// =============================================================================
// Display Thread (ONLY thread that uses ncurses)
// =============================================================================

void* display_thread_func(void* arg) {
    game_context_t* ctx = (game_context_t*)arg;
    
    debug("[Display] Thread started\n");
    
    // Enable non-blocking input for getch()
    nodelay(stdscr, TRUE);
    
    while (ctx->threads_running) {
        game_state_t state = get_game_state(ctx);
        
        if (state != GAME_RUNNING) {
            // Game not running - check if we should exit
            if (state == GAME_QUIT || state == GAME_OVER || 
                state == GAME_WON || state == GAME_NEXT_LEVEL ||
                state == GAME_CHECKPOINT || state == GAME_RESTORE) {
                break;
            }
            sleep_ms(10);
            continue;
        }
        
        // Non-blocking input check
        int ch = getch();
        if (ch != ERR) {
            char input = '\0';
            ch = toupper(ch);
            
            switch (ch) {
                case 'W':
                case 'S':
                case 'A':
                case 'D':
                    input = (char)ch;
                    send_input(ctx, input);
                    break;
                case 'Q':
                    set_game_state(ctx, GAME_QUIT);
                    break;
                case 'G':
                    set_game_state(ctx, GAME_CHECKPOINT);
                    break;
            }
        }
        
        // Check if display needs refresh
        pthread_mutex_lock(&ctx->state_mutex);
        if (ctx->board_changed) {
            ctx->board_changed = false;
            pthread_mutex_unlock(&ctx->state_mutex);
            
            // Read lock for drawing
            pthread_rwlock_rdlock(&ctx->board_lock);
            draw_board(ctx->board, DRAW_MENU);
            pthread_rwlock_unlock(&ctx->board_lock);
            
            refresh_screen();
        } else {
            pthread_mutex_unlock(&ctx->state_mutex);
        }
        
        // Small sleep to prevent busy-waiting
        sleep_ms(16);  // ~60 FPS max
    }
    
    debug("[Display] Thread exiting\n");
    return NULL;
}

// =============================================================================
// Pacman Thread
// =============================================================================

void* pacman_thread_func(void* arg) {
    game_context_t* ctx = (game_context_t*)arg;
    board_t* board = ctx->board;
    pacman_t* pacman = &board->pacmans[0];
    
    debug("[Pacman] Thread started\n");
    
    // Wait for initial passo
    if (pacman->waiting > 0) {
        for (int i = 0; i < pacman->waiting && ctx->threads_running; i++) {
            sleep_ms(board->tempo > 0 ? board->tempo : 100);
        }
    }
    
    while (ctx->threads_running && get_game_state(ctx) == GAME_RUNNING) {
        command_t cmd;
        bool has_command = false;
        
        if (pacman->n_moves == 0) {
            // Manual control - wait for input from display thread
            char input = wait_for_input(ctx, board->tempo > 0 ? board->tempo : 100);
            
            if (input != '\0') {
                cmd.command = input;
                cmd.turns = 1;
                cmd.turns_left = 1;
                has_command = true;
            }
        } else {
            // Automatic movement from file
            cmd = pacman->moves[pacman->current_move % pacman->n_moves];
            has_command = true;
            
            // Handle special commands
            if (cmd.command == 'Q') {
                set_game_state(ctx, GAME_QUIT);
                break;
            }
            if (cmd.command == 'G') {
                set_game_state(ctx, GAME_CHECKPOINT);
                // Wait for checkpoint to be processed
                while (get_game_state(ctx) == GAME_CHECKPOINT && ctx->threads_running) {
                    sleep_ms(10);
                }
                pacman->current_move++;
                continue;
            }
        }
        
        if (has_command && get_game_state(ctx) == GAME_RUNNING) {
            // Write lock for moving pacman
            pthread_rwlock_wrlock(&ctx->board_lock);
            
            debug("[Pacman] Moving: %c\n", cmd.command);
            int result = move_pacman(board, 0, &cmd);
            
            pthread_rwlock_unlock(&ctx->board_lock);
            
            // Handle movement result
            if (result == REACHED_PORTAL) {
                set_game_state(ctx, GAME_NEXT_LEVEL);
                break;
            }
            
            if (result == DEAD_PACMAN || !pacman->alive) {
                pthread_mutex_lock(&ctx->state_mutex);
                ctx->pacman_dead = true;
                pthread_mutex_unlock(&ctx->state_mutex);
                set_game_state(ctx, GAME_OVER);
                break;
            }
            
            // Request display refresh
            request_display_refresh(ctx);
            
            // For automatic pacman, advance to next move
            if (pacman->n_moves > 0) {
                pacman->current_move++;
            }
        }
        
        // Wait for tempo between moves (for automatic pacman)
        if (pacman->n_moves > 0 && board->tempo > 0) {
            sleep_ms(board->tempo);
        }
    }
    
    debug("[Pacman] Thread exiting\n");
    return NULL;
}

// =============================================================================
// Ghost Thread
// =============================================================================

void* ghost_thread_func(void* arg) {
    ghost_thread_data_t* data = (ghost_thread_data_t*)arg;
    game_context_t* ctx = data->ctx;
    int ghost_index = data->ghost_index;
    board_t* board = ctx->board;
    ghost_t* ghost = &board->ghosts[ghost_index];
    
    debug("[Ghost %d] Thread started\n", ghost_index);
    
    // Wait for initial passo
    if (ghost->waiting > 0) {
        for (int i = 0; i < ghost->waiting && ctx->threads_running; i++) {
            sleep_ms(board->tempo > 0 ? board->tempo : 100);
        }
    }
    
    while (ctx->threads_running && get_game_state(ctx) == GAME_RUNNING) {
        // Skip if ghost has no moves defined
        if (ghost->n_moves == 0) {
            sleep_ms(100);
            continue;
        }
        
        // Get current move
        command_t* cmd = &ghost->moves[ghost->current_move % ghost->n_moves];
        
        // Write lock for moving ghost
        pthread_rwlock_wrlock(&ctx->board_lock);
        
        debug("[Ghost %d] Moving: %c\n", ghost_index, cmd->command);
        move_ghost(board, ghost_index, cmd);
        
        // Check if pacman was killed
        if (!board->pacmans[0].alive) {
            pthread_rwlock_unlock(&ctx->board_lock);
            
            pthread_mutex_lock(&ctx->state_mutex);
            ctx->pacman_dead = true;
            pthread_mutex_unlock(&ctx->state_mutex);
            
            set_game_state(ctx, GAME_OVER);
            break;
        }
        
        pthread_rwlock_unlock(&ctx->board_lock);
        
        // Request display refresh
        request_display_refresh(ctx);
        
        // Wait for passo time between moves
        int wait_time = ghost->passo > 0 ? ghost->passo : 1;
        for (int i = 0; i < wait_time && ctx->threads_running && 
             get_game_state(ctx) == GAME_RUNNING; i++) {
            sleep_ms(board->tempo > 0 ? board->tempo : 100);
        }
        
        // Advance to next move
        ghost->current_move++;
    }
    
    debug("[Ghost %d] Thread exiting\n", ghost_index);
    
    // Free the thread data
    free(data);
    return NULL;
}

// =============================================================================
// Thread Management
// =============================================================================

int start_game_threads(game_context_t* ctx) {
    ctx->threads_running = true;
    set_game_state(ctx, GAME_RUNNING);
    
    // Start display thread
    if (pthread_create(&ctx->display_thread, NULL, display_thread_func, ctx) != 0) {
        ctx->threads_running = false;
        return -1;
    }
    
    // Start pacman thread
    if (pthread_create(&ctx->pacman_thread, NULL, pacman_thread_func, ctx) != 0) {
        ctx->threads_running = false;
        pthread_join(ctx->display_thread, NULL);
        return -1;
    }
    
    // Start ghost threads
    for (int i = 0; i < ctx->n_ghost_threads; i++) {
        ghost_thread_data_t* data = malloc(sizeof(ghost_thread_data_t));
        if (!data) {
            // Cleanup on failure
            ctx->threads_running = false;
            pthread_join(ctx->pacman_thread, NULL);
            pthread_join(ctx->display_thread, NULL);
            for (int j = 0; j < i; j++) {
                pthread_join(ctx->ghost_threads[j], NULL);
            }
            return -1;
        }
        
        data->ctx = ctx;
        data->ghost_index = i;
        
        if (pthread_create(&ctx->ghost_threads[i], NULL, ghost_thread_func, data) != 0) {
            free(data);
            ctx->threads_running = false;
            pthread_join(ctx->pacman_thread, NULL);
            pthread_join(ctx->display_thread, NULL);
            for (int j = 0; j < i; j++) {
                pthread_join(ctx->ghost_threads[j], NULL);
            }
            return -1;
        }
    }
    
    debug("[Main] All %d threads started\n", 2 + ctx->n_ghost_threads);
    return 0;
}

void stop_game_threads(game_context_t* ctx) {
    debug("[Main] Stopping threads...\n");
    
    // Signal all threads to stop
    ctx->threads_running = false;
    
    // Wake up any waiting threads
    pthread_mutex_lock(&ctx->input_mutex);
    pthread_cond_broadcast(&ctx->input_cond);
    pthread_mutex_unlock(&ctx->input_mutex);
    
    pthread_mutex_lock(&ctx->state_mutex);
    pthread_cond_broadcast(&ctx->display_cond);
    pthread_cond_broadcast(&ctx->game_cond);
    pthread_mutex_unlock(&ctx->state_mutex);
    
    // Wait for display thread
    pthread_join(ctx->display_thread, NULL);
    debug("[Main] Display thread joined\n");
    
    // Wait for pacman thread
    pthread_join(ctx->pacman_thread, NULL);
    debug("[Main] Pacman thread joined\n");
    
    // Wait for ghost threads
    for (int i = 0; i < ctx->n_ghost_threads; i++) {
        pthread_join(ctx->ghost_threads[i], NULL);
        debug("[Main] Ghost %d thread joined\n", i);
    }
    
    debug("[Main] All threads stopped\n");
}
