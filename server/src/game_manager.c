#include "game_manager.h"
#include "board.h"
#include "display.h"
#include "threads.h"
#include "protocol.h"
#include "leaderboard.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

// Game result codes
#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define PACMAN_DIED 3
#define CLIENT_DISCONNECTED 4

// Forward declaration
static int play_level_threaded(board_t* game_board, client_session_t* session,
                               leaderboard_t* lb, int lb_index);

/**
 * Extract client ID from pipe path.
 * E.g., "/tmp/1_request" -> "1"
 */
static void extract_client_id(const char* pipe_path, char* client_id, size_t max_len) {
    // Find last '/' in path
    const char* filename = strrchr(pipe_path, '/');
    if (filename) {
        filename++;  // Skip the '/'
    } else {
        filename = pipe_path;
    }
    
    // Copy until '_' or end
    size_t i = 0;
    while (filename[i] && filename[i] != '_' && i < max_len - 1) {
        client_id[i] = filename[i];
        i++;
    }
    client_id[i] = '\0';
}

// =============================================================================
// Game Manager Thread
// =============================================================================

/**
 * Handle a single client session - play all levels until game over or disconnect.
 */
static void handle_client_session(game_manager_t* manager, connection_request_t* request) {
    debug("[Manager %d] Handling new client session\n", manager->id);
    debug("[Manager %d] req_pipe: %s\n", manager->id, request->req_pipe_path);
    debug("[Manager %d] notif_pipe: %s\n", manager->id, request->notif_pipe_path);
    
    // Extract client ID from pipe path for leaderboard
    char client_id[MAX_CLIENT_ID_LENGTH + 1];
    extract_client_id(request->req_pipe_path, client_id, sizeof(client_id));
    
    // Register in leaderboard
    int lb_index = -1;
    if (manager->leaderboard) {
        lb_index = leaderboard_register(manager->leaderboard, client_id);
    }
    
    // Initialize session
    client_session_t session;
    init_session(&session);
    
    // Copy pipe paths from request
    strncpy(session.req_pipe_path, request->req_pipe_path, MAX_PIPE_PATH_LENGTH);
    session.req_pipe_path[MAX_PIPE_PATH_LENGTH] = '\0';
    strncpy(session.notif_pipe_path, request->notif_pipe_path, MAX_PIPE_PATH_LENGTH);
    session.notif_pipe_path[MAX_PIPE_PATH_LENGTH] = '\0';
    
    // Accept the connection (open client FIFOs and send response)
    if (accept_connection(&session) < 0) {
        debug("[Manager %d] Failed to accept connection\n", manager->id);
        cleanup_session(&session);
        if (manager->leaderboard && lb_index >= 0) {
            leaderboard_unregister(manager->leaderboard, lb_index);
        }
        return;
    }
    
    debug("[Manager %d] Client connected successfully!\n", manager->id);
    
    // Play all levels with this client
    int current_level = 0;
    bool game_over = false;
    board_t game_board = {0};
    
    while (!game_over && current_level < manager->n_levels && manager->running) {
        // Load level from file
        if (load_level_from_file(&game_board, manager->level_dir, 
                                 manager->level_files[current_level], 
                                 session.accumulated_points) < 0) {
            debug("[Manager %d] Error loading level: %s\n", 
                  manager->id, manager->level_files[current_level]);
            break;
        }
        
        debug("[Manager %d] Loaded level %d: %s\n", 
              manager->id, current_level, manager->level_files[current_level]);
        print_board(&game_board);
        
        // Play the level (passes leaderboard for real-time updates)
        int result = play_level_threaded(&game_board, &session, 
                                         manager->leaderboard, lb_index);
        
        if (result == NEXT_LEVEL) {
            session.accumulated_points = game_board.pacmans[0].points;
            debug("[Manager %d] Level completed! Points: %d\n", 
                  manager->id, session.accumulated_points);
            
            // Update leaderboard with current points
            if (manager->leaderboard && lb_index >= 0) {
                leaderboard_update_points(manager->leaderboard, lb_index, 
                                         session.accumulated_points);
            }
            
            current_level++;
            
            // Check if this was the last level
            if (current_level >= manager->n_levels) {
                debug("[Manager %d] All levels completed!\n", manager->id);
                game_over = true;
            }
        }
        else if (result == QUIT_GAME) {
            debug("[Manager %d] Client quit the game\n", manager->id);
            game_over = true;
        }
        else if (result == CLIENT_DISCONNECTED) {
            debug("[Manager %d] Client disconnected\n", manager->id);
            game_over = true;
        }
        else if (result == PACMAN_DIED) {
            debug("[Manager %d] Pacman died - game over\n", manager->id);
            // Update final score before game over
            if (manager->leaderboard && lb_index >= 0) {
                leaderboard_update_points(manager->leaderboard, lb_index, 
                                         session.accumulated_points);
            }
            game_over = true;
        }
        
        unload_level(&game_board);
    }
    
    // Cleanup session
    debug("[Manager %d] Session ended. Final score: %d\n", 
          manager->id, session.accumulated_points);
    cleanup_session(&session);
    
    // Unregister from leaderboard
    if (manager->leaderboard && lb_index >= 0) {
        leaderboard_unregister(manager->leaderboard, lb_index);
    }
}

