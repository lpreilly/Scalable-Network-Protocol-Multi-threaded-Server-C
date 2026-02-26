#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <getopt.h>

#include "gfserver-student.h"
#include "gfserver.h"
#include "content.h"

#define USAGE                                                                                     \
  "usage:\n"                                                                                      \
  "  gfserver_main [options]\n"                                                                   \
  "options:\n"                                                                                    \
  "  -h                  Show this help message.\n"                                               \
  "  -t [nthreads]       Number of threads (Default: 16)\n"                                       \
  "  -m [content_file]   Content file mapping keys to content files (Default: content.txt)\n"     \
  "  -p [listen_port]    Listen port (Default: 56726)\n"                                          \
  "  -d [delay]          Delay in content_get, default 0, range 0-5000000 (microseconds)\n"


  // Command line options structure
static struct option gLongOptions[] = {
    {"help", no_argument, NULL, 'h'},
    {"delay", required_argument, NULL, 'd'},
    {"nthreads", required_argument, NULL, 't'},
    {"port", required_argument, NULL, 'p'},
    {"content", required_argument, NULL, 'm'},
    {NULL, 0, NULL, 0}};

extern unsigned long int content_delay;

// Functions from handler.c
extern void init_threads(size_t numthreads);
extern void cleanup_threads();
extern gfh_error_t gfs_handler(gfcontext_t **ctx, const char *path, void *arg);

// Signal handler to cleanup on shutdown
static void _sig_handler(int signo) {
  if (signo == SIGINT || signo == SIGTERM) {
    // Just exit
    exit(signo);
  }
}

int main(int argc, char **argv) {
  char *content_map = "content.txt"; // default content map file
  gfserver_t *gfs = NULL;
  int nthreads = 16;
  unsigned short port = 56726;
  int option_char = 0;

  // Turn off buffering to see output
  setbuf(stdout, NULL);

  // Setup signal handlers
  signal(SIGINT, _sig_handler);
  signal(SIGTERM, _sig_handler);

  // Parse command line arguments
  while ((option_char = getopt_long(argc, argv, "p:d:hm:t:", gLongOptions, NULL)) != -1) {
    switch (option_char) {
      case 'h': // help
        fprintf(stdout, "%s", USAGE);
        exit(0);
      case 'p': // port number
        port = atoi(optarg);
        break;
      case 'd': // delay
        content_delay = (unsigned long int)atoi(optarg);
        break;
      case 't': // number of threads
        nthreads = atoi(optarg);
        break;
      case 'm': // content mapping file
        content_map = optarg;
        break;
      default:
        fprintf(stderr, "%s", USAGE);
        exit(1);
    }
  }

  // Checks on parameters
  if (nthreads < 1) nthreads = 1; // need at least 1 thread
  if (content_delay > 5000000) {
    fprintf(stderr, "Content delay must be less than 5000000\n");
    exit(1);
  }

  // Load the content mapping 
  content_init(content_map);

  // Create and configure the server
  gfs = gfserver_create();
  gfserver_set_port(&gfs, port);
  gfserver_set_maxpending(&gfs, 24); // max pending connections in the queue
  gfserver_set_handler(&gfs, gfs_handler);
  gfserver_set_handlerarg(&gfs, NULL); // don't need handler args

  // Initialize the thread pool
  init_threads((size_t)nthreads);

  // Start serving
  gfserver_serve(&gfs);

  // Not reached
  cleanup_threads();

  return 0;
}
