#include "session.h"
#include "protocol.h"
#include "board.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

// Debug macro (uses the debug function from display.c if available)
extern void debug(const char* format, ...);

// =============================================================================
// Session Initialization and Cleanup
// =============================================================================

void init_session(client_session_t* session) {
    session->client_id = -1;
    session->req_pipe_fd = -1;
    session->notif_pipe_fd = -1;
    session->req_pipe_path[0] = '\0';
    session->notif_pipe_path[0] = '\0';
    session->active = false;
    session->accumulated_points = 0;
}

void cleanup_session(client_session_t* session) {
    debug("[Session] Cleaning up session (client_id=%d)\n", session->client_id);
    
    if (session->req_pipe_fd >= 0) {
        close(session->req_pipe_fd);
        session->req_pipe_fd = -1;
    }
    
    if (session->notif_pipe_fd >= 0) {
        close(session->notif_pipe_fd);
        session->notif_pipe_fd = -1;
    }
    
    session->active = false;
    session->req_pipe_path[0] = '\0';
    session->notif_pipe_path[0] = '\0';
}

// =============================================================================
// Connection Handling
// =============================================================================

int read_connect_request(int server_fd, client_session_t* session) {
    // Read connection message:
    // (char)OP_CODE | (char[40])req_pipe_path | (char[40])notif_pipe_path
    char buffer[CONNECT_MSG_SIZE];
    
    ssize_t bytes_read = read(server_fd, buffer, sizeof(buffer));
    if (bytes_read <= 0) {
        if (bytes_read == 0) {
            debug("[Session] Registration FIFO closed (no more clients)\n");
        } else {
            debug("[Session] Error reading from registration FIFO: %s\n", strerror(errno));
        }
        return -1;
    }
    
    if (bytes_read != sizeof(buffer)) {
        debug("[Session] Incomplete connection message (got %zd, expected %zu)\n", 
              bytes_read, sizeof(buffer));
        return -1;
    }
    
    // Verify OP_CODE
    if (buffer[0] != OP_CODE_CONNECT) {
        debug("[Session] Invalid OP_CODE in connection message: %d\n", buffer[0]);
        return -1;
    }
    
    // Extract pipe paths (fixed 40 bytes each)
    memcpy(session->req_pipe_path, &buffer[1], MAX_PIPE_PATH_LENGTH);
    session->req_pipe_path[MAX_PIPE_PATH_LENGTH] = '\0';
    
    memcpy(session->notif_pipe_path, &buffer[1 + MAX_PIPE_PATH_LENGTH], MAX_PIPE_PATH_LENGTH);
    session->notif_pipe_path[MAX_PIPE_PATH_LENGTH] = '\0';
    
    debug("[Session] Connection request received:\n");
    debug("  req_pipe: %s\n", session->req_pipe_path);
    debug("  notif_pipe: %s\n", session->notif_pipe_path);
    
    return 0;
}

int accept_connection(client_session_t* session) {
    // 1. Open notification pipe for writing (client is waiting to read)
    session->notif_pipe_fd = open(session->notif_pipe_path, O_WRONLY);
    if (session->notif_pipe_fd < 0) {
        debug("[Session] Failed to open notification FIFO: %s\n", strerror(errno));
        return -1;
    }
    debug("[Session] Opened notification FIFO for writing\n");
    
    // 2. Send success response BEFORE opening request pipe
    //    (client opens req_pipe for writing only after receiving response)
    if (send_connect_response(session, 0) < 0) {
        close(session->notif_pipe_fd);
        session->notif_pipe_fd = -1;
        return -1;
    }
    
    // 3. Open request pipe for reading (now client has opened it for writing)
    session->req_pipe_fd = open(session->req_pipe_path, O_RDONLY);
    if (session->req_pipe_fd < 0) {
        debug("[Session] Failed to open request FIFO: %s\n", strerror(errno));
        close(session->notif_pipe_fd);
        session->notif_pipe_fd = -1;
        return -1;
    }
    debug("[Session] Opened request FIFO for reading\n");
    
    session->active = true;
    debug("[Session] Connection accepted successfully\n");
    
    return 0;
}