/**
 * Game manager thread function.
 * Consumes connection requests from buffer and handles sessions.
 */
void* game_manager_thread_func(void* arg) {
    game_manager_t* manager = (game_manager_t*)arg;
    
    // Block SIGUSR1 in this thread - only host thread should receive it
    block_sigusr1();
    
    debug("[Manager %d] Thread started\n", manager->id);
    
    while (manager->running) {
        // Wait for a connection request from the buffer
        connection_request_t request;
        
        debug("[Manager %d] Waiting for connection request...\n", manager->id);
        
        if (pc_buffer_remove(manager->request_buffer, &request) < 0) {
            // Buffer shutdown or error
            debug("[Manager %d] Buffer remove failed, exiting\n", manager->id);
            break;
        }
        
        // Handle the client session
        manager->active = true;
        handle_client_session(manager, &request);
        manager->active = false;
    }
    
    debug("[Manager %d] Thread exiting\n", manager->id);
    return NULL;
}

// =============================================================================
// Play Level (moved from game.c to be accessible here)
// =============================================================================

/**
 * Play one level using threads with a connected client.
 * Returns: NEXT_LEVEL, QUIT_GAME, PACMAN_DIED, or CLIENT_DISCONNECTED
 */
static int play_level_threaded(board_t* game_board, client_session_t* session,
                               leaderboard_t* lb, int lb_index) {
    game_context_t ctx;
    
    if (init_game_context(&ctx, game_board, session) < 0) {
        debug("Error: Failed to initialize game context\n");
        return QUIT_GAME;
    }
    
    // Set leaderboard for real-time score updates
    set_game_leaderboard(&ctx, lb, lb_index);
    
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
        case GAME_CLIENT_DISCONNECTED:
            return CLIENT_DISCONNECTED;
        case GAME_OVER:
            return pacman_died ? PACMAN_DIED : QUIT_GAME;
        case GAME_WON:
            return NEXT_LEVEL;
        default:
            return QUIT_GAME;
    }
}

// =============================================================================
// Server Context Management
// =============================================================================

int server_init(server_context_t* ctx, int max_games, const char* level_dir, 
                const char* server_fifo_path, char** level_files, int n_levels) {
    
    ctx->max_games = max_games;
    ctx->level_dir = level_dir;
    ctx->server_fifo_path = server_fifo_path;
    ctx->level_files = level_files;
    ctx->n_levels = n_levels;
    ctx->running = false;
    ctx->server_fd = -1;
    ctx->n_managers = 0;
    
    // Limit max_games to our maximum
    if (max_games > MAX_CONCURRENT_GAMES) {
        debug("[Server] max_games capped to %d\n", MAX_CONCURRENT_GAMES);
        ctx->max_games = MAX_CONCURRENT_GAMES;
    }
    
    // Initialize producer-consumer buffer
    if (pc_buffer_init(&ctx->request_buffer) < 0) {
        debug("[Server] Failed to initialize request buffer\n");
        return -1;
    }
    
    // Initialize leaderboard
    if (leaderboard_init(&ctx->leaderboard) < 0) {
        debug("[Server] Failed to initialize leaderboard\n");
        pc_buffer_destroy(&ctx->request_buffer);
        return -1;
    }
    
    // Initialize manager structures
    for (int i = 0; i < ctx->max_games; i++) {
        ctx->managers[i].id = i;
        ctx->managers[i].request_buffer = &ctx->request_buffer;
        ctx->managers[i].level_files = level_files;
        ctx->managers[i].n_levels = n_levels;
        ctx->managers[i].level_dir = level_dir;
        ctx->managers[i].leaderboard = &ctx->leaderboard;  // Share leaderboard
        ctx->managers[i].active = false;
        ctx->managers[i].running = false;
    }
    
    debug("[Server] Initialized with max_games=%d\n", ctx->max_games);
    return 0;
}

int server_start_managers(server_context_t* ctx) {
    debug("[Server] Starting %d game manager threads\n", ctx->max_games);
    
    for (int i = 0; i < ctx->max_games; i++) {
        ctx->managers[i].running = true;
        
        if (pthread_create(&ctx->managers[i].thread, NULL, 
                          game_manager_thread_func, &ctx->managers[i]) != 0) {
            debug("[Server] Failed to create manager thread %d: %s\n", 
                  i, strerror(errno));
            
            // Stop already created threads
            for (int j = 0; j < i; j++) {
                ctx->managers[j].running = false;
            }
            pc_buffer_shutdown(&ctx->request_buffer);
            return -1;
        }
        
        ctx->n_managers++;
    }
    
    debug("[Server] All %d manager threads started\n", ctx->n_managers);
    return 0;
}

