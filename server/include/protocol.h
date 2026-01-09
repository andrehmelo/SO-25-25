#ifndef PROTOCOL_H
#define PROTOCOL_H

// =============================================================================
// Protocol Constants for Client-Server Communication
// =============================================================================

#define MAX_PIPE_PATH_LENGTH 40

// Operation codes for messages
enum {
    OP_CODE_CONNECT = 1,     // Client -> Server: Request connection
    OP_CODE_DISCONNECT = 2,  // Client -> Server: Disconnect
    OP_CODE_PLAY = 3,        // Client -> Server: Send command (W/A/S/D)
    OP_CODE_BOARD = 4,       // Server -> Client: Board update
};

// =============================================================================
// Message Structures
// =============================================================================

// Connection request message (client -> server via registration FIFO)
// Format: (char)OP_CODE | (char[40])req_pipe_path | (char[40])notif_pipe_path
#define CONNECT_MSG_SIZE (1 + MAX_PIPE_PATH_LENGTH + MAX_PIPE_PATH_LENGTH)
#define CONNECT_REQUEST_SIZE CONNECT_MSG_SIZE

// Connection response message (server -> client via notification FIFO)
// Format: (char)OP_CODE | (char)result
#define CONNECT_RESPONSE_SIZE 2

// Play command message (client -> server via request FIFO)
// Format: (char)OP_CODE | (char)command
#define PLAY_MSG_SIZE 2

// Board update header (server -> client via notification FIFO)
// Format: (char)OP_CODE | (int)width | (int)height | (int)tempo | 
//         (int)victory | (int)game_over | (int)accumulated_points
#define BOARD_HEADER_SIZE (1 + 6 * sizeof(int))

#endif
