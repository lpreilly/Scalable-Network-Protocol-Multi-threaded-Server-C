#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>

#include "gfserver-student.h"
#include "steque.h"
#include "content.h"

#define MAX_THREADS 1024
#define BUFSIZE 4096

// Job Strucutre - keeps track of what each worker needs to do
typedef struct {
    gfcontext_t *ctx;   // the context for this request
    char *path;      // path to the file we need to serve        
} job_t;

// Gloval stuff for managing the thread pool

static steque_t job_queue;

static pthread_mutex_t queue_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  queue_cv  = PTHREAD_COND_INITIALIZER;

static pthread_t worker_ids[MAX_THREADS];
static size_t worker_count = 0;

static int shutting_down = 0; // flag to tell workers when to exit

// Worker thread function - what each thread runs
static void *worker_thread(void *arg) {
    (void)arg; //unused parameter

    while (1) {
        // lock the queue to check for jobs
        pthread_mutex_lock(&queue_mtx);

        // wait if there's no work and we're not shutting down
        while (steque_isempty(&job_queue) && !shutting_down) {
            pthread_cond_wait(&queue_cv, &queue_mtx);
        }

        // check if we should exit
        if (shutting_down && steque_isempty(&job_queue)) {
            pthread_mutex_unlock(&queue_mtx);
            break; // exit the thread
        }

        // grab a job from the queue
        job_t *job = steque_front(&job_queue);
        steque_pop(&job_queue);

        pthread_mutex_unlock(&queue_mtx);

        // now do the work for this job
        int fd = content_get(job->path);

        if (fd < 0) {
            // File not found - send error response
            gfs_sendheader(&job->ctx, GF_FILE_NOT_FOUND, 0); 
        }
        else {
            struct stat st;

            if (fstat(fd, &st) < 0) {
                // Couldn't stat file - send error response
                gfs_sendheader(&job->ctx, GF_ERROR, 0); 
            }
            else {
                // Send the OK header with the file size
                gfs_sendheader(&job->ctx, GF_OK, st.st_size); 

                char buf[BUFSIZE];
                off_t offset = 0;
                off_t remaining = st.st_size;

                // Read the file in chunks
                while (remaining > 0) {

                    size_t chunk = remaining > BUFSIZE ? BUFSIZE : remaining;

                    ssize_t bytes = pread(fd, buf, chunk, offset);

                    if (bytes <= 0)
                        break; // error or EDF

                    ssize_t sent = gfs_send(&job->ctx, buf, bytes); 

                    if (sent <= 0)
                        break; // client disconnected maybe?

                    offset    += sent;
                    remaining -= sent;
                }
            }
            // not closing because content.c does that 
        }

        // Signal we are done with this context
        job->ctx = NULL;

        // clean up the job
        free(job->path);
        free(job);
    }

    return NULL;
}


// Initialize the thread pool with the specified number of threads
void init_threads(size_t numthreads) {
    // safety check - don't create too many threads
    if (numthreads > MAX_THREADS)
        numthreads = MAX_THREADS;

    steque_init(&job_queue);

    shutting_down = 0;
    worker_count = numthreads;

    // spin up all the worker threads
    for (size_t i = 0; i < worker_count; i++) {
        pthread_create(&worker_ids[i], NULL, worker_thread, NULL);
    }
}

// Shut down the thread pool cleanly
void cleanup_threads() {
    // Tell all workers to finish
    pthread_mutex_lock(&queue_mtx);
    shutting_down = 1;
    pthread_cond_broadcast(&queue_cv); // wake up all the workers
    pthread_mutex_unlock(&queue_mtx);

    // Wait for all workers to finish
    for (size_t i = 0; i < worker_count; i++) {
        pthread_join(worker_ids[i], NULL);
    }
}

// Main request handler - called by the server for each incoming request
gfh_error_t gfs_handler(gfcontext_t **ctx, const char *path, void *arg) {
    (void)arg; // not used

    // Create a new job for this request
    job_t *job = malloc(sizeof(job_t));
    if (!job) {
        // Out of memory - send error response
        gfs_sendheader(ctx, GF_ERROR, 0);
        *ctx = NULL;
        return gfh_failure;
    }

    // Transfer ownership of the context to the job
    job->ctx = *ctx;

    // Make a copy of the path
    job->path = strdup(path);
    if (!job->path) {
        // strdup failed
        free(job);
        gfs_sendheader(ctx, GF_ERROR, 0);
        *ctx = NULL;
        return gfh_failure;
    }

    // Add the job to the queue
    pthread_mutex_lock(&queue_mtx);
    steque_enqueue(&job_queue, job);
    pthread_cond_signal(&queue_cv); // wake up one worker
    pthread_mutex_unlock(&queue_mtx);

    // Done with this context now - the worker thread will handle it 
    *ctx = NULL;

    return gfh_success;
}