void server_run_host(server_context_t* ctx) {
    ctx->running = true;
    
    debug("[Host] Starting host thread (reading from %s)\n", ctx->server_fifo_path);
    
    // Remove any existing server FIFO and create a new one
    unlink(ctx->server_fifo_path);
    if (mkfifo(ctx->server_fifo_path, 0640) != 0) {
        debug("[Host] Failed to create server FIFO: %s\n", strerror(errno));
        return;
    }
    debug("[Host] Created server FIFO: %s\n", ctx->server_fifo_path);
    
    while (ctx->running) {
        // Check if SIGUSR1 was received - write top 5 file immediately
        if (check_and_clear_sigusr1()) {
            debug("[Host] SIGUSR1 received! Writing top 5 leaderboard...\n");
            if (leaderboard_write_top5(&ctx->leaderboard, "top5.txt") < 0) {
                debug("[Host] Failed to write leaderboard file\n");
            } else {
                debug("[Host] Leaderboard written to top5.txt\n");
            }
        }
        
        debug("\n[Host] === Waiting for client connection ===\n");
        
        // Open server FIFO for reading (blocks until a client connects)
        ctx->server_fd = open(ctx->server_fifo_path, O_RDONLY);
        if (ctx->server_fd < 0) {
            if (errno == EINTR) {
                // Interrupted by signal - check for SIGUSR1
                continue;
            }
            debug("[Host] Failed to open server FIFO: %s\n", strerror(errno));
            break;
        }
        debug("[Host] Server FIFO opened for reading\n");
        
        // Read connection request from client
        // Format: (char)OP_CODE | (char[40])req_pipe | (char[40])notif_pipe
        char buffer[CONNECT_REQUEST_SIZE];
        ssize_t bytes_read = read(ctx->server_fd, buffer, sizeof(buffer));
        
        // Close server FIFO (we'll reopen it for next client)
        close(ctx->server_fd);
        ctx->server_fd = -1;
        
        // Check for signal interruption
        if (bytes_read < 0 && errno == EINTR) {
            continue;  // Go back to check for SIGUSR1
        }
        
        if (bytes_read <= 0) {
            debug("[Host] Failed to read from server FIFO\n");
            continue;
        }
        
        if (bytes_read != sizeof(buffer)) {
            debug("[Host] Incomplete connection request: %zd bytes\n", bytes_read);
            continue;
        }
        
        // Verify OP_CODE
        if (buffer[0] != OP_CODE_CONNECT) {
            debug("[Host] Invalid OP_CODE: %d\n", buffer[0]);
            continue;
        }
        
        // Extract pipe paths
        connection_request_t request;
        memcpy(request.req_pipe_path, &buffer[1], MAX_PIPE_PATH_LENGTH);
        request.req_pipe_path[MAX_PIPE_PATH_LENGTH] = '\0';
        memcpy(request.notif_pipe_path, &buffer[1 + MAX_PIPE_PATH_LENGTH], MAX_PIPE_PATH_LENGTH);
        request.notif_pipe_path[MAX_PIPE_PATH_LENGTH] = '\0';
        
        debug("[Host] Received connection request:\n");
        debug("[Host]   req_pipe: %s\n", request.req_pipe_path);
        debug("[Host]   notif_pipe: %s\n", request.notif_pipe_path);
        
        // Insert into buffer (will block if buffer is full)
        if (pc_buffer_insert(&ctx->request_buffer, &request) < 0) {
            debug("[Host] Failed to insert request into buffer\n");
            continue;
        }
        
        debug("[Host] Request inserted into buffer\n");
    }
    
    debug("[Host] Host thread exiting\n");
}

void server_shutdown(server_context_t* ctx) {
    debug("[Server] Shutting down...\n");
    
    ctx->running = false;
    
    // Close server FIFO to unblock host
    if (ctx->server_fd >= 0) {
        close(ctx->server_fd);
        ctx->server_fd = -1;
    }
    
    // Shutdown buffer to wake up all waiting managers
    pc_buffer_shutdown(&ctx->request_buffer);
    
    // Signal managers to stop
    for (int i = 0; i < ctx->n_managers; i++) {
        ctx->managers[i].running = false;
    }
    
    // Wait for all manager threads to finish
    for (int i = 0; i < ctx->n_managers; i++) {
        pthread_join(ctx->managers[i].thread, NULL);
        debug("[Server] Manager %d joined\n", i);
    }
    
    debug("[Server] All threads stopped\n");
}

void server_cleanup(server_context_t* ctx) {
    pc_buffer_destroy(&ctx->request_buffer);
    leaderboard_destroy(&ctx->leaderboard);
    unlink(ctx->server_fifo_path);
    debug("[Server] Cleanup complete\n");
}
