#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "gfclient-student.h"

#define REQ_BUFSIZE 1024
#define HDR_BUFSIZE 4096
#define DATA_BUFSIZE 4096

// Main request 
struct gfcrequest_t {
    char server[256];
    unsigned short port;
    char path[256];

    void (*headerfunc)(void *header_buffer, size_t header_buffer_length, void *handlerarg);
    void *headerarg;

    void (*writefunc)(void *data_buffer, size_t data_buffer_length, void *handlerarg);
    void *writearg;

    gfstatus_t status;
    size_t filelen;
    size_t bytesreceived;
};

// Helper function
static int send_all(int sockfd, const void *buf, size_t len) {
    const char *p = buf;
    size_t left = len;
    
    while (left > 0) {
        ssize_t n = send(sockfd, p, left, 0);
        if (n <= 0) {
            return -1;  // send failed
        }
        p += n;
        left -= n;
    }
    return 0;
}

// Parse the status string from server response
static gfstatus_t parse_status(const char *status_str) {
    if (strcmp(status_str, "OK") == 0) {
        return GF_OK;
    }
    if (strcmp(status_str, "FILE_NOT_FOUND") == 0) {
        return GF_FILE_NOT_FOUND;
    }
    if (strcmp(status_str, "ERROR") == 0) {
        return GF_ERROR;
    }
    if (strcmp(status_str, "INVALID") == 0) {
        return GF_INVALID;
    }
    // Default to invalid if we don't recognize it
    return GF_INVALID;
}

void gfc_cleanup(gfcrequest_t **gfr) {
    if (gfr && *gfr) {
        free(*gfr);
        *gfr = NULL;
    }
}

gfcrequest_t *gfc_create() {
    gfcrequest_t *req = calloc(1, sizeof(gfcrequest_t));
    if (!req) {
        return NULL;
    }
    
    // Initialize some fields
    req->port = 0;
    req->status = GF_INVALID;
    req->filelen = 0;
    req->bytesreceived = 0;
    
    return req;
}

size_t gfc_get_bytesreceived(gfcrequest_t **gfr) {
    if (!gfr || !*gfr) {
        return 0;
    }
    return (*gfr)->bytesreceived;
}

gfstatus_t gfc_get_status(gfcrequest_t **gfr) {
    if (!gfr || !*gfr) {
        return GF_INVALID;
    }
    return (*gfr)->status;
}

size_t gfc_get_filelen(gfcrequest_t **gfr) {
    if (!gfr || !*gfr) {
        return 0;
    }
    return (*gfr)->filelen;
}

void gfc_global_init() {
}

void gfc_global_cleanup() {
}

void gfc_set_path(gfcrequest_t **gfr, const char *path) {
    if (!gfr || !*gfr || !path) {
        return;
    }
    strncpy((*gfr)->path, path, sizeof((*gfr)->path) - 1);
    (*gfr)->path[sizeof((*gfr)->path) - 1] = '\0';  // make sure it's null terminated
}

void gfc_set_headerfunc(gfcrequest_t **gfr, void (*headerfunc)(void *, size_t, void *)) {
    if (!gfr || !*gfr) {
        return;
    }
    (*gfr)->headerfunc = headerfunc;
}

void gfc_set_server(gfcrequest_t **gfr, const char *server) {
    if (!gfr || !*gfr || !server) {
        return;
    }
    strncpy((*gfr)->server, server, sizeof((*gfr)->server) - 1);
    (*gfr)->server[sizeof((*gfr)->server) - 1] = '\0';
}

void gfc_set_port(gfcrequest_t **gfr, unsigned short port) {
    if (!gfr || !*gfr) {
        return;
    }
    (*gfr)->port = port;
}

void gfc_set_writearg(gfcrequest_t **gfr, void *writearg) {
    if (!gfr || !*gfr) {
        return;
    }
    (*gfr)->writearg = writearg;
}

void gfc_set_headerarg(gfcrequest_t **gfr, void *headerarg) {
    if (!gfr || !*gfr) {
        return;
    }
    (*gfr)->headerarg = headerarg;
}

void gfc_set_writefunc(gfcrequest_t **gfr, void (*writefunc)(void *, size_t, void *)) {
    if (!gfr || !*gfr) {
        return;
    }
    (*gfr)->writefunc = writefunc;
}

