#include "api.h"
#include "protocol.h"
#include "debug.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>


// Session state - stores connection info
struct Session {
  int id;
  int req_pipe;           // File descriptor for request pipe (client -> server)
  int notif_pipe;         // File descriptor for notification pipe (server -> client)
  char req_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
  char notif_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
};

static struct Session session = {.id = -1, .req_pipe = -1, .notif_pipe = -1};


/**
 * Establishes a connection with the server.
 * 
 * Protocol:
 *   Request:  (char)OP_CODE=1 | (char[40])req_pipe_path | (char[40])notif_pipe_path
 *   Response: (char)OP_CODE=1 | (char)result (0 = success)
 * 
 * @return 0 on success, 1 on error
 */
int pacman_connect(char const *req_pipe_path, char const *notif_pipe_path, char const *server_pipe_path) {
  debug("pacman_connect: Starting connection...\n");
  debug("  req_pipe_path: %s\n", req_pipe_path);
  debug("  notif_pipe_path: %s\n", notif_pipe_path);
  debug("  server_pipe_path: %s\n", server_pipe_path);

  // Store pipe paths in session
  strncpy(session.req_pipe_path, req_pipe_path, MAX_PIPE_PATH_LENGTH);
  session.req_pipe_path[MAX_PIPE_PATH_LENGTH] = '\0';
  strncpy(session.notif_pipe_path, notif_pipe_path, MAX_PIPE_PATH_LENGTH);
  session.notif_pipe_path[MAX_PIPE_PATH_LENGTH] = '\0';

  // 1. Remove any existing FIFOs (in case of previous crash)
  unlink(req_pipe_path);
  unlink(notif_pipe_path);

  // 2. Create the client FIFOs
  if (mkfifo(req_pipe_path, 0640) != 0) {
    debug("pacman_connect: Failed to create request FIFO: %s\n", strerror(errno));
    return 1;
  }
  debug("pacman_connect: Created request FIFO\n");

  if (mkfifo(notif_pipe_path, 0640) != 0) {
    debug("pacman_connect: Failed to create notification FIFO: %s\n", strerror(errno));
    unlink(req_pipe_path);
    return 1;
  }
  debug("pacman_connect: Created notification FIFO\n");

  // 3. Open the server's registration FIFO for writing
  int server_pipe = open(server_pipe_path, O_WRONLY);
  if (server_pipe < 0) {
    debug("pacman_connect: Failed to open server FIFO: %s\n", strerror(errno));
    unlink(req_pipe_path);
    unlink(notif_pipe_path);
    return 1;
  }
  debug("pacman_connect: Opened server FIFO for writing\n");

  // 4. Build and send connection request message
  // Format: (char)OP_CODE | (char[40])req_pipe | (char[40])notif_pipe
  char message[1 + MAX_PIPE_PATH_LENGTH + MAX_PIPE_PATH_LENGTH];
  memset(message, 0, sizeof(message));
  
  message[0] = OP_CODE_CONNECT;
  
  // Copy pipe paths with null padding (fixed 40 bytes each)
  strncpy(&message[1], req_pipe_path, MAX_PIPE_PATH_LENGTH);
  strncpy(&message[1 + MAX_PIPE_PATH_LENGTH], notif_pipe_path, MAX_PIPE_PATH_LENGTH);

  ssize_t written = write(server_pipe, message, sizeof(message));
  if (written != sizeof(message)) {
    debug("pacman_connect: Failed to write connection request: %s\n", strerror(errno));
    close(server_pipe);
    unlink(req_pipe_path);
    unlink(notif_pipe_path);
    return 1;
  }
  debug("pacman_connect: Sent connection request (%zd bytes)\n", written);

  // Close server registration pipe (no longer needed)
  close(server_pipe);

  // 5. Open notification pipe for reading (to receive server response)
  // This will block until server opens it for writing
  session.notif_pipe = open(notif_pipe_path, O_RDONLY);
  if (session.notif_pipe < 0) {
    debug("pacman_connect: Failed to open notification FIFO: %s\n", strerror(errno));
    unlink(req_pipe_path);
    unlink(notif_pipe_path);
    return 1;
  }
  debug("pacman_connect: Opened notification FIFO for reading\n");

  // 6. Wait for connection response from server
  // Format: (char)OP_CODE | (char)result
  char response[2];
  ssize_t bytes_read = read(session.notif_pipe, response, sizeof(response));
  if (bytes_read != sizeof(response)) {
    debug("pacman_connect: Failed to read connection response: %s\n", strerror(errno));
    close(session.notif_pipe);
    session.notif_pipe = -1;
    unlink(req_pipe_path);
    unlink(notif_pipe_path);
    return 1;
  }

  if (response[0] != OP_CODE_CONNECT || response[1] != 0) {
    debug("pacman_connect: Server rejected connection (op=%d, result=%d)\n", 
          response[0], response[1]);
    close(session.notif_pipe);
    session.notif_pipe = -1;
    unlink(req_pipe_path);
    unlink(notif_pipe_path);
    return 1;
  }
  debug("pacman_connect: Server accepted connection\n");

  // 7. Open request pipe for writing (to send commands to server)
  session.req_pipe = open(req_pipe_path, O_WRONLY);
  if (session.req_pipe < 0) {
    debug("pacman_connect: Failed to open request FIFO for writing: %s\n", strerror(errno));
    close(session.notif_pipe);
    session.notif_pipe = -1;
    unlink(req_pipe_path);
    unlink(notif_pipe_path);
    return 1;
  }
  debug("pacman_connect: Opened request FIFO for writing\n");

  debug("pacman_connect: Connection established successfully!\n");
  return 0;
}


