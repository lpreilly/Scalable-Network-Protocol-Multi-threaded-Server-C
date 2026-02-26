#include <unistd.h>
#include <getopt.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/types.h>
#include <stdio.h>

/* Be prepared accept a response of this length */
#define BUFSIZE 1024

#define USAGE                                                                       \
    "usage:\n"                                                                      \
    "  echoclient [options]\n"                                                      \
    "options:\n"                                                                    \
    "  -p                  Port (Default: 14757)\n"                                  \
    "  -s                  Server (Default: localhost)\n"                           \
    "  -m                  Message to send to server (Default: \"Hello Spring!!\")\n" \
    "  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"message", required_argument, NULL, 'm'},
    {"port", required_argument, NULL, 'p'},
    {"server", required_argument, NULL, 's'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

/* Main ========================================================= */
int main(int argc, char **argv)
{
    int option_char = 0;
    unsigned short portno = 14757;
    char *hostname = "localhost";
    char *message = "Hello Spring!!";

    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "s:p:m:hx", gLongOptions, NULL)) != -1) {
        switch (option_char) {
        case 's': // server
            hostname = optarg;
            break;
        case 'p': // listen-port
            portno = atoi(optarg);
            break;
        default:
            exit(1);
        case 'm': // message
            message = optarg;
            break;
        case 'h': // help
            exit(0);
            break;
        }
    }

    setbuf(stdout, NULL); // disable buffering

    if ((portno < 1025) || (portno > 65535)) {
        exit(1);
    }

    if (NULL == message) {
        exit(1);
    }

    if (NULL == hostname) {
        exit(1);
    }

        /* Socket Code Here */
    
    int sockfd;
    struct addrinfo hints, *res, *p;
    char port_str[10];
    
    // doing hints for getaddrinfo
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;    // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP
    
    // convert port to string
    snprintf(port_str, sizeof(port_str), "%d", portno);
    
    // get address info (handles IPv4 and IPv6)
    if (getaddrinfo(hostname, port_str, &hints, &res) != 0) {
        return 1;
    }
    
    // try each address until one works
    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd < 0) continue;
        
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == 0) {
            break; // it worked
        }
        
        close(sockfd);
    }
    
    freeaddrinfo(res);
    
    if (p == NULL) {
        return 1; // all addresses failed
    }
    
    // send message (max 15 bytes)
    int msg_len = strlen(message);
    if (msg_len > 15) msg_len = 15;
    send(sockfd, message, msg_len, 0);
    
    // receive echo
    char buffer[16];
    int n = recv(sockfd, buffer, 15, 0);
    if (n > 0) {
        buffer[n] = '\0';
        printf("%s", buffer);
    }
    
    close(sockfd);
    return 0;
}
