#include "parser.h"

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// Low-level I/O Primitives (using unget_char)
// =============================================================================

int unget_char(int fd) {
    // Seek back one byte to "unget" the last read character
    if (lseek(fd, -1, SEEK_CUR) < 0) {
        return -1;
    }
    return 0;
}

int read_char(int fd, char *c) {
    ssize_t r = read(fd, c, 1);
    if (r < 0) return -1;  // Error
    if (r == 0) return 0;  // EOF
    return 1;              // Success
}

int skip_spaces(int fd) {
    int count = 0;
    char c;
    
    while (read_char(fd, &c) == 1) {
        if (c == ' ' || c == '\t') {
            count++;
        } else {
            // Non-whitespace found - unget it and stop
            unget_char(fd);
            break;
        }
    }
    
    return count;
}

int read_uint(int fd, int *value) {
    int digits = 0;
    *value = 0;
    char c;
    
    while (read_char(fd, &c) == 1) {
        if (c >= '0' && c <= '9') {
            *value = (*value * 10) + (c - '0');
            digits++;
        } else {
            // Non-digit found - unget it and stop
            unget_char(fd);
            break;
        }
    }
    
    if (digits == 0) {
        return -1;  // No digits found
    }
    
    return digits;
}

int read_word(int fd, char *buffer, size_t max_len) {
    size_t len = 0;
    char c;
    
    while (len < max_len - 1 && read_char(fd, &c) == 1) {
        // Stop at whitespace, newline, or EOF
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            unget_char(fd);
            break;
        }
        buffer[len++] = c;
    }
    
    buffer[len] = '\0';
    
    if (len == 0) {
        return -1;  // No word found
    }
    
    return (int)len;
}

// =============================================================================
// Line-based Utilities
// =============================================================================

/**
 * Skip to the end of the current line (after newline or EOF).
 * Returns 0 on success, -1 on EOF/error.
 */
int skip_line(int fd) {
    char c;
    while (read(fd, &c, 1) == 1) {
        if (c == '\n') return 0;  // Found newline
    }
    return -1;  // EOF or error
}

int read_line(int fd, char *buffer, size_t max_len) {
    size_t i = 0;
    char c;
    
    while (i < max_len - 1) {
        ssize_t r = read(fd, &c, 1);
        if (r <= 0) {
            // EOF or error
            if (i == 0) return -1;
            break;
        }
        if (c == '\n') {
            break;
        }
        buffer[i++] = c;
    }
    
    buffer[i] = '\0';
    return (int)i;
}

/**
 * Reads the next non-comment, non-empty line.
 * Skips lines starting with '#' and empty lines.
 */
static int read_next_line(int fd, char *buffer, size_t max_len) {
    while (1) {
        int len = read_line(fd, buffer, max_len);
        if (len < 0) return -1;  // EOF
        if (len == 0) continue;  // Empty line
        if (buffer[0] == '#') continue;  // Comment
        return len;
    }
}

// =============================================================================
// Behavior File Parsing (.m and .p files)
// =============================================================================

int parse_behavior_file(int fd, int *passo, int *row, int *col,
                        char *commands, int *turns, int max_cmds, int *n_cmds) {
    char line[256];
    
    *passo = 0;
    *row = 0;
    *col = 0;
    *n_cmds = 0;
    
    // Parse PASSO line
    if (read_next_line(fd, line, sizeof(line)) < 0) return -1;
    if (strncmp(line, "PASSO ", 6) == 0) {
        *passo = atoi(line + 6);
    }
    
    // Parse POS line
    if (read_next_line(fd, line, sizeof(line)) < 0) return -1;
    if (strncmp(line, "POS ", 4) == 0) {
        char *ptr = line + 4;
        *row = atoi(ptr);
        // Find next number
        while (*ptr && *ptr != ' ') ptr++;
        while (*ptr == ' ') ptr++;
        *col = atoi(ptr);
    }
    
    // Parse movement commands
    while (*n_cmds < max_cmds) {
        int len = read_next_line(fd, line, sizeof(line));
        if (len < 0) break;  // EOF
        
        char cmd = line[0];
        int turn_count = 1;
        
        switch (cmd) {
            case 'W':
            case 'A':
            case 'S':
            case 'D':
            case 'R':
            case 'C':
            case 'G':  // Quicksave (Exercise 2)
            case 'Q':  // Quit game
                commands[*n_cmds] = cmd;
                turns[*n_cmds] = 1;
                (*n_cmds)++;
                break;
                
            case 'T':
                // T command has a number after it: "T 2"
                if (len > 2) {
                    turn_count = atoi(line + 2);
                }
                commands[*n_cmds] = 'T';
                turns[*n_cmds] = turn_count;
                (*n_cmds)++;
                break;
                
            default:
                // Unknown command, skip
                break;
        }
    }
    
    return 0;
}

