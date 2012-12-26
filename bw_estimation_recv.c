#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/times.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>

#include "bw_estimation_recv.h"
#include "bw_estimation_packets.h"

//Idea is that:
//Client sends a request to the sender stating desired bandwidth and duration
//This request is sent 10 times, five seconds apart, unless a reply is received
//If no reply is received 60 seconds after last reply, exit with failure
//A similar approach is taken for the sender when it comes to END_SESSION
//Removed log file, because writing to disk takes time. If required, pipe output
//Check out this timestamping.c example in documentation/networking

socklen_t fillSenderAddr(struct sockaddr_storage *senderAddr, char *senderIp, char *senderPort){
    socklen_t addrLen = 0;
    struct addrinfo hints, *servinfo;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;

    if((rv = getaddrinfo(senderIp, senderPort, &hints, &servinfo)) != 0){
        fprintf(stdout, "Getaddrinfo failed for sender address\n");
        return 0;
    }

    if(servinfo != NULL){
        memcpy(senderAddr, servinfo->ai_addr, servinfo->ai_addrlen);
        addrLen = servinfo->ai_addrlen;
    }

    freeaddrinfo(servinfo);
    return addrLen;
}

void networkLoop(int32_t udpSockFd, int16_t bandwidth, int16_t duration, \
        int16_t payloadLen, struct sockaddr_storage *senderAddr, socklen_t senderAddrLen){

    fd_set recvSet;
    fd_set recvSetCopy;
    int32_t fdmax = udpSockFd + 1;
    int32_t retval = 0;
    struct timeval tv;

    struct msghdr msg;
    struct iovec iov;
    bwrecv_state state = STARTING; 

    uint8_t buf[MAX_PAYLOAD_LEN] = {0};
    ssize_t numbytes = 0;
    uint8_t consecutiveRetrans = 0;
    uint8_t cmsg_buf[sizeof(struct cmsghdr) + sizeof(struct timespec)] = {0};

    //Configure the variables used for the select
    FD_ZERO(&recvSet);
    FD_ZERO(&recvSetCopy);
    FD_SET(udpSockFd, &recvSet);

    memset(&msg, 0, sizeof(struct msghdr));
    memset(&iov, 0, sizeof(struct iovec));

    iov.iov_base = buf;

    //TODO: Fix so that I now longer waste space, right now I always send
    //len(buf) bytes
    iov.iov_len = sizeof(struct newSessionPkt);

    msg.msg_name = (void *) senderAddr;
    msg.msg_namelen = senderAddrLen;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = (void *) cmsg_buf;
 
    //Send first NEW_SESSION-packet
    struct newSessionPkt *sessionPkt = (struct newSessionPkt *) buf; 
    sessionPkt->type = NEW_SESSION; 
    sessionPkt->duration = duration;
    sessionPkt->bw = bandwidth;
    sessionPkt->payload_len = payloadLen;

    numbytes = sendmsg(udpSockFd, &msg, 0);
    if(numbytes <= 0){
        fprintf(stdout, "Failed to send initial NEW_SESSION message\n");
        exit(EXIT_FAILURE);
    } else {
        fprintf(stdout, "Sent first NEW_SESSION message\n");
    }

    while(1){
        recvSetCopy = recvSet;
        if(state == STARTING){
            tv.tv_sec = NEW_SESSION_TIMEOUT;
            tv.tv_usec = 0;
        } else {
            //The timeout is reset for each data packet I receive
            tv.tv_sec = duration;
            tv.tv_usec = 0;
        }

        retval = select(fdmax, &recvSetCopy, NULL, NULL, &tv);

        if(retval == 0){
            //Reason for using EXIT_FAILURE here is that these are not clean
            //resets, something went wrong
            if(state == RECEIVING){
                fprintf(stdout, "%d seconds passed without any traffic, aborting\n", duration); 
                exit(EXIT_FAILURE);
            } else if(consecutiveRetrans == RETRANSMISSION_THRESHOLD){
                fprintf(stdout, "Did not receive any reply to NEW_SESSION, aborting\n");
                exit(EXIT_FAILURE);
            } else {
                //Send retransmission
                fprintf(stdout, "Retransmitting NEW_SESSION. Consecutive retransmissions %d\n", ++consecutiveRetrans);
                sendmsg(udpSockFd, &msg, 0);
            }
        } else {
            msg.msg_controllen = sizeof(cmsg_buf);
            iov.iov_len = sizeof(buf);
            numbytes = recvmsg(udpSockFd, &msg, 0); 
            //Recv packet
        }

    }

    numbytes = sendmsg(udpSockFd, &msg, 0);
    fprintf(stdout, "Sent %zd bytes\n", numbytes);
}

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

    //TODO: Improve to use hardware timestamps, if available
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

