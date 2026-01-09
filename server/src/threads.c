#include "threads.h"
#include "session.h"
#include "display.h"
#include "leaderboard.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

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

int init_game_context(game_context_t* ctx, board_t* board, client_session_t* session) {
    memset(ctx, 0, sizeof(game_context_t));
    ctx->board = board;
    ctx->session = session;
    ctx->state = GAME_PAUSED;
    ctx->pacman_dead = false;
    ctx->board_changed = true;  // Initial update needed
    ctx->threads_running = false;
    ctx->leaderboard = NULL;
    ctx->leaderboard_index = -1;
    
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
    
    // Allocate ghost thread handles
    ctx->n_ghost_threads = board->n_ghosts;
    if (ctx->n_ghost_threads > 0) {
        ctx->ghost_threads = calloc((size_t)ctx->n_ghost_threads, sizeof(pthread_t));
        if (!ctx->ghost_threads) {
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
    
    pthread_cond_destroy(&ctx->game_cond);
    pthread_cond_destroy(&ctx->display_cond);
    pthread_mutex_destroy(&ctx->state_mutex);
    pthread_rwlock_destroy(&ctx->board_lock);
}

void set_game_leaderboard(game_context_t* ctx, leaderboard_t* lb, int lb_index) {
    ctx->leaderboard = lb;
    ctx->leaderboard_index = lb_index;
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
// Session Thread (Sends board updates to client via FIFO)
// =============================================================================

void* session_thread_func(void* arg) {
    game_context_t* ctx = (game_context_t*)arg;
    client_session_t* session = ctx->session;
    board_t* board = ctx->board;
    
    // Block SIGUSR1 - only host thread should receive it
    block_sigusr1();
    
    debug("[Session] Thread started\n");
    
    while (ctx->threads_running) {
        game_state_t state = get_game_state(ctx);
        
        // Check if game has ended
        int victory = (state == GAME_WON || state == GAME_NEXT_LEVEL) ? 1 : 0;
        int game_over = (state == GAME_OVER || state == GAME_QUIT || 
                        state == GAME_CLIENT_DISCONNECTED) ? 1 : 0;
        
        // Send board update to client
        pthread_rwlock_rdlock(&ctx->board_lock);
        int result = send_board_update(session, board, victory, game_over);
        pthread_rwlock_unlock(&ctx->board_lock);
        
        if (result < 0) {
            // Client disconnected
            debug("[Session] Failed to send board update, client disconnected\n");
            set_game_state(ctx, GAME_CLIENT_DISCONNECTED);
            break;
        }
        
        // Exit if game ended (after sending the final update with game_over/victory)
        if (state != GAME_RUNNING) {
            debug("[Session] Game ended with state %d (victory=%d, game_over=%d)\n", 
                  state, victory, game_over);
            break;
        }
        
        // Wait for tempo before sending next update
        int tempo = board->tempo > 0 ? board->tempo : 100;
        sleep_ms(tempo);
    }
    
    debug("[Session] Thread exiting\n");
    return NULL;
}

// =============================================================================
// Pacman Thread (Reads commands from client FIFO)
// =============================================================================

void* pacman_thread_func(void* arg) {
    game_context_t* ctx = (game_context_t*)arg;
    board_t* board = ctx->board;
    client_session_t* session = ctx->session;
    pacman_t* pacman = &board->pacmans[0];
    
    // Block SIGUSR1 - only host thread should receive it
    block_sigusr1();
    
    debug("[Pacman] Thread started\n");
    
    // Wait for initial passo
    if (pacman->waiting > 0) {
        for (int i = 0; i < pacman->waiting && ctx->threads_running; i++) {
            sleep_ms(board->tempo > 0 ? board->tempo : 100);
        }
    }
    
    while (ctx->threads_running && get_game_state(ctx) == GAME_RUNNING) {
        // Read command from client via FIFO
        char cmd_char;
        int result = read_client_command(session, &cmd_char);
        
        if (result < 0) {
            // Client disconnected or error
            debug("[Pacman] Client disconnected\n");
            set_game_state(ctx, GAME_CLIENT_DISCONNECTED);
            break;
        }
        
        if (result == -2) {
            // Client requested disconnect
            debug("[Pacman] Client requested disconnect\n");
            set_game_state(ctx, GAME_QUIT);
            break;
        }
        
        // Handle quit command
        if (cmd_char == 'Q' || cmd_char == 'q') {
            set_game_state(ctx, GAME_QUIT);
            break;
        }
        
        // Convert to uppercase
        cmd_char = (char)toupper((unsigned char)cmd_char);
        
        // Only process valid movement commands
        if (cmd_char == 'W' || cmd_char == 'A' || cmd_char == 'S' || cmd_char == 'D') {
            command_t cmd;
            cmd.command = cmd_char;
            cmd.turns = 1;
            cmd.turns_left = 1;
            
            // Write lock for moving pacman
            pthread_rwlock_wrlock(&ctx->board_lock);
            
            debug("[Pacman] Moving: %c\n", cmd.command);
            int move_result = move_pacman(board, 0, &cmd);
            bool is_alive = pacman->alive;
            
            // Update session points
            session->accumulated_points = pacman->points;
            
            // Update leaderboard in real-time
            if (ctx->leaderboard && ctx->leaderboard_index >= 0) {
                leaderboard_update_points(ctx->leaderboard, ctx->leaderboard_index, 
                                         session->accumulated_points);
            }
            
            pthread_rwlock_unlock(&ctx->board_lock);
            
            // Handle movement result
            if (move_result == REACHED_PORTAL) {
                set_game_state(ctx, GAME_NEXT_LEVEL);
                break;
            }
            
            if (move_result == DEAD_PACMAN || !is_alive) {
                pthread_mutex_lock(&ctx->state_mutex);
                ctx->pacman_dead = true;
                pthread_mutex_unlock(&ctx->state_mutex);
                set_game_state(ctx, GAME_OVER);
                break;
            }
            
            // Request session thread to send updated board
            request_display_refresh(ctx);
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
    
    // Block SIGUSR1 - only host thread should receive it
    block_sigusr1();
    
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
        
        debug("[Ghost %d] Cmd: %c (move %d)\n", ghost_index, cmd->command, ghost->current_move);
        int result = move_ghost(board, ghost_index, cmd);
        
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
        
        // Advance to next move only if command was completed
        if (result == MOVE_COMPLETED) {
            ghost->current_move++;
        }
        
        // Small delay for game timing
        sleep_ms(board->tempo > 0 ? board->tempo : 100);
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
    
    // Start session thread (sends board updates to client)
    if (pthread_create(&ctx->session_thread, NULL, session_thread_func, ctx) != 0) {
        ctx->threads_running = false;
        return -1;
    }
    
    // Start pacman thread (reads commands from client)
    if (pthread_create(&ctx->pacman_thread, NULL, pacman_thread_func, ctx) != 0) {
        ctx->threads_running = false;
        pthread_join(ctx->session_thread, NULL);
        return -1;
    }
    
    // Start ghost threads
    for (int i = 0; i < ctx->n_ghost_threads; i++) {
        ghost_thread_data_t* data = malloc(sizeof(ghost_thread_data_t));
        if (!data) {
            // Cleanup on failure
            ctx->threads_running = false;
            pthread_join(ctx->pacman_thread, NULL);
            pthread_join(ctx->session_thread, NULL);
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
            pthread_join(ctx->session_thread, NULL);
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
    pthread_mutex_lock(&ctx->state_mutex);
    pthread_cond_broadcast(&ctx->display_cond);
    pthread_cond_broadcast(&ctx->game_cond);
    pthread_mutex_unlock(&ctx->state_mutex);
    
    // Wait for session thread
    pthread_join(ctx->session_thread, NULL);
    debug("[Main] Session thread joined\n");
    
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
