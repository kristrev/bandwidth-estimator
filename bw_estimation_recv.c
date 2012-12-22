#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>

#include "bw_estimation_packets.h"

//Idea is that:
//Client sends a request to the sender stating desired bandwidth and duration
//This request is sent 10 times, five seconds apart, unless a reply is received
//If no reply is received 60 seconds after last reply, exit with failure
//A similar approach is taken for the sender when it comes to END_SESSION

//Bind the local socket. Should work with both IPv4 and IPv6
int bind_local(char *local_addr, char *local_port, int socktype){
  struct addrinfo hints, *info, *p;
  int sockfd;
  int rv;
  int yes=1;
  
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = socktype;
  
  if((rv = getaddrinfo(local_addr, local_port, &hints, &info)) != 0){
    fprintf(stderr, "Getaddrinfo (local) failed: %s\n", gai_strerror(rv));
    return -1;
  }
  
  for(p = info; p != NULL; p = p->ai_next){
    if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
      perror("Socket:");
      continue;
    }

    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      close(sockfd);
      perror("Setsockopt");
      continue;
    }

    if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
      close(sockfd);
      perror("Bind (local)");
      continue;
    }

    break;
  }

  if(p == NULL){
    fprintf(stderr, "Local bind failed\n");
    freeaddrinfo(info);  
    return -1;
  }

  freeaddrinfo(info);

  return sockfd;
}
void usage(){
    fprintf(stderr, "Supported command line arguments\n");
    fprintf(stderr, "-b : Bandwidth (in Mbit/s, only integers)\n");
    fprintf(stderr, "-t : Duration of test (in seconds)\n");
    fprintf(stderr, "-l : Payload length (in bytes)\n");
    fprintf(stderr, "-s : Source IP to bind to\n");
    fprintf(stderr, "-d : Destion IP\n");
    fprintf(stderr, "-p : Destion port\n");
    fprintf(stderr, "-f : log file (optional)\n");
}

int main(int argc, char *argv[]){
    uint16_t bandwidth = 0, duration = 0, payloadLen = 0;
    uint8_t *srcIp = NULL, *senderIp = NULL, *senderPort = NULL;
    uint8_t *logFileName = NULL;
    int32_t c, udpSockFd = -1;

    //Mandatory arguments + values
    if(argc != 13){
        usage();
        exit(EXIT_FAILURE);
    }

    while((c = getopt(argc, argv, "b:t:l:s:d:p:f:")) != -1){
        switch(c){
            case 'b':
                bandwidth = atoi(optarg);
                break;
            case 't':
                duration = atoi(optarg);
                break;
            case 'l':
                payloadLen = atoi(optarg);
                break;
            case 's':
                srcIp = optarg;
                break;
            case 'd':
                senderIp = optarg;
                break;
            case 'p':
                senderPort = optarg;
                break;
            case 'f':
                logFileName = optarg;
                break;
            default:
                usage();
                exit(EXIT_FAILURE);
        }
    }

    if(bandwidth == 0 || duration == 0 || payloadLen == 0 || srcIp == NULL || \
            senderIp == NULL || senderPort == NULL){
        usage();
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "Will contact sender at %s:%s\n", senderIp, senderPort);
    fprintf(stderr, "Bandwidth %d Mbit/s, Duration %d sec, Payload length %d bytes, Source IP %s\n", \
            bandwidth, duration, payloadLen, srcIp);

    //Bind UDP socket
    if((udpSockFd = bind_local(srcIp, NULL, SOCK_DGRAM)) == -1){
        fprintf(stderr, "Binding to local IP failed\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}