void usage(){
    fprintf(stdout, "Supported command line arguments\n");
    fprintf(stdout, "-b : Bandwidth (in Mbit/s, only integers)\n");
    fprintf(stdout, "-t : Duration of test (in seconds)\n");
    fprintf(stdout, "-l : Payload length (in bytes)\n");
    fprintf(stdout, "-s : Source IP to bind to\n");
    fprintf(stdout, "-d : Destion IP\n");
    fprintf(stdout, "-p : Destion port\n");
}

int main(int argc, char *argv[]){
    uint16_t bandwidth = 0, duration = 0, payloadLen = 0;
    char *srcIp = NULL, *senderIp = NULL, *senderPort = NULL;
    int32_t c, udpSockFd = -1;
    struct sockaddr_storage senderAddr;
    socklen_t senderAddrLen = 0;
    char addrPresentation[INET6_ADDRSTRLEN];

    //Mandatory arguments + values
    if(argc != 13){
        usage();
        exit(EXIT_FAILURE);
    }

    while((c = getopt(argc, argv, "b:t:l:s:d:p:")) != -1){
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

    if(payloadLen > MAX_PAYLOAD_LEN){
        fprintf(stdout, "Payload length exceeds limit (%d)\n", MAX_PAYLOAD_LEN);
        exit(EXIT_FAILURE);
    }

    //Use stdout for all non-essential information
    fprintf(stdout, "Bandwidth %d Mbit/s, Duration %d sec, Payload length %d bytes, Source IP %s\n", \
            bandwidth, duration, payloadLen, srcIp);

    //Bind UDP socket
    if((udpSockFd = bind_local(srcIp, NULL, SOCK_DGRAM)) == -1){
        fprintf(stdout, "Binding to local IP failed\n");
        exit(EXIT_FAILURE);
    }

    fprintf(stdout, "UDP socket %d\n", udpSockFd);

    memset(&senderAddr, 0, sizeof(struct sockaddr_storage));
    
    if(!(senderAddrLen = fillSenderAddr(&senderAddr, senderIp, senderPort))){
        fprintf(stdout, "Could not fill sender address struct. Is the address correct?\n");
        exit(EXIT_FAILURE);
    }

    if(senderAddr.ss_family == AF_INET){
        inet_ntop(AF_INET, &(((struct sockaddr_in *) &senderAddr)->sin_addr),\
                addrPresentation, INET6_ADDRSTRLEN);
        fprintf(stdout, "Sender (IPv4) %s:%s\n", addrPresentation, senderPort);
    } else if(senderAddr.ss_family == AF_INET6){
        inet_ntop(AF_INET6, &(((struct sockaddr_in6 *) &senderAddr)->sin6_addr),\
                addrPresentation, INET6_ADDRSTRLEN);
        fprintf(stdout, "Sender (IPv6) %s:%s\n", addrPresentation, senderPort);
    }

    networkLoop(udpSockFd, bandwidth, duration, payloadLen, &senderAddr, senderAddrLen);

    return 0;
}
