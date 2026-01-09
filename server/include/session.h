#ifndef SESSION_H
#define SESSION_H

#include <stdbool.h>
#include "protocol.h"
#include "board.h"

// =============================================================================
// Client Session Management (Exercise 1)
// =============================================================================

// Represents a connected client session
typedef struct {
    int client_id;                              // Client identifier (from connection)
    int req_pipe_fd;                            // FD for reading commands from client
    int notif_pipe_fd;                          // FD for sending board updates to client
    char req_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
    char notif_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
    bool active;                                // Session is active
    int accumulated_points;                     // Points accumulated in this session
} client_session_t;

// =============================================================================
// Session Functions
// =============================================================================

/**
 * Initializes a client session structure.
 */
void init_session(client_session_t* session);

/**
 * Cleans up a client session, closing FIFOs.
 */
void cleanup_session(client_session_t* session);

/**
 * Reads a connection request from the server's registration FIFO.
 * 
 * @param server_fd     File descriptor of the registration FIFO
 * @param session       Output: session structure to populate with pipe paths
 * @return              0 on success, -1 on error
 */
int read_connect_request(int server_fd, client_session_t* session);

/**
 * Opens the client's FIFOs and sends connection response.
 * 
 * @param session       Session with pipe paths to open
 * @return              0 on success, -1 on error
 */
int accept_connection(client_session_t* session);

/**
 * Sends a connection response to the client.
 * 
 * @param session       Active session
 * @param result        0 for success, non-zero for error
 * @return              0 on success, -1 on error
 */
int send_connect_response(client_session_t* session, char result);

/**
 * Sends board update to the client.
 * 
 * @param session       Active session
 * @param board         Game board to send
 * @param victory       1 if player won, 0 otherwise
 * @param game_over     1 if game is over, 0 otherwise
 * @return              0 on success, -1 on error (client disconnected)
 */
int send_board_update(client_session_t* session, board_t* board, int victory, int game_over);

/**
 * Reads a command from the client.
 * 
 * @param session       Active session
 * @param command       Output: command character (W/A/S/D/Q)
 * @return              0 on success, -1 on error/disconnect, -2 on disconnect request
 */
int read_client_command(client_session_t* session, char* command);

#endif
