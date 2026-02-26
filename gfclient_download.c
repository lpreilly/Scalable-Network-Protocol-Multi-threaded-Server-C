#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gfclient-student.h"
#include "workload.h"
#include "steque.h"

#define MAX_THREADS 1024
#define PATH_BUFFER_SIZE 512

// Usage message
#define USAGE                                                             \
  "usage:\n"                                                              \
  "  gfclient_download [options]\n"                                       \
  "options:\n"                                                            \
  "  -h                  Show this help message\n"                        \
  "  -s [server_addr]    Server address (Default: 127.0.0.1)\n"           \
  "  -p [server_port]    Server port (Default: 56726)\n"                  \
  "  -w [workload_path]  Path to workload file (Default: workload.txt)\n" \
  "  -t [nthreads]       Number of threads (Default 8 Max: 1024)\n"       \
  "  -n [num_requests]   Request download total (Default: 16)\n"

static struct option gLongOptions[] = {
    {"nrequests", required_argument, NULL, 'n'},
    {"port", required_argument, NULL, 'p'},
    {"nthreads", required_argument, NULL, 't'},
    {"server", required_argument, NULL, 's'},
    {"help", no_argument, NULL, 'h'},
    {"workload", required_argument, NULL, 'w'},
    {NULL, 0, NULL, 0}
};

// Utility function

static void Usage() {
  fprintf(stderr, "%s", USAGE);
}

// Generates local file path based on request path
static void localPath(char *req_path, char *local_path) {
  static int counter = 0;
  
  sprintf(local_path, "%s-%06d", &req_path[1], counter++);
}

// Opens file for writing - creates directories if they don't exist
static FILE *openFile(char *path) {
  char *cur, *prev;
  FILE *ans;

  // Create necessary directories first
  prev = path;
  while (NULL != (cur = strchr(prev + 1, '/'))) {
    *cur = '\0';
    
    if (0 > mkdir(&path[0], S_IRWXU)) {
      if (errno != EEXIST) {
        perror("Unable to create directory");
        exit(EXIT_FAILURE);
      }
    }
    
    *cur = '/';
    prev = cur;
  }

  if (NULL == (ans = fopen(&path[0], "w"))) {
    perror("Unable to open file");
    exit(EXIT_FAILURE);
  }

  return ans;
}

// Write callback - called when data is received
static void writecb(void *data, size_t data_len, void *arg) {
  FILE *file = (FILE *)arg;
  
  fwrite(data, 1, data_len, file);
}

// Job structure

typedef struct {
  char req_path[PATH_BUFFER_SIZE];
  char local_path[PATH_BUFFER_SIZE];
  char server[256];
  unsigned short port;
} job_t;

// Shared queue and synch work

static steque_t job_queue;
static pthread_mutex_t job_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t job_cond = PTHREAD_COND_INITIALIZER;

// Progress tracking variables
static int total_requests = 0;
static int completed_requests = 0;
static pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;

// Worker thread

static void* worker(void *arg) {
  (void)arg;  // Not using this parameter

  while (1) {
    // Lock the queue to check for jobs
    pthread_mutex_lock(&job_mutex);

    // Wait until there's something in the queue
    while (steque_isempty(&job_queue)) {
      pthread_cond_wait(&job_cond, &job_mutex);
    }

    // Pop a job from the queue
    job_t *job = (job_t*) steque_front(&job_queue);
    steque_pop(&job_queue);

    pthread_mutex_unlock(&job_mutex);

    // Do the work for this job
    FILE *file = openFile(job->local_path);

    // Setup GFC request
    gfcrequest_t *gfr = gfc_create();
    gfc_set_path(&gfr, job->req_path);
    gfc_set_server(&gfr, job->server);
    gfc_set_port(&gfr, job->port);
    gfc_set_writefunc(&gfr, writecb);
    gfc_set_writearg(&gfr, file);

    fprintf(stdout, "Requesting %s%s\n", job->server, job->req_path);

    // Actually perform the download
    int rc = gfc_perform(&gfr);

    if (rc < 0) {
      fprintf(stdout, "gfc_perform returned error %d\n", rc);
      fclose(file);
      unlink(job->local_path);  // Remove incomplete file
    } else {
      fclose(file);
    }

    // Check status and clean up failed downloads
    if (gfc_get_status(&gfr) != GF_OK) {
      unlink(job->local_path);
    }

    // Output stats about the download
    fprintf(stdout, "Status: %s\n", gfc_strstatus(gfc_get_status(&gfr)));
    fprintf(stdout, "Received %zu of %zu bytes\n",
            gfc_get_bytesreceived(&gfr),
            gfc_get_filelen(&gfr));

    gfc_cleanup(&gfr);
    free(job);

    // Count this completed request
    pthread_mutex_lock(&count_mutex);
    completed_requests++;
    pthread_mutex_unlock(&count_mutex);
  }

  return NULL;
}

