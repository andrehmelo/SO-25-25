#include "pc_buffer.h"
#include "display.h"
#include <string.h>
#include <errno.h>

int pc_buffer_init(pc_buffer_t* buf) {
    buf->head = 0;
    buf->tail = 0;
    buf->shutdown = false;
    
    // Initialize mutex
    if (pthread_mutex_init(&buf->mutex, NULL) != 0) {
        debug("[PC Buffer] Failed to init mutex: %s\n", strerror(errno));
        return -1;
    }
    
    // Initialize semaphore for empty slots (starts at buffer size)
    if (sem_init(&buf->sem_empty, 0, PC_BUFFER_SIZE) != 0) {
        debug("[PC Buffer] Failed to init sem_empty: %s\n", strerror(errno));
        pthread_mutex_destroy(&buf->mutex);
        return -1;
    }
    
    // Initialize semaphore for full slots (starts at 0)
    if (sem_init(&buf->sem_full, 0, 0) != 0) {
        debug("[PC Buffer] Failed to init sem_full: %s\n", strerror(errno));
        sem_destroy(&buf->sem_empty);
        pthread_mutex_destroy(&buf->mutex);
        return -1;
    }
    
    debug("[PC Buffer] Initialized successfully\n");
    return 0;
}

void pc_buffer_destroy(pc_buffer_t* buf) {
    sem_destroy(&buf->sem_full);
    sem_destroy(&buf->sem_empty);
    pthread_mutex_destroy(&buf->mutex);
    debug("[PC Buffer] Destroyed\n");
}

int pc_buffer_insert(pc_buffer_t* buf, const connection_request_t* request) {
    // Wait for an empty slot
    if (sem_wait(&buf->sem_empty) != 0) {
        return -1;
    }
    
    // Check for shutdown
    if (buf->shutdown) {
        sem_post(&buf->sem_empty);  // Release for others
        return -1;
    }
    
    // Lock mutex to access buffer
    pthread_mutex_lock(&buf->mutex);
    
    // Insert request at head
    memcpy(&buf->buffer[buf->head], request, sizeof(connection_request_t));
    buf->head = (buf->head + 1) % PC_BUFFER_SIZE;
    
    debug("[PC Buffer] Inserted request (req=%s, notif=%s)\n", 
          request->req_pipe_path, request->notif_pipe_path);
    
    pthread_mutex_unlock(&buf->mutex);
    
    // Signal that there's a full slot
    sem_post(&buf->sem_full);
    
    return 0;
}

int pc_buffer_remove(pc_buffer_t* buf, connection_request_t* request) {
    // Wait for a full slot
    if (sem_wait(&buf->sem_full) != 0) {
        return -1;
    }
    
    // Check for shutdown
    if (buf->shutdown) {
        sem_post(&buf->sem_full);  // Release for others
        return -1;
    }
    
    // Lock mutex to access buffer
    pthread_mutex_lock(&buf->mutex);
    
    // Remove request from tail
    memcpy(request, &buf->buffer[buf->tail], sizeof(connection_request_t));
    buf->tail = (buf->tail + 1) % PC_BUFFER_SIZE;
    
    debug("[PC Buffer] Removed request (req=%s, notif=%s)\n", 
          request->req_pipe_path, request->notif_pipe_path);
    
    pthread_mutex_unlock(&buf->mutex);
    
    // Signal that there's an empty slot
    sem_post(&buf->sem_empty);
    
    return 0;
}

void pc_buffer_shutdown(pc_buffer_t* buf) {
    buf->shutdown = true;
    
    // Wake up all waiting threads by posting to both semaphores
    // Post multiple times to ensure all consumers wake up
    for (int i = 0; i < PC_BUFFER_SIZE + 1; i++) {
        sem_post(&buf->sem_full);
        sem_post(&buf->sem_empty);
    }
    
    debug("[PC Buffer] Shutdown signaled\n");
}