// Main function 
int gfc_perform(gfcrequest_t **gfr) {
    if (!gfr || !*gfr) {
        return -1;
    }
    
    gfcrequest_t *req = *gfr;
    
    // Reset state
    req->bytesreceived = 0;
    req->filelen = 0;
    req->status = GF_INVALID;
    
    // Resolve the server address
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%hu", req->port);
    
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP
    
    if (getaddrinfo(req->server, portstr, &hints, &res) != 0) {
        return -1;
    }
    
    // Try to connect to one of the addresses
    int sockfd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) {
            continue;
        }
        
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;  // success
        }
        
        close(sockfd);
        sockfd = -1;
    }
    
    freeaddrinfo(res);
    
    if (sockfd == -1) {
        return -1;  // couldn't connect
    }
    
    // Build and send the request
    char reqbuf[REQ_BUFSIZE];
    int n = snprintf(reqbuf, sizeof(reqbuf), "GETFILE GET %s\r\n\r\n", req->path);
    
    if (n <= 0 || (size_t)n >= sizeof(reqbuf)) {
        close(sockfd);
        return -1;
    }
    
    if (send_all(sockfd, reqbuf, (size_t)n) < 0) {
        close(sockfd);
        return -1;
    }
    
    // Now read the response header
    char hdrbuf[HDR_BUFSIZE];
    size_t hdrlen = 0;
    
    // Keep reading until we find the end of header marker
    while (hdrlen + 1 < sizeof(hdrbuf)) {
        ssize_t r = recv(sockfd, hdrbuf + hdrlen, sizeof(hdrbuf) - hdrlen - 1, 0);
        
        if (r <= 0) {
            close(sockfd);
            return -1;
        }
        
        hdrlen += (size_t)r;
        hdrbuf[hdrlen] = '\0';
        
        // Look for end of header
        char *end = strstr(hdrbuf, "\r\n\r\n");
        if (end) {
            size_t header_bytes = (end + 4) - hdrbuf;
            
            // Parse the header line
            char proto[32], status_str[32];
            size_t filelen = 0;
            int parsed = sscanf(hdrbuf, "%31s %31s %zu", proto, status_str, &filelen);
            
            // Some responses don't include file length 
            if (parsed < 2) {
                close(sockfd);
                return -1;
            }
            
            if (parsed == 2) {
                filelen = 0;  // no file length provided
            }
            
            if (strcmp(proto, "GETFILE") != 0) {
                close(sockfd);
                return -1;
            }
            
            req->status = parse_status(status_str);
            req->filelen = filelen;
            
            // Call header callback if set
            if (req->headerfunc) {
                req->headerfunc(hdrbuf, header_bytes, req->headerarg);
            }
            
            // Check if there's any data after the header
            size_t remaining = hdrlen - header_bytes;
            
            if (req->status == GF_OK) {
                if (remaining > 0 && req->writefunc) {
                    req->writefunc(hdrbuf + header_bytes, remaining, req->writearg);
                }
                req->bytesreceived += remaining;
            }
            
            goto read_body;  // jump to body reading
        }
    }
    
    // Header was too large or malformed
    close(sockfd);
    return -1;

read_body:
    // If status isn't OK, we're done
    if (req->status != GF_OK) {
        req->bytesreceived = 0;
        close(sockfd);
        return 0;
    }
    
    // Read the file data
    char databuf[DATA_BUFSIZE];
    
    for (;;) {
        ssize_t r = recv(sockfd, databuf, sizeof(databuf), 0);
        
        if (r < 0) {
            close(sockfd);
            return -1;
        }
        
        if (r == 0) {
            // Connection closed, check if we got everything
            if (req->bytesreceived < req->filelen) {
                close(sockfd);
                return -1;  // premature close
            }
            break;
        }
        
        req->bytesreceived += (size_t)r;
        
        if (req->writefunc) {
            req->writefunc(databuf, (size_t)r, req->writearg);
        }
        
        // Stop when we've received all the bytes
        if (req->bytesreceived >= req->filelen) {
            break;
        }
    }
    
    close(sockfd);
    return 0;
}

// Convert status enum to string
const char *gfc_strstatus(gfstatus_t status) {
    const char *strstatus = "UNKNOWN";
    
    switch (status) {
        case GF_OK:
            strstatus = "OK";
            break;
        case GF_FILE_NOT_FOUND:
            strstatus = "FILE_NOT_FOUND";
            break;
        case GF_INVALID:
            strstatus = "INVALID";
            break;
        case GF_ERROR:
            strstatus = "ERROR";
            break;
    }
    
    return strstatus;
}
