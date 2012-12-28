#include <stdio.h>
#include <stdint.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>

#include "bw_estimation_packets.h"

#define NUM_THREADS 4
#define NUM_END_SESSION_PKTS 10

typedef enum{
    PAUSED,
    RUNNING
} threadStatus;

//Make multithreaded. Need a hashmap here
struct threadInfo{
    threadStatus status;
    int32_t udpSockFd;
    int32_t tcpSockFd;
    struct sockaddr_storage source;
    uint16_t bandwidth;
    uint16_t duration;
    uint16_t payloadLen;
    pthread_cond_t newSession;
    pthread_mutex_t newSessionMutex;
};

//Bind the local socket. Should work with both IPv4 and IPv6
int bind_local(char *local_addr, char *local_port, int socktype, uint8_t listenSocket){
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

    if(listenSocket && listen(sockfd, NUM_THREADS) == -1){
        close(sockfd);
        perror("Connect");
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

uint64_t generateTcpTraffic(struct threadInfo *threadInfo){
    struct msghdr msg;
    struct iovec iov;
    uint8_t buf[MAX_PAYLOAD_LEN] = {DATA};
    int32_t numbytes = 0;
    uint64_t tot_bytes = 0;

    memset(&msg, 0, sizeof(struct msghdr));
    memset(&iov, 0, sizeof(struct iovec));

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    iov.iov_base = buf;
    iov.iov_len = MAX_PAYLOAD_LEN;

    //With TCP, it is is sufficient to send data until the socket returns an
    //error (closed by peer)
    while((numbytes = sendmsg(threadInfo->tcpSockFd, &msg, 0)) > 0)
            tot_bytes += numbytes;

    return tot_bytes;
}

uint64_t generateUdpTraffic(struct threadInfo *threadInfo){
    //Variables used to compute and keep the desired bandwidth
    struct timeval t0_p, t1_p;
    time_t t0, t1;
    double pkts_per_sec = 0; 
    double desired_iat = 0;
    double iat = 0;
    double adjust = 0;
    int32_t i;
    uint64_t tot_bytes = 0;

    //Used for sending packet
    struct msghdr msg;
    struct iovec iov;
    uint8_t buf[MAX_PAYLOAD_LEN] = {DATA};

    memset(&msg, 0, sizeof(struct msghdr));
    memset(&iov, 0, sizeof(struct iovec));
    msg.msg_name = (void *) &(threadInfo->source);
    msg.msg_namelen = sizeof(struct sockaddr_storage);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    iov.iov_base = buf;
    iov.iov_len = threadInfo->payloadLen;

    pkts_per_sec = (threadInfo->bandwidth * 1000 * 1000) / (double) (threadInfo->payloadLen * 8);
    desired_iat = 1000000 / pkts_per_sec; //IAT is microseconds, sufficient resolution
    fprintf(stdout, "Bandiwdth of %d Mbit/s, duration %ds, payload length %d byte\n", 
            threadInfo->bandwidth, threadInfo->duration, threadInfo->payloadLen);
    fprintf(stdout, "Sending %f packets/s, IAT %f microseconds\n", pkts_per_sec, desired_iat);

    t0 = time(NULL);
    gettimeofday(&t0_p, NULL);
    
    //Remember to reset values
    buf[0] = DATA;
    iat = 0;
    tot_bytes = 0;

    while(1){
        gettimeofday(&t1_p, NULL);  
        //See notebook for order, goal is that difference should be 0. I want to find how much more/less I should
        //sleep this time to provide the desired IAT. That adjustment is desired_iat + the time it took for the last packet
        //to be processed.
        //If it it took longer than the desired IAT, the adjustment will be negative and "this" packet have to sleep less
        //If it took a shorter time than desiread IAT, this packet will have to be delayed a little bit
        adjust = desired_iat + (((t0_p.tv_sec - t1_p.tv_sec) * 1000000) + (t0_p.tv_usec - t1_p.tv_usec));
        t0_p.tv_sec = t1_p.tv_sec;
        t0_p.tv_usec = t1_p.tv_usec;

        if(adjust > 0 || iat > 0)
          iat += adjust;

        tot_bytes += sendmsg(threadInfo->udpSockFd, &msg, 0);

        //Check if it is time to abort
        t1 = time(NULL);

        //Must "include" previous second
        if(difftime(t1,t0) > threadInfo->duration){
            break;
        }

        if(iat > 0)
            usleep(iat);
    }

    //Easy solution for sending end_session
    buf[0] = END_SESSION;
    for(i = 0; i<NUM_END_SESSION_PKTS; i++)
        sendmsg(threadInfo->udpSockFd, &msg, 0);

    return tot_bytes;
}

void *sendLoop(void *data){
    uint64_t tot_bytes = 0;
    struct threadInfo *threadInfo = (struct threadInfo *) data;
    fprintf(stdout, "Started thread\n");

    while(1){
        pthread_mutex_lock(&(threadInfo->newSessionMutex));
        if(threadInfo->status == PAUSED)
            pthread_cond_wait(&(threadInfo->newSession), &(threadInfo->newSessionMutex));
        pthread_mutex_unlock(&(threadInfo->newSessionMutex));

        //Sanity
        assert(threadInfo->status == RUNNING);

        if(threadInfo->tcpSockFd > 0){
            tot_bytes = generateTcpTraffic(threadInfo);
            close(threadInfo->tcpSockFd);
            threadInfo->tcpSockFd = 0; 
        } else {
            tot_bytes = generateUdpTraffic(threadInfo); 
        }

       
        fprintf(stdout, "Done with sending. Sent %lu bytes\n", tot_bytes);
        //Send end session
        threadInfo->status = PAUSED;
    }
}

void networkEventLoop(int32_t udpSockFd, int32_t tcpSockFd){
    fd_set recvSet, recvSetCopy;
    int32_t fdmax = (udpSockFd > tcpSockFd ? udpSockFd : tcpSockFd) + 1;
    int32_t retval = 0, i;
    ssize_t numbytes = 0;
    int32_t recvSocket = 0;
    struct pktHdr *hdr;
    struct newSessionPkt *newSPkt;

    pthread_t threads[NUM_THREADS];
    struct threadInfo *threadInfos[NUM_THREADS];

    struct msghdr msg;
    struct iovec iov;
    uint8_t buf[MAX_PAYLOAD_LEN] = {0};
    struct sockaddr_storage senderAddr;
    socklen_t addrLen;
    char addrPresentation[INET6_ADDRSTRLEN];
    uint16_t recvPort = 0;
    struct dataPkt *pkt = NULL;

    //Initialize and start threads used to generate traffic
    for(i = 0; i<NUM_THREADS; i++){
        threadInfos[i] = (struct threadInfo*) malloc(sizeof(struct threadInfo));
        threadInfos[i]->status = PAUSED;
        threadInfos[i]->udpSockFd = udpSockFd;
        pthread_cond_init(&(threadInfos[i]->newSession), NULL);
        pthread_mutex_init(&(threadInfos[i]->newSessionMutex), NULL);

        pthread_create(&threads[i], NULL, sendLoop, (void *) threadInfos[i]);
    }

    memset(&msg, 0, sizeof(struct msghdr));
    memset(&iov, 0, sizeof(struct iovec));
    memset(&senderAddr, 0, sizeof(struct sockaddr_storage));

    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);

    msg.msg_name = (void *) &senderAddr;

    //Unlike for example recvfrom, this one seems to be left unchanged
    msg.msg_namelen = sizeof(struct sockaddr_storage);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    FD_ZERO(&recvSet);
    FD_ZERO(&recvSetCopy);
    FD_SET(udpSockFd, &recvSet);
    FD_SET(tcpSockFd, &recvSet);

    while(1){
        //Use this an indication for wheter or not new session is using TCP
        recvSocket = 0;
        recvSetCopy = recvSet;
        retval = select(fdmax, &recvSetCopy, NULL, NULL, NULL);

        if(retval < 0){
            fprintf(stdout, "Select failed, aborting\n");
            close(udpSockFd);
            exit(EXIT_FAILURE);
        } else {
            if (FD_ISSET(tcpSockFd, &recvSetCopy)){
                addrLen = sizeof(struct sockaddr_storage);
                if((recvSocket = accept(tcpSockFd, (struct sockaddr*) &senderAddr, &addrLen)) == -1){
                    fprintf(stdout, "Failed connection attempt\n");
                }
            } else {
                numbytes = recvmsg(udpSockFd, &msg, 0);
                hdr = (struct pktHdr *) buf;

                if(hdr->type != NEW_SESSION){
                    fprintf(stdout, "Received an incorrect packet\n");
                    continue;
                }
            }

            //The combination adress + port is used for lookup in the currently
            //running threads
            if(senderAddr.ss_family == AF_INET){
                inet_ntop(AF_INET, &(((struct sockaddr_in *) &senderAddr)->sin_addr),\
                        addrPresentation, INET6_ADDRSTRLEN);
                recvPort = ntohs(((struct sockaddr_in *) &senderAddr)->sin_port);
            } else if(senderAddr.ss_family == AF_INET6){
                inet_ntop(AF_INET6, &(((struct sockaddr_in6 *) &senderAddr)->sin6_addr),\
                        addrPresentation, INET6_ADDRSTRLEN);
                recvPort = ntohs(((struct sockaddr_in6 *) &senderAddr)->sin6_port);
            }

            //Check that I have not already started the thread belonging to
            //this session
            //The checks in this and the next for-loop are not thread-safe,
            //but it doesn't matter as the behvaior is not critical.
            //Wors-case, the session request will be accepted on the next
            //request
            for(i = 0; i<NUM_THREADS; i++){
                if(threadInfos[i]->status == RUNNING && \
                        threadInfos[i]->source.ss_family == senderAddr.ss_family){
                    if(senderAddr.ss_family == AF_INET && \
                            !memcmp(&senderAddr, &threadInfos[i]->source, \
                            sizeof(struct sockaddr_in))){
                        break;
                    } else if(senderAddr.ss_family == AF_INET6 && \
                            !memcmp(&senderAddr, &threadInfos[i]->source, \
                            sizeof(struct sockaddr_in6))){
                        break;
                    }
                }
            }

            if(i!=NUM_THREADS){
                fprintf(stdout, "This session has already been seen (%s:%d)\n", addrPresentation, recvPort);
                continue;
            }

            for(i=0; i<NUM_THREADS; i++){
                //Found an availale thread. Initialise and start
                if(threadInfos[i]->status == PAUSED){
                    if(recvSocket > 0){
                        threadInfos[i]->tcpSockFd = recvSocket;
                    } else{
                        newSPkt = (struct newSessionPkt *) buf;
                        threadInfos[i]->duration = newSPkt->duration;
                        threadInfos[i]->bandwidth = newSPkt->bw;
                        threadInfos[i]->payloadLen = newSPkt->payload_len;
                    }

                    memcpy(&threadInfos[i]->source, &senderAddr, sizeof(struct sockaddr_storage));
                    
                    //Signal thread that it has work to do
                    pthread_mutex_lock(&(threadInfos[i]->newSessionMutex));
                    threadInfos[i]->status = RUNNING;
                    pthread_cond_signal(&(threadInfos[i]->newSession));
                    pthread_mutex_unlock(&(threadInfos[i]->newSessionMutex));
                    fprintf(stdout, "Created a new session for %s:%d\n", addrPresentation, recvPort);
                    break;
                }
            }

            if(i==NUM_THREADS){
                fprintf(stdout, "No available threads\n");

                //Create SENDER_FULL packet, which only consis of 1 byte payload
                buf[0] = SENDER_FULL;
                iov.iov_len = 1;

                if(recvSocket > 0){
                    close(recvSocket);
                } else
                    sendmsg(udpSockFd, &msg, 0);

                iov.iov_len = sizeof(buf);
                //Send message that sender is full
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

    if((udpSockFd = bind_local(srcIp, srcPort, SOCK_DGRAM, 0)) == -1){
        fprintf(stdout, "Could not create UDP socket\n");
        exit(EXIT_FAILURE);
    }

    //I can have one UDP and one TCP socket bound to same port
    if((tcpSockFd = bind_local(srcIp, srcPort, SOCK_STREAM, 1)) == -1){
        fprintf(stdout, "Could not create TCP socket\n");
        exit(EXIT_FAILURE);
    }

    networkEventLoop(udpSockFd, tcpSockFd);
    return 0;
}
