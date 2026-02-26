#include <unistd.h>
#include <sys/socket.h>
#include <getopt.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>

#define BUFSIZE 1024

#define USAGE                                                        \
    "usage:\n"                                                         \
    "  echoserver [options]\n"                                         \
    "options:\n"                                                       \
    "  -p                  Port (Default: 14757)\n"                    \
    "  -m                  Maximum pending connections (default: 5)\n" \
    "  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"maxnpending",   required_argument,      NULL,           'm'},
    {"port",          required_argument,      NULL,           'p'},
    {"help",          no_argument,            NULL,           'h'},
    {NULL,            0,                      NULL,             0}
};


int main(int argc, char **argv) {
    int option_char;
    int portno = 14757; /* port to listen on */
    int maxnpending = 5;
  
    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "p:m:hx", gLongOptions, NULL)) != -1) {
        switch (option_char) {
        case 'm': // server
            maxnpending = atoi(optarg);
            break; 
        case 'h': // help
            exit(0);
            break;
        case 'p': // listen-port
            portno = atoi(optarg);
            break;                                        
        default:
            exit(1);
        }
    }

    setbuf(stdout, NULL); // disable buffering

    if ((portno < 1025) || (portno > 65535)) {
        exit(1);
    }
    if (maxnpending < 1) {
        exit(1);
    }


      /* Socket Code Here */
  
  int server_fd, client_fd;
  struct sockaddr_in6 address;
  int addrlen = sizeof(address);
  char buffer[16];
  int opt = 1;
  
  // create IPv6 socket ===
  server_fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (server_fd < 0) {
      return 1;
  }
  
  // set socket options 
  // allowing reuse of address/port
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
      close(server_fd);
      return 1;
  }
  
  // allow IPv4 connections on IPv6 socket 
  opt = 0;
  if (setsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) < 0) {
      close(server_fd);
      return 1;
  }
  
  // bind
  memset(&address, 0, sizeof(address));
  address.sin6_family = AF_INET6;
  address.sin6_addr = in6addr_any;  
  address.sin6_port = htons(portno);
  
  if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
      close(server_fd);
      return 1;
  }
  
  // listen
  if (listen(server_fd, maxnpending) < 0) {
      close(server_fd);
      return 1;
  }
  
  // main loop
  while (1) {
      client_fd = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
      if (client_fd < 0) {
          continue;
      }
      
      int bytes_received = recv(client_fd, buffer, 15, 0);
      if (bytes_received > 0) {
          send(client_fd, buffer, bytes_received, 0);
      }
      
      close(client_fd);
  }
  
  close(server_fd);
  return 0;
}
