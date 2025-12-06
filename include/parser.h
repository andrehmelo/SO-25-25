#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

// =============================================================================
// Low-level I/O Primitives (using unget_char - no char* next needed)
// =============================================================================

/**
 * "Ungets" a character by seeking back one byte.
 * Use this to return a character to the file descriptor stream.
 * @param fd    File descriptor.
 * @return      0 on success, -1 on error.
 */
int unget_char(int fd);

/**
 * Reads a single character from the file descriptor.
 * @param fd    File descriptor.
 * @param c     Output: the character read.
 * @return      1 on success, 0 on EOF, -1 on error.
 */
int read_char(int fd, char *c);

/**
 * Skips whitespace characters (spaces and tabs, NOT newlines).
 * Stops at the first non-whitespace character (which is ungotten).
 * @param fd    File descriptor.
 * @return      Number of characters skipped.
 */
int skip_spaces(int fd);

/**
 * Reads an unsigned integer from the file descriptor.
 * Stops at the first non-digit character (which is ungotten).
 * @param fd    File descriptor.
 * @param value Output: the parsed integer value.
 * @return      Number of digits read, or -1 on error (no digits found).
 */
int read_uint(int fd, int *value);

/**
 * Reads a word (sequence of non-whitespace characters) from the file descriptor.
 * Stops at whitespace or newline (which is ungotten).
 * @param fd        File descriptor.
 * @param buffer    Output buffer for the word.
 * @param max_len   Maximum buffer size.
 * @return          Length of the word, or -1 on error.
 */
int read_word(int fd, char *buffer, size_t max_len);

// =============================================================================
// Line-based Utilities
// =============================================================================

/**
 * Skips a line (reads until newline or EOF).
 */
void skip_line(int fd);

/**
 * Reads a line into buffer (without the newline).
 * @return Number of characters read, or -1 on error/EOF.
 */
int read_line(int fd, char *buffer, size_t max_len);

// =============================================================================
// Behavior File Parsing (.m and .p files)
// =============================================================================

/**
 * Parses a behavior file (.m or .p).
 * Reads PASSO, POS, and movement commands.
 * 
 * @param fd        File descriptor (already opened).
 * @param passo     Output: steps to wait between moves.
 * @param row       Output: starting row position.
 * @param col       Output: starting column position.
 * @param commands  Output: array of command characters (W/A/S/D/R/T/C).
 * @param turns     Output: array of turn counts (for T command).
 * @param max_cmds  Maximum number of commands to read.
 * @param n_cmds    Output: number of commands read.
 * @return          0 on success, -1 on error.
 */
int parse_behavior_file(int fd, int *passo, int *row, int *col,
                        char *commands, int *turns, int max_cmds, int *n_cmds);

// =============================================================================
// Level File Parsing (.lvl files)
// =============================================================================

/**
 * Parses a level file (.lvl).
 * Reads DIM, TEMPO, PAC (optional), MON, and the board matrix.
 * 
 * @param fd            File descriptor (already opened).
 * @param rows          Output: number of rows.
 * @param cols          Output: number of columns.
 * @param tempo         Output: milliseconds per game tick.
 * @param pac_file      Output: pacman file name (empty string if not present).
 * @param mon_files     Output: array of monster file names.
 * @param max_mons      Maximum number of monster files.
 * @param n_mons        Output: number of monster files.
 * @param board         Output: board matrix as string (row by row, no newlines).
 * @param max_board     Maximum size of board buffer.
 * @return              0 on success, -1 on error.
 */
int parse_level_file(int fd, int *rows, int *cols, int *tempo,
                     char *pac_file, char mon_files[][256], int max_mons, int *n_mons,
                     char *board, size_t max_board);

#endif // PARSER_H