int send_connect_response(client_session_t* session, char result) {
    // Response: (char)OP_CODE | (char)result
    char response[CONNECT_RESPONSE_SIZE];
    response[0] = OP_CODE_CONNECT;
    response[1] = result;
    
    ssize_t written = write(session->notif_pipe_fd, response, sizeof(response));
    if (written != sizeof(response)) {
        debug("[Session] Failed to send connection response: %s\n", strerror(errno));
        return -1;
    }
    
    debug("[Session] Sent connection response (result=%d)\n", result);
    return 0;
}

// =============================================================================
// Board Updates
// =============================================================================

int send_board_update(client_session_t* session, board_t* board, int victory, int game_over) {
    if (!session->active || session->notif_pipe_fd < 0) {
        return -1;
    }
    
    int width = board->width;
    int height = board->height;
    int board_size = width * height;
    
    // Build message:
    // (char)OP_CODE | (int)width | (int)height | (int)tempo | 
    // (int)victory | (int)game_over | (int)points | (char[w*h])data
    
    size_t msg_size = 1 + 6 * sizeof(int) + (size_t)board_size;
    char* message = malloc(msg_size);
    if (!message) {
        debug("[Session] Failed to allocate board message\n");
        return -1;
    }
    
    size_t offset = 0;
    
    // OP_CODE
    message[offset++] = OP_CODE_BOARD;
    
    // Header integers
    int header[6] = {
        width,
        height,
        board->tempo,
        victory,
        game_over,
        session->accumulated_points
    };
    memcpy(&message[offset], header, sizeof(header));
    offset += sizeof(header);
    
    // Board data - serialize from board positions
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            char content = board->board[idx].content;
            
            // Map content to display characters
            // 'W' -> '#' (wall)
            // 'P' -> 'C' (pacman)
            // 'M' -> 'M' (monster)
            // ' ' with dot -> 'o' (dot)
            // '@' or portal -> '@'
            if (content == 'W') {
                message[offset++] = '#';
            } else if (content == 'P') {
                message[offset++] = 'C';
            } else if (content == 'M') {
                message[offset++] = 'M';
            } else if (board->board[idx].has_portal) {
                message[offset++] = '@';
            } else if (board->board[idx].has_dot) {
                message[offset++] = 'o';
            } else {
                message[offset++] = ' ';
            }
        }
    }
    
    // Send message
    ssize_t written = write(session->notif_pipe_fd, message, msg_size);
    free(message);
    
    if (written != (ssize_t)msg_size) {
        if (written < 0) {
            debug("[Session] Failed to send board update: %s\n", strerror(errno));
        } else {
            debug("[Session] Partial write sending board update\n");
        }
        return -1;
    }
    
    debug("[Session] Sent board update (%zu bytes)\n", msg_size);
    return 0;
}

// =============================================================================
// Command Reading
// =============================================================================

int read_client_command(client_session_t* session, char* command) {
    if (!session->active || session->req_pipe_fd < 0) {
        return -1;
    }
    
    // Read command message: (char)OP_CODE | (char)command
    char buffer[2];
    
    ssize_t bytes_read = read(session->req_pipe_fd, buffer, sizeof(buffer));
    
    if (bytes_read == 0) {
        // Client closed the pipe - disconnected
        debug("[Session] Client disconnected (pipe closed)\n");
        return -1;
    }
    
    if (bytes_read < 0) {
        debug("[Session] Error reading command: %s\n", strerror(errno));
        return -1;
    }
    
    if (bytes_read == 1 && buffer[0] == OP_CODE_DISCONNECT) {
        // Disconnect request
        debug("[Session] Client requested disconnect\n");
        return -2;
    }
    
    if (bytes_read != 2) {
        debug("[Session] Invalid command message size: %zd\n", bytes_read);
        return -1;
    }
    
    if (buffer[0] != OP_CODE_PLAY) {
        if (buffer[0] == OP_CODE_DISCONNECT) {
            debug("[Session] Client requested disconnect\n");
            return -2;
        }
        debug("[Session] Unexpected OP_CODE: %d\n", buffer[0]);
        return -1;
    }
    
    *command = buffer[1];
    debug("[Session] Received command: %c\n", *command);
    
    return 0;
}
