#include <stdio.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <getopt.h>
#include <string.h>

#define BUFSIZE 512

#define USAGE                                                \
  "usage:\n"                                                 \
  "  transferclient [options]\n"                             \
  "options:\n"                                               \
  "  -p                  Port (Default: 61321)\n"            \
  "  -s                  Server (Default: localhost)\n"      \
  "  -h                  Show this help message\n"           \
  "  -o                  Output file (Default cs6200.txt)\n" 

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"server", required_argument, NULL, 's'},
    {"output", required_argument, NULL, 'o'},
    {"help", no_argument, NULL, 'h'},
    {"port", required_argument, NULL, 'p'},
    {NULL, 0, NULL, 0}};

/* Main ========================================================= */
int main(int argc, char **argv)
{
    int option_char = 0;
    char *hostname = "localhost";
    unsigned short portno = 61321;
    char *filename = "cs6200.txt";

    setbuf(stdout, NULL);

    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "s:p:o:hx", gLongOptions, NULL)) != -1) {
        switch (option_char) {
        case 's': // server
            hostname = optarg;
            break;
        case 'p': // listen-port
            portno = atoi(optarg);
            break;
        default:
            fprintf(stderr, "%s", USAGE);
            exit(1);
        case 'o': // filename
            filename = optarg;
            break;
        case 'h': // help
            fprintf(stdout, "%s", USAGE);
            exit(0);
            break;
        }
    }

    if (NULL == hostname) {
        fprintf(stderr, "%s @ %d: invalid host name\n", __FILE__, __LINE__);
        exit(1);
    }

    if (NULL == filename) {
        fprintf(stderr, "%s @ %d: invalid filename\n", __FILE__, __LINE__);
        exit(1);
    }

    if ((portno < 1025) || (portno > 65535)) {
        fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__, portno);
        exit(1);
    }

            /* Socket Code Here */
    
    int sockfd;
    struct addrinfo hints, *res, *p;
    char port_str[10];
    char buffer[BUFSIZE];
    FILE *file_fp;
    ssize_t bytes_received;
    size_t bytes_written;
    
    // create output file
    file_fp = fopen(filename, "wb");
    if (file_fp == NULL) {
        fclose(file_fp);
        return 1;
    }
    
    // setup address hints
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    // convert port
    snprintf(port_str, sizeof(port_str), "%d", portno);
    
    // get address info
    if (getaddrinfo(hostname, port_str, &hints, &res) != 0) {
        fclose(file_fp);
        return 1;
    }
    
    // try each address
    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd < 0) {
            continue;
        }
        
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        
        close(sockfd);
    }
    
    freeaddrinfo(res);
    
    if (p == NULL) {
        fclose(file_fp);
        return 1;
    }
    
    // get data
    while ((bytes_received = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
        bytes_written = fwrite(buffer, 1, bytes_received, file_fp);
        if (bytes_written != bytes_received) {
            fclose(file_fp);
            close(sockfd);
            return 1;
        }
    }
    
    // checking errors
    if (bytes_received < 0) {
        fclose(file_fp);
        close(sockfd);
        return 1;
    }
    
    // cleanup
    fclose(file_fp);
    close(sockfd);
    
    return 0;
}
