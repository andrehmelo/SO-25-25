#ifndef LEADERBOARD_H
#define LEADERBOARD_H

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>

// Maximum number of active sessions to track
#define MAX_ACTIVE_SESSIONS 64

// Maximum length of client ID
#define MAX_CLIENT_ID_LENGTH 40

/**
 * Entry for tracking active client sessions.
 */
typedef struct {
    char client_id[MAX_CLIENT_ID_LENGTH + 1];  // Client identifier from pipe path
    int points;                                  // Current accumulated points
    bool active;                                 // Is this session active?
} session_entry_t;

/**
 * Leaderboard structure for tracking all active sessions.
 * Protected by mutex for thread-safe access.
 */
typedef struct {
    session_entry_t sessions[MAX_ACTIVE_SESSIONS];
    int count;
    pthread_mutex_t mutex;
} leaderboard_t;

/**
 * Initialize the leaderboard.
 */
int leaderboard_init(leaderboard_t* lb);

/**
 * Destroy the leaderboard.
 */
void leaderboard_destroy(leaderboard_t* lb);

/**
 * Register a new active session.
 * @param lb        The leaderboard.
 * @param client_id The client identifier (extracted from pipe path).
 * @return          Index of the session, or -1 on error.
 */
int leaderboard_register(leaderboard_t* lb, const char* client_id);

/**
 * Update points for a session.
 * @param lb        The leaderboard.
 * @param index     Session index returned by leaderboard_register.
 * @param points    New points value.
 */
void leaderboard_update_points(leaderboard_t* lb, int index, int points);

/**
 * Unregister a session (client disconnected).
 * @param lb        The leaderboard.
 * @param index     Session index to remove.
 */
void leaderboard_unregister(leaderboard_t* lb, int index);

/**
 * Write top 5 clients to a file.
 * @param lb        The leaderboard.
 * @param filename  Output filename.
 * @return          0 on success, -1 on error.
 */
int leaderboard_write_top5(leaderboard_t* lb, const char* filename);

// =============================================================================
// Signal Handling
// =============================================================================

/**
 * Setup SIGUSR1 handler.
 * Only the host thread should receive this signal.
 */
void setup_sigusr1_handler(void);

/**
 * Check if SIGUSR1 was received and clear the flag.
 * @return true if signal was received, false otherwise.
 */
bool check_and_clear_sigusr1(void);

/**
 * Block SIGUSR1 in the current thread.
 * Should be called by all worker threads.
 */
void block_sigusr1(void);

#endif
