#include "leaderboard.h"
#include "display.h"
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

// =============================================================================
// Signal Handling
// =============================================================================

// Global flag for SIGUSR1 - must be volatile sig_atomic_t for async-safety
static volatile sig_atomic_t sigusr1_received = 0;

/**
 * Signal handler for SIGUSR1 - only sets a flag (async-safe).
 */
static void sigusr1_handler(int sig) {
    (void)sig;
    sigusr1_received = 1;
}

void setup_sigusr1_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // No SA_RESTART - we want to interrupt blocking calls
    
    if (sigaction(SIGUSR1, &sa, NULL) < 0) {
        debug("[Signal] Failed to setup SIGUSR1 handler: %s\n", strerror(errno));
    } else {
        debug("[Signal] SIGUSR1 handler installed\n");
    }
}

bool check_and_clear_sigusr1(void) {
    if (sigusr1_received) {
        sigusr1_received = 0;
        return true;
    }
    return false;
}

void block_sigusr1(void) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    
    if (pthread_sigmask(SIG_BLOCK, &mask, NULL) != 0) {
        debug("[Signal] Failed to block SIGUSR1: %s\n", strerror(errno));
    } else {
        debug("[Signal] SIGUSR1 blocked in this thread\n");
    }
}

// =============================================================================
// Leaderboard Management
// =============================================================================

int leaderboard_init(leaderboard_t* lb) {
    memset(lb->sessions, 0, sizeof(lb->sessions));
    lb->count = 0;
    
    if (pthread_mutex_init(&lb->mutex, NULL) != 0) {
        debug("[Leaderboard] Failed to init mutex\n");
        return -1;
    }
    
    debug("[Leaderboard] Initialized\n");
    return 0;
}

void leaderboard_destroy(leaderboard_t* lb) {
    pthread_mutex_destroy(&lb->mutex);
    debug("[Leaderboard] Destroyed\n");
}

int leaderboard_register(leaderboard_t* lb, const char* client_id) {
    pthread_mutex_lock(&lb->mutex);
    
    // Find empty slot
    int index = -1;
    for (int i = 0; i < MAX_ACTIVE_SESSIONS; i++) {
        if (!lb->sessions[i].active) {
            index = i;
            break;
        }
    }
    
    if (index < 0) {
        debug("[Leaderboard] No free slots for client: %s\n", client_id);
        pthread_mutex_unlock(&lb->mutex);
        return -1;
    }
    
    // Register session
    strncpy(lb->sessions[index].client_id, client_id, MAX_CLIENT_ID_LENGTH);
    lb->sessions[index].client_id[MAX_CLIENT_ID_LENGTH] = '\0';
    lb->sessions[index].points = 0;
    lb->sessions[index].active = true;
    lb->count++;
    
    debug("[Leaderboard] Registered client '%s' at index %d (total: %d)\n", 
          client_id, index, lb->count);
    
    pthread_mutex_unlock(&lb->mutex);
    return index;
}

void leaderboard_update_points(leaderboard_t* lb, int index, int points) {
    if (index < 0 || index >= MAX_ACTIVE_SESSIONS) return;
    
    pthread_mutex_lock(&lb->mutex);
    
    if (lb->sessions[index].active) {
        lb->sessions[index].points = points;
        debug("[Leaderboard] Updated client '%s' points to %d\n", 
              lb->sessions[index].client_id, points);
    }
    
    pthread_mutex_unlock(&lb->mutex);
}

void leaderboard_unregister(leaderboard_t* lb, int index) {
    if (index < 0 || index >= MAX_ACTIVE_SESSIONS) return;
    
    pthread_mutex_lock(&lb->mutex);
    
    if (lb->sessions[index].active) {
        debug("[Leaderboard] Unregistered client '%s'\n", lb->sessions[index].client_id);
        lb->sessions[index].active = false;
        lb->sessions[index].points = 0;
        lb->sessions[index].client_id[0] = '\0';
        lb->count--;
    }
    
    pthread_mutex_unlock(&lb->mutex);
}

/**
 * Comparison function for qsort - sort by points descending.
 */
static int compare_entries(const void* a, const void* b) {
    const session_entry_t* ea = (const session_entry_t*)a;
    const session_entry_t* eb = (const session_entry_t*)b;
    
    // Inactive entries go to the end
    if (!ea->active && !eb->active) return 0;
    if (!ea->active) return 1;
    if (!eb->active) return -1;
    
    // Sort by points descending
    return eb->points - ea->points;
}

int leaderboard_write_top5(leaderboard_t* lb, const char* filename) {
    pthread_mutex_lock(&lb->mutex);
    
    // Make a copy of sessions to sort
    session_entry_t sorted[MAX_ACTIVE_SESSIONS];
    memcpy(sorted, lb->sessions, sizeof(sorted));
    int total = lb->count;
    
    pthread_mutex_unlock(&lb->mutex);
    
    // Sort by points (descending)
    qsort(sorted, MAX_ACTIVE_SESSIONS, sizeof(session_entry_t), compare_entries);
    
    // Open file for writing
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        debug("[Leaderboard] Failed to create file '%s': %s\n", filename, strerror(errno));
        return -1;
    }
    
    // Write header
    char buffer[256];
    int len = snprintf(buffer, sizeof(buffer), 
                       "=== TOP 5 PACMANIST CLIENTS ===\n"
                       "Active sessions: %d\n\n"
                       "Rank | Client ID            | Points\n"
                       "-----+----------------------+--------\n",
                       total);
    if (write(fd, buffer, (size_t)len) < 0) {
        close(fd);
        return -1;
    }
    
    // Write top 5 (or less if fewer active)
    int written = 0;
    for (int i = 0; i < MAX_ACTIVE_SESSIONS && written < 5; i++) {
        if (sorted[i].active) {
            len = snprintf(buffer, sizeof(buffer), 
                          "  %d  | %-20s | %6d\n",
                          written + 1, sorted[i].client_id, sorted[i].points);
            if (write(fd, buffer, (size_t)len) < 0) {
                close(fd);
                return -1;
            }
            written++;
        }
    }
    
    if (written == 0) {
        len = snprintf(buffer, sizeof(buffer), "(No active clients)\n");
        if (write(fd, buffer, (size_t)len) < 0) {
            // ignore
        }
    }
    
    close(fd);
    debug("[Leaderboard] Wrote top %d clients to '%s'\n", written, filename);
    return 0;
}
