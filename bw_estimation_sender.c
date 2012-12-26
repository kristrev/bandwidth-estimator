#include <stdio.h>
#include <stdint.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>

#include "bw_estimation_packets.h"

//Make multithreaded. Need a hashmap here

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
    fprintf(stdout, "Getaddrinfo (local) failed: %s\n", gai_strerror(rv));
    return -1;
  }
  
  for(p = info; p != NULL; p = p->ai_next){
    if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
      perror("Socket:");
      continue;
    }

    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      close(sockfd);
      perror("Setsockopt (reuseaddr)");
      continue;
    }

    if(setsockopt(sockfd, SOL_SOCKET, SO_TIMESTAMPNS, &yes, sizeof(int)) == -1) {
      close(sockfd);
      perror("Setsockopt (timestamp)");
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
    fprintf(stdout, "Local bind failed\n");
    freeaddrinfo(info);  
    return -1;
  }

  freeaddrinfo(info);

  return sockfd;
}

void networkEventLoop(int32_t udpSockFd){
    fd_set recvSet, recvSetCopy;
    int32_t fdmax = udpSockFd + 1;
    int32_t retval = 0;
    ssize_t numbytes = 0;
    struct pktHdr *hdr;

    struct msghdr msg;
    struct iovec iov;
    uint8_t buf[MAX_PAYLOAD_LEN] = {0};
    struct sockaddr_storage senderAddr;
    char addrPresentation[INET6_ADDRSTRLEN];
    uint16_t recvPort = 0;
    struct dataPkt *pkt = NULL;

    memset(&msg, 0, sizeof(struct msghdr));
    memset(&iov, 0, sizeof(struct iovec));
    memset(&senderAddr, 0, sizeof(struct sockaddr_storage));

    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);

    msg.msg_name = (void *) &senderAddr;
    msg.msg_namelen = sizeof(struct sockaddr_storage);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    FD_ZERO(&recvSet);
    FD_ZERO(&recvSetCopy);
    FD_SET(udpSockFd, &recvSet);

    while(1){
        recvSetCopy = recvSet;
        retval = select(fdmax, &recvSetCopy, NULL, NULL, NULL);

        if(retval < 0){
            fprintf(stdout, "Select failed, aborting\n");
            close(udpSockFd);
            exit(EXIT_FAILURE);
        } else {
            numbytes = recvmsg(udpSockFd, &msg, 0); 
            fprintf(stdout, "Received %zd bytes\n", numbytes);
            hdr = (struct pktHdr *) buf;

            if(senderAddr.ss_family == AF_INET){
                inet_ntop(AF_INET, &(((struct sockaddr_in *) &senderAddr)->sin_addr),\
                        addrPresentation, INET6_ADDRSTRLEN);
                recvPort = ntohs(((struct sockaddr_in *) &senderAddr)->sin_port);
            } else if(senderAddr.ss_family == AF_INET6){
                inet_ntop(AF_INET6, &(((struct sockaddr_in6 *) &senderAddr)->sin6_addr),\
                        addrPresentation, INET6_ADDRSTRLEN);
                recvPort = ntohs(((struct sockaddr_in6 *) &senderAddr)->sin6_port);
            }

            if(hdr->type == NEW_SESSION){
                fprintf(stdout, "Request for new session from %s:%u\n", addrPresentation, recvPort);
                pkt = (struct dataPkt *) buf;
                pkt->type = DATA;
                sendmsg(udpSockFd, &msg, 0);
            } else {
                fprintf(stdout, "Unknown packet type\n");
            }
        }
    }
}

void usage(){
    fprintf(stdout, "Supported command line arguments (all required)\n");
    fprintf(stdout, "-s : Source IP to bind to\n");
    fprintf(stdout, "-p : Source port\n");
}

int main(int argc, char *argv[]){
    char *srcIp = NULL, *srcPort = NULL;
    int32_t c;
    int32_t udpSockFd, tcpSockFd;

    //Mandatory arguments + values
    if(argc != 5){
        usage();
        exit(EXIT_FAILURE);
    }

    while((c = getopt(argc, argv, "s:p:")) != -1){
        switch(c){
            case 's':
                srcIp = optarg;
                break;
            case 'p':
                srcPort = optarg;
                break;
            default:
                usage();
                exit(EXIT_FAILURE);
        }
    }

    if(srcIp == NULL || srcPort == NULL){
        usage();
        exit(EXIT_FAILURE);
    } else {
        fprintf(stdout, "Source IP: %s:%s\n", srcIp, srcPort);
    }

    if((udpSockFd = bind_local(srcIp, srcPort, SOCK_DGRAM)) == -1){
        fprintf(stdout, "Could not create UDP socket\n");
        exit(EXIT_FAILURE);
    }

#if 0
    //I can have one UDP and one TCP socket bound to same port
    if((tcpSockFd = bind_local(srcIp, srcPort, SOCK_STREAM)) == -1){
        fprintf(stdout, "Could not create TCP socket\n");
        exit(EXIT_FAILURE);
    }
#endif

    networkEventLoop(udpSockFd);
    return 0;
}
