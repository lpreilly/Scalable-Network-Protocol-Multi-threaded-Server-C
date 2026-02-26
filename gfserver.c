#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "gfserver-student.h"

#define BUF_SIZE 4096

// Context structure - holds client connection info
struct gfcontext_t {
    int clientfd;  // socket file descriptor for this client
};

// Server structure - holds all the server configuration
struct gfserver_t {
    unsigned short port;
    int backlog;  // max pending connections in listen queue
    gfh_error_t (*handler)(gfcontext_t **, const char *, void *);
    void *arg;  // argument to pass to handler
};

// Helper function to make sure we send all the data
static int send_all(int fd, const void *buf, size_t len) {
    const char *p = buf;
    while (len > 0) {
        ssize_t sent = send(fd, p, len, 0);
        if (sent <= 0) {
            return -1;  // connection error
        }
        p += sent;
        len -= sent;
    }
    return 0;
}

// Convert status code to string for protocol
static const char *get_status_str(gfstatus_t status) {
    if (status == GF_OK) {
        return "OK";
    }
    if (status == GF_FILE_NOT_FOUND) {
        return "FILE_NOT_FOUND";
    }
    if (status == GF_ERROR) {
        return "ERROR";
    }
    return "INVALID";  
}

// Abort a connection - close socket and free context
void gfs_abort(gfcontext_t **ctx) {
    if (!ctx || !*ctx) {
        return;  // already cleaned up or NULL
    }
    
    close((*ctx)->clientfd);
    free(*ctx);
    *ctx = NULL;
}

// Send data to client
ssize_t gfs_send(gfcontext_t **ctx, const void *data, size_t len) {
    if (!ctx || !*ctx) {
        return -1;
    }
    
    if (send_all((*ctx)->clientfd, data, len) < 0) {
        return -1;
    }
    
    return (ssize_t)len;
}

// Send the response header
ssize_t gfs_sendheader(gfcontext_t **ctx, gfstatus_t status, size_t file_len) {
    if (!ctx || !*ctx) {
        return -1;
    }
    
    char buf[BUF_SIZE];
    int len;

    // For OK status, we include the file length
    // For errors, we don't send length
    if (status == GF_OK) {
        len = snprintf(buf, sizeof(buf), "GETFILE OK %zu\r\n\r\n", file_len);
    } else {
        len = snprintf(buf, sizeof(buf), "GETFILE %s\r\n\r\n", get_status_str(status));
    }

    if (len <= 0 || send_all((*ctx)->clientfd, buf, len) < 0) {
        return -1;
    }
    
    return (ssize_t)len;
}

// Create a new server instance
gfserver_t *gfserver_create() {
    gfserver_t *srv = malloc(sizeof(gfserver_t));
    if (srv) {
        memset(srv, 0, sizeof(gfserver_t));
        srv->backlog = 5;  // default backlog
    }
    return srv;
}

// Set the port number for the server
void gfserver_set_port(gfserver_t **gfs, unsigned short port) {
    if (gfs && *gfs) {
        (*gfs)->port = port;
    }
}

// Set the handler argument 
void gfserver_set_handlerarg(gfserver_t **gfs, void *arg) {
    if (gfs && *gfs) {
        (*gfs)->arg = arg;
    }
}

// Set the request handler callback
void gfserver_set_handler(gfserver_t **gfs, gfh_error_t (*handler)(gfcontext_t **, const char *, void *)) {
    if (gfs && *gfs) {
        (*gfs)->handler = handler;
    }
}

// Set max pending connections
void gfserver_set_maxpending(gfserver_t **gfs, int n_pending) {
    if (gfs && *gfs) {
        (*gfs)->backlog = n_pending;
    }
}

// Main server loop 
void gfserver_serve(gfserver_t **gfs) {
    if (!gfs || !*gfs) {
        return;
    }
    
    gfserver_t *srv = *gfs;

    // Create listening socket
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        exit(1);
    }

    // Set socket option to reuse address 
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Setup address structure
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;  // listen on all interfaces
    addr.sin_port = htons(srv->port);

    // Bind to port
    if (bind(listenfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    // Start listening
    listen(listenfd, srv->backlog);

    // Main accept loop
    while (1) {
        int clientfd = accept(listenfd, NULL, NULL);
        if (clientfd < 0) {
            continue;  // accept failed, try again
        }

        // Create context for this connection
        gfcontext_t *ctx = malloc(sizeof(gfcontext_t));
        ctx->clientfd = clientfd;

        // Read the request header
        char req[BUF_SIZE];
        ssize_t received = 0;
        
        // Read byte by byte until we see the end marker
        while (received < BUF_SIZE - 1) {
            ssize_t r = recv(clientfd, req + received, 1, 0);
            if (r <= 0) {
                break;  // error or connection closed
            }
            received++;
            req[received] = '\0';
            
            // Check if we got the complete header
            if (strstr(req, "\r\n\r\n")) {
                break;
            }
        }

        // Parse the request line
        char scheme[16];
        char method[16];
        char path[256];
        int parts = sscanf(req, "%s %s %s", scheme, method, path);

        // Validate the request format
        if (parts != 3 || strcmp(scheme, "GETFILE") != 0 || 
            strcmp(method, "GET") != 0 || path[0] != '/') {
            
            // Invalid request
            gfs_sendheader(&ctx, GF_INVALID, 0);
            gfs_abort(&ctx);
            continue;
        }

        // Call the handler if we have one
        if (srv->handler) {
            srv->handler(&ctx, path, srv->arg);
            
            // If handler didn't consume the context, clean it up
            if (ctx) {
                gfs_abort(&ctx);
            }
        } else {
            gfs_sendheader(&ctx, GF_ERROR, 0);
            gfs_abort(&ctx);
        }
    }
    
}