// =============================================================================
// Level File Parsing (.lvl files)
// =============================================================================

int parse_level_file(int fd, int *rows, int *cols, int *tempo,
                     char *pac_file, char mon_files[][256], int max_mons, int *n_mons,
                     char *board, size_t max_board) {
    char line[512];
    
    *rows = 0;
    *cols = 0;
    *tempo = 0;
    pac_file[0] = '\0';
    *n_mons = 0;
    board[0] = '\0';
    
    size_t board_offset = 0;
    int reading_board = 0;
    
    while (1) {
        int len = read_line(fd, line, sizeof(line));
        if (len < 0) break;  // EOF
        
        // Skip empty lines before board
        if (len == 0 && !reading_board) continue;
        
        // Skip comments
        if (line[0] == '#') continue;
        
        // Check for commands
        if (!reading_board) {
            if (strncmp(line, "DIM ", 4) == 0) {
                char *ptr = line + 4;
                *rows = atoi(ptr);
                while (*ptr && *ptr != ' ') ptr++;
                while (*ptr == ' ') ptr++;
                *cols = atoi(ptr);
                continue;
            }
            
            if (strncmp(line, "TEMPO ", 6) == 0) {
                *tempo = atoi(line + 6);
                continue;
            }
            
            if (strncmp(line, "PAC ", 4) == 0) {
                // Copy filename (skip "PAC ")
                char *ptr = line + 4;
                while (*ptr == ' ') ptr++;  // Skip extra spaces
                strncpy(pac_file, ptr, 255);
                pac_file[255] = '\0';
                continue;
            }
            
            if (strncmp(line, "MON ", 4) == 0) {
                // Parse multiple filenames separated by spaces
                char *ptr = line + 4;
                while (*ptr && *n_mons < max_mons) {
                    while (*ptr == ' ') ptr++;  // Skip spaces
                    if (*ptr == '\0') break;
                    
                    // Read filename
                    char *start = ptr;
                    while (*ptr && *ptr != ' ') ptr++;
                    
                    size_t fname_len = (size_t)(ptr - start);
                    if (fname_len > 0 && fname_len < 256) {
                        strncpy(mon_files[*n_mons], start, fname_len);
                        mon_files[*n_mons][fname_len] = '\0';
                        (*n_mons)++;
                    }
                }
                continue;
            }
            
            // If line starts with X or o, it's the board
            if (line[0] == 'X' || line[0] == 'o' || line[0] == '@') {
                reading_board = 1;
            }
        }
        
        // Reading board matrix
        if (reading_board || line[0] == 'X' || line[0] == 'o' || line[0] == '@') {
            reading_board = 1;
            
            // Copy this line to board buffer
            size_t line_len = strlen(line);
            if (board_offset + line_len < max_board) {
                memcpy(board + board_offset, line, line_len);
                board_offset += line_len;
            }
        }
    }
    
    board[board_offset] = '\0';
    
    // Validate board size matches dimensions
    size_t expected_size = (size_t)(*rows) * (size_t)(*cols);
    if (board_offset < expected_size) {
        // Board data is smaller than declared dimensions
        return -1;
    }
    
    return 0;
}
