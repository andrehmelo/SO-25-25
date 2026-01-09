#ifndef PC_BUFFER_H
#define PC_BUFFER_H

#include "session.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

// Maximum size of the producer-consumer buffer
#define PC_BUFFER_SIZE 16

/**
 * Connection request structure.
 * Contains the pipe paths received from a client connection request.
 */
typedef struct {
    char req_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
    char notif_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
} connection_request_t;

/**
 * Producer-Consumer buffer for connection requests.
 * Uses semaphores for synchronization as required by the project.
 */
typedef struct {
    connection_request_t buffer[PC_BUFFER_SIZE];
    int head;           // Index for next insert (producer)
    int tail;           // Index for next remove (consumer)
    
    pthread_mutex_t mutex;      // Protects buffer access
    sem_t sem_empty;            // Counts empty slots
    sem_t sem_full;             // Counts full slots
    
    bool shutdown;              // Signal to shutdown consumers
} pc_buffer_t;

/**
 * Initialize the producer-consumer buffer.
 * @param buf   Pointer to the buffer structure.
 * @return      0 on success, -1 on error.
 */
int pc_buffer_init(pc_buffer_t* buf);

/**
 * Destroy the producer-consumer buffer.
 * @param buf   Pointer to the buffer structure.
 */
void pc_buffer_destroy(pc_buffer_t* buf);

/**
 * Insert a connection request into the buffer (producer).
 * Blocks if buffer is full.
 * @param buf       Pointer to the buffer structure.
 * @param request   The connection request to insert.
 * @return          0 on success, -1 if shutdown.
 */
int pc_buffer_insert(pc_buffer_t* buf, const connection_request_t* request);

/**
 * Remove a connection request from the buffer (consumer).
 * Blocks if buffer is empty.
 * @param buf       Pointer to the buffer structure.
 * @param request   Output: the removed connection request.
 * @return          0 on success, -1 if shutdown.
 */
int pc_buffer_remove(pc_buffer_t* buf, connection_request_t* request);

/**
 * Signal all waiting consumers to shutdown.
 * @param buf   Pointer to the buffer structure.
 */
void pc_buffer_shutdown(pc_buffer_t* buf);

#endif