// Main function

int main(int argc, char **argv) {
  // Defaults
  char *workload_path = "workload.txt";
  char *server = "localhost";
  unsigned short port = 56726;
  int nthreads = 8;
  int nrequests = 16;

  int option_char = 0;

  setbuf(stdout, NULL);  // Turn off stdout buffering

  // Parse command line options
  while ((option_char = getopt_long(argc, argv, "p:n:hs:t:r:w:", gLongOptions, NULL)) != -1) {
    switch (option_char) {
      case 's':
        server = optarg;
        break;
      case 'w':
        workload_path = optarg;
        break;
      case 'n':
      case 'r':  
        nrequests = atoi(optarg);
        break;
      case 't':
        nthreads = atoi(optarg);
        break;
      case 'p':
        port = atoi(optarg);
        break;
      case 'h':
        Usage();
        exit(0);
      default:
        Usage();
        exit(1);
    }
  }

  // Load workload
  if (EXIT_SUCCESS != workload_init(workload_path)) {
    fprintf(stderr, "Unable to load workload file %s.\n", workload_path);
    exit(EXIT_FAILURE);
  }

  // Validate thread count
  if (nthreads < 1 || nthreads > MAX_THREADS) {
    fprintf(stderr, "Invalid number of threads\n");
    exit(EXIT_FAILURE);
  }

  gfc_global_init();

  // Initialize the job queue
  steque_init(&job_queue);

  // Spawn worker threads
  pthread_t tid;
  int i;
  for (i = 0; i < nthreads; i++) {
    pthread_create(&tid, NULL, worker, NULL);
    pthread_detach(tid);  // Don't need to join these threads
  }

  // Main thread - read workload and enqueue jobs
  for (i = 0; i < nrequests; i++) {
    char *req_path = workload_get_path();

    // Create a new job
    job_t *job = malloc(sizeof(job_t));
    strncpy(job->req_path, req_path, PATH_BUFFER_SIZE);
    job->req_path[PATH_BUFFER_SIZE - 1] = '\0';  // Safety null terminator

    strncpy(job->server, server, sizeof(job->server));
    job->server[sizeof(job->server) - 1] = '\0';

    job->port = port;

    localPath(req_path, job->local_path);

    // Enqueue the job
    pthread_mutex_lock(&job_mutex);
    steque_enqueue(&job_queue, job);
    pthread_cond_signal(&job_cond);  // Signal a waiting worker
    pthread_mutex_unlock(&job_mutex);

    // Count this request
    pthread_mutex_lock(&count_mutex);
    total_requests++;
    pthread_mutex_unlock(&count_mutex);
  }

  // Wait for all requests to complete
  while (1) {
    pthread_mutex_lock(&count_mutex);
    int done = (completed_requests == total_requests);
    pthread_mutex_unlock(&count_mutex);

    if (done) break;
    
    usleep(10000);  // Sleep 10ms to avoid spinning
  }

  gfc_global_cleanup();
  
  return 0;
}
