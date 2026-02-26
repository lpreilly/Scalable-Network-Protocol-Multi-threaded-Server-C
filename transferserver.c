#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <getopt.h>
#include <sys/types.h>
#include <string.h>
#include <sys/socket.h>
#include <stdio.h>

#define BUFSIZE 512

#define USAGE                                                \
    "usage:\n"                                               \
    "  transferserver [options]\n"                           \
    "options:\n"                                             \
    "  -f                  Filename (Default: 6200.txt)\n"   \
    "  -p                  Port (Default: 61321)\n"          \
    "  -h                  Show this help message\n"         \

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"port", required_argument, NULL, 'p'},
    {"filename", required_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

int main(int argc, char **argv)
{
    int option_char;
    int portno = 61321;             /* port to listen on */
    char *filename = "6200.txt"; /* file to transfer */

    setbuf(stdout, NULL); // disable buffering

    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "p:hf:x", gLongOptions, NULL)) != -1) {
        switch (option_char) {
        case 'p': // listen-port
            portno = atoi(optarg);
            break;
        case 'f': // file to transfer
            filename = optarg;
            break;
        case 'h': // help
            fprintf(stdout, "%s", USAGE);
            exit(0);
            break;
        default:
            fprintf(stderr, "%s", USAGE);
            exit(1);
        }
    }


    if ((portno < 1025) || (portno > 65535)) {
        fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__, portno);
        exit(1);
    }
    
    if (NULL == filename) {
        fprintf(stderr, "%s @ %d: invalid filename\n", __FILE__, __LINE__);
        exit(1);
    }

    /* Socket Code Here */
    
    int server_fd, client_fd;
    struct addrinfo hints, *res, *p;
    struct sockaddr_storage client_addr;
    socklen_t addr_len;
    char buffer[BUFSIZE];
    char port_str[10];
    FILE *file_fp;
    size_t bytes_read;
    ssize_t bytes_sent;
    size_t total_sent;
    int opt = 1;
    
    // open the file for transfer
    file_fp = fopen(filename, "rb");  
    if (file_fp == NULL) {
        return 1;
    }
    
    // setup for get address info
    snprintf(port_str, sizeof(port_str), "%d", portno);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     
    hints.ai_socktype = SOCK_STREAM; 
    hints.ai_flags = AI_PASSIVE;     
    
    // get address info
    if (getaddrinfo(NULL, port_str, &hints, &res) != 0) {
        fclose(file_fp);
        return 1;
    }
    
    // try each address 
    for (p = res; p != NULL; p = p->ai_next) {
        server_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (server_fd < 0) {
            continue;
        }
        
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        if (bind(server_fd, p->ai_addr, p->ai_addrlen) == 0) {
            break; 
        }
        
        close(server_fd);
    }
    
    freeaddrinfo(res);
    
    if (p == NULL) {
        fclose(file_fp);
        return 1;
    }
    
    // listen for connections
    if (listen(server_fd, 5) < 0) {
        fclose(file_fp);
        close(server_fd);
        return 1;
    }
    
    // main server loop
    while (1) {
        addr_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            continue;
        }
        
        // rewind file to the start for each new client
        rewind(file_fp);
        
        // read file and send in chunks
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file_fp)) > 0) {
            total_sent = 0;
            
            // handle partial seeds
            while (total_sent < bytes_read) {
                bytes_sent = send(client_fd, 
                                 buffer + total_sent, 
                                 bytes_read - total_sent, 
                                 0);
                
                if (bytes_sent < 0) {
                    // Send error
                    close(client_fd);
                    goto next_client;
                }
                
                total_sent += bytes_sent;
            }
        }
        
        // check for errors
        if (ferror(file_fp)) {
            close(client_fd);
            continue;
        }
        
        // the file is sent, close connection
        close(client_fd);
        
        next_client: ; 
    }
    
    // cleanup
    fclose(file_fp);
    close(server_fd);
    return 0;
}