/**
 * Sends a play command to the server.
 * 
 * Protocol:
 *   Request: (char)OP_CODE=3 | (char)command
 *   No response expected
 */
void pacman_play(char command) {
  if (session.req_pipe < 0) {
    debug("pacman_play: Not connected to server\n");
    return;
  }

  // Build message: (char)OP_CODE | (char)command
  char message[2];
  message[0] = OP_CODE_PLAY;
  message[1] = command;

  ssize_t written = write(session.req_pipe, message, sizeof(message));
  if (written != sizeof(message)) {
    debug("pacman_play: Failed to send command '%c': %s\n", command, strerror(errno));
  } else {
    debug("pacman_play: Sent command '%c'\n", command);
  }
}


/**
 * Disconnects from the server.
 * 
 * Protocol:
 *   Request: (char)OP_CODE=2
 *   No response expected
 * 
 * @return 0 on success, 1 on error
 */
int pacman_disconnect() {
  debug("pacman_disconnect: Disconnecting...\n");

  // 1. Send disconnect message if connected
  if (session.req_pipe >= 0) {
    char message = OP_CODE_DISCONNECT;
    write(session.req_pipe, &message, sizeof(message));
    debug("pacman_disconnect: Sent disconnect message\n");
  }

  // 2. Close file descriptors
  if (session.req_pipe >= 0) {
    close(session.req_pipe);
    session.req_pipe = -1;
    debug("pacman_disconnect: Closed request pipe\n");
  }

  if (session.notif_pipe >= 0) {
    close(session.notif_pipe);
    session.notif_pipe = -1;
    debug("pacman_disconnect: Closed notification pipe\n");
  }

  // 3. Remove FIFOs from filesystem
  if (session.req_pipe_path[0] != '\0') {
    unlink(session.req_pipe_path);
    debug("pacman_disconnect: Removed request FIFO\n");
    session.req_pipe_path[0] = '\0';
  }

  if (session.notif_pipe_path[0] != '\0') {
    unlink(session.notif_pipe_path);
    debug("pacman_disconnect: Removed notification FIFO\n");
    session.notif_pipe_path[0] = '\0';
  }

  debug("pacman_disconnect: Disconnected successfully\n");
  return 0;
}


/**
 * Receives a board update from the server.
 * 
 * Protocol:
 *   Message: (char)OP_CODE=4 | (int)width | (int)height | (int)tempo | 
 *            (int)victory | (int)game_over | (int)accumulated_points | 
 *            (char[width*height])board_data
 * 
 * @return Board struct with updated data (data=NULL on error or disconnect)
 */
Board receive_board_update(void) {
  Board board = {0};
  board.data = NULL;

  if (session.notif_pipe < 0) {
    debug("receive_board_update: Not connected to server\n");
    return board;
  }

  // 1. Read OP_CODE
  char op_code;
  ssize_t bytes_read = read(session.notif_pipe, &op_code, sizeof(op_code));
  if (bytes_read <= 0) {
    debug("receive_board_update: Connection closed or error (read=%zd)\n", bytes_read);
    return board;
  }

  if (op_code != OP_CODE_BOARD) {
    debug("receive_board_update: Unexpected OP_CODE: %d\n", op_code);
    return board;
  }

  // 2. Read fixed-size header: width, height, tempo, victory, game_over, points
  int header[6];
  bytes_read = read(session.notif_pipe, header, sizeof(header));
  if (bytes_read != sizeof(header)) {
    debug("receive_board_update: Failed to read header (read=%zd, expected=%zu)\n", 
          bytes_read, sizeof(header));
    return board;
  }

  board.width = header[0];
  board.height = header[1];
  board.tempo = header[2];
  board.victory = header[3];
  board.game_over = header[4];
  board.accumulated_points = header[5];

  debug("receive_board_update: Got header - %dx%d, tempo=%d, victory=%d, game_over=%d, points=%d\n",
        board.width, board.height, board.tempo, board.victory, board.game_over, board.accumulated_points);

  // 3. Read board data
  int board_size = board.width * board.height;
  if (board_size <= 0 || board_size > 10000) {  // Sanity check
    debug("receive_board_update: Invalid board size: %d\n", board_size);
    return board;
  }

  board.data = malloc((size_t)board_size + 1);
  if (board.data == NULL) {
    debug("receive_board_update: Failed to allocate board data\n");
    return board;
  }

  bytes_read = read(session.notif_pipe, board.data, (size_t)board_size);
  if (bytes_read != board_size) {
    debug("receive_board_update: Failed to read board data (read=%zd, expected=%d)\n", 
          bytes_read, board_size);
    free(board.data);
    board.data = NULL;
    return board;
  }
  board.data[board_size] = '\0';  // Null terminate for safety

  debug("receive_board_update: Received board data (%d bytes)\n", board_size);
  return board;
}