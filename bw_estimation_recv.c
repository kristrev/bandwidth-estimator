/*
 * Copyright 2013 Kristian Evensen <kristian.evensen@gmail.com>
 *
 * This file is part of Bandwidth Estimator. Bandwidth Estimator is free
 * software: you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * Bandwidth Estimatior is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * Bandwidth Estimator. If not, see http://www.gnu.org/licenses/.
 */

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

//Fill the sender addr that will be used with sendmsg
socklen_t fill_sender_addr(struct sockaddr_storage *sender_addr, 
        char *sender_ip, char *sender_port){
    socklen_t addr_len = 0;
    struct addrinfo hints, *servinfo;
    int32_t rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;

    if((rv = getaddrinfo(sender_ip, sender_port, &hints, &servinfo)) != 0){
        fprintf(stdout, "Getaddrinfo failed for sender address\n");
        return 0;
    }

    if(servinfo != NULL){
        memcpy(sender_addr, servinfo->ai_addr, servinfo->ai_addrlen);
        addr_len = servinfo->ai_addrlen;
    }

    freeaddrinfo(servinfo);
    return addr_len;
}

//These two functions could be merged, but I think the code is cleaner if they
//are separated
void network_loop_tcp(int32_t tcp_sock_fd, int16_t duration, FILE *output_file){
    //Related to select
    fd_set recv_set;
    fd_set recv_set_copy;
    int32_t fdmax = tcp_sock_fd + 1;
    int32_t retval;
    size_t total_number_bytes = 0;

    //Estimation
    struct timeval t0, t1;
    double data_interval;
    double estimated_bandwidth;
    
    //Receiving packet + control data
    struct msghdr msg;
    struct iovec iov;
    uint8_t buf[MAX_PAYLOAD_LEN] = {0};
    ssize_t numbytes;

    FD_ZERO(&recv_set);
    FD_ZERO(&recv_set_copy);
    FD_SET(tcp_sock_fd, &recv_set);
    memset(&msg, 0, sizeof(struct msghdr));
    memset(&iov, 0, sizeof(struct iovec));

    iov.iov_base = buf;
    iov.iov_len = MAX_PAYLOAD_LEN;
    //Timestamping does not work with TCP due to collapsing of packets
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    while(1){
        recv_set_copy = recv_set;
        retval = select(fdmax, &recv_set_copy, NULL, NULL, NULL); 

        iov.iov_len = sizeof(buf);
        numbytes = recvmsg(tcp_sock_fd, &msg, 0);

        //Server had to close connection
        if(numbytes <= 0)
            break;

        //This will be less accurate than for UDP, using application layer
        //timestamps
        if(total_number_bytes == 0)
            gettimeofday(&t0, NULL);

        gettimeofday(&t1, NULL);
        if(output_file)
            fprintf(output_file, "%lu.%lu %zd\n", t1.tv_sec, t1.tv_usec,
                    numbytes);
        total_number_bytes += numbytes;

        if((t1.tv_sec - t0.tv_sec) > duration)
            break;
    }

    if(total_number_bytes > 0){
        data_interval = (t1.tv_sec - t0.tv_sec) +
                       ((t1.tv_usec - t0.tv_usec)/1000000.0);
        estimated_bandwidth = ((total_number_bytes / 1000000.0) * 8)
                             / data_interval;
        fprintf(stdout, "Received %zd bytes in %.2f seconds. Estimated"
                "bandwidth %.2f Mbit/s\n", total_number_bytes, data_interval,
                estimated_bandwidth);
    } else {
        fprintf(stdout, "Received no data from server. All threads busy?\n");
    }
        
    close(tcp_sock_fd);
}

void network_loop_udp(int32_t udp_sock_fd, int16_t bandwidth, int16_t duration,
        int16_t payload_len, struct sockaddr_storage *sender_addr, 
        socklen_t sender_addr_len, FILE *output_file){

    fd_set recv_set;
    fd_set recv_set_copy;
    int32_t fdmax = udp_sock_fd + 1;
    int32_t retval = 0;
    struct timeval tv;
    size_t total_number_bytes = 0;
    struct timespec t0, t1;
    double data_interval;
    double estimated_bandwidth;

    struct msghdr msg;
    struct iovec iov;
    bwrecv_state state = STARTING; 

    uint8_t buf[MAX_PAYLOAD_LEN] = {0};
    ssize_t numbytes = 0;
    uint8_t consecutive_retrans = 0;
    struct cmsghdr *cmsg;
    uint8_t cmsg_buf[sizeof(struct cmsghdr) + sizeof(struct timespec)] = {0};
    struct timespec *recv_time;
    struct pkt_hdr *hdr = NULL;

    //Configure the variables used for the select
    FD_ZERO(&recv_set);
    FD_ZERO(&recv_set_copy);
    FD_SET(udp_sock_fd, &recv_set);

    memset(&msg, 0, sizeof(struct msghdr));
    memset(&iov, 0, sizeof(struct iovec));

    iov.iov_base = buf;
    iov.iov_len = sizeof(struct new_session_pkt);

    msg.msg_name = (void *) sender_addr;
    msg.msg_namelen = sender_addr_len;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = (void *) cmsg_buf;
 
    //Send first NEW_SESSION-packet
    struct new_session_pkt *session_pkt = (struct new_session_pkt *) buf; 
    session_pkt->type = NEW_SESSION; 
    session_pkt->duration = duration;
    session_pkt->bw = bandwidth;
    session_pkt->payload_len = payload_len;

    numbytes = sendmsg(udp_sock_fd, &msg, 0);
    if(numbytes <= 0){
        fprintf(stdout, "Failed to send initial NEW_SESSION message\n");
        exit(EXIT_FAILURE);
    } else {
        fprintf(stdout, "Sent first NEW_SESSION message\n");
    }

    while(1){
        recv_set_copy = recv_set;
        if(state == STARTING){
            tv.tv_sec = NEW_SESSION_TIMEOUT;
            tv.tv_usec = 0;
        } else {
            //The timeout is reset for each data packet I receive
            tv.tv_sec = duration > DEFAULT_TIMEOUT ? duration : DEFAULT_TIMEOUT; 
            tv.tv_usec = 0;
        }

        retval = select(fdmax, &recv_set_copy, NULL, NULL, &tv);
        if(retval == 0){
            if(state == RECEIVING){
                //Might be able to compute somethig, therefore break
                fprintf(stdout, "%d seconds passed without any traffic," 
                        "aborting\n", duration); 
                break;
            } else if(consecutive_retrans == RETRANSMISSION_THRESHOLD){
                fprintf(stdout, "Did not receive any reply to NEW_SESSION," 
                        "aborting\n");
                exit(EXIT_FAILURE);
            } else {
                //Send retransmission
                fprintf(stdout, "Retransmitting NEW_SESSION. Consecutive " 
                        "retransmissions %d\n", ++consecutive_retrans);
                sendmsg(udp_sock_fd, &msg, 0);
            }
        } else {
            msg.msg_controllen = sizeof(cmsg_buf);
            iov.iov_len = sizeof(buf);
            numbytes = recvmsg(udp_sock_fd, &msg, 0); 
            hdr = (struct pkt_hdr*) buf;

            if(hdr->type == DATA){
                cmsg = CMSG_FIRSTHDR(&msg);
                if(cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == 
                        SO_TIMESTAMPNS){
                    recv_time = (struct timespec *) CMSG_DATA(cmsg);
                    if(output_file != NULL)
                        fprintf(output_file, "%lu.%lu %zd\n", recv_time->tv_sec, 
                                recv_time->tv_nsec, numbytes);
                }

                if(state == STARTING){
                    memcpy(&t0, recv_time, sizeof(struct timespec));
                    state = RECEIVING;
                }

                memcpy(&t1, recv_time, sizeof(struct timespec));
                total_number_bytes += numbytes;
            } else if(hdr->type == END_SESSION){
                fprintf(stdout, "End session\n");
                break;
            } else if(hdr->type == SENDER_FULL){
                fprintf(stdout, "Sender is full, cant serve more clients\n");
                break;
            } else {
                fprintf(stdout, "Unkown\n");
            }
        }

    }

    if(total_number_bytes > 0){
        data_interval = (t1.tv_sec - t0.tv_sec) + 
            ((t1.tv_nsec - t0.tv_nsec)/1000000000.0);
        estimated_bandwidth = 
            ((total_number_bytes / 1000000.0) * 8) / data_interval;
        //Computations?
        fprintf(stdout, "Received %zd bytes in %.2f seconds. Estimated " 
                "bandwidth %.2f Mbit/s\n", total_number_bytes, data_interval, 
                estimated_bandwidth);
    }

    close(udp_sock_fd);
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
      fprintf(stdout, "Socket:");
      continue;
    }

    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      close(sockfd);
      fprintf(stdout, "Setsockopt (reuseaddr)");
      continue;
    }

    if(setsockopt(sockfd, SOL_SOCKET, SO_TIMESTAMPNS, &yes, sizeof(int)) == -1){
      close(sockfd);
      fprintf(stdout, "Setsockopt (timestamp)");
      continue;
    }

    if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
      close(sockfd);
      fprintf(stdout, "Bind (local)");
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
    fprintf(stdout, "-b : Bandwidth (in Mbit/s, only integers and only needed" 
            "with UDP)\n");
    fprintf(stdout, "-t : Duration of test (in seconds)\n");
    fprintf(stdout, "-l : Payload length (in bytes), only needed with UDP\n");
    fprintf(stdout, "-s : Source IP to bind to\n");
    fprintf(stdout, "-d : Destion IP\n");
    fprintf(stdout, "-p : Destion port\n");
    fprintf(stdout, "-r : Use TCP (reliable) instead of UDP\n");
    fprintf(stdout, "-w : Provide an optional filename for writing the "\
            "packet receive times\n");

}

int main(int argc, char *argv[]){
    uint8_t use_tcp = 0;
    uint16_t bandwidth = 0, duration = 0, payload_len = 0;
    char *src_ip = NULL, *sender_ip = NULL, *sender_port = NULL, 
         *file_name = NULL;
    int32_t retval, socket_fd = -1, socktype = SOCK_DGRAM;
    struct sockaddr_storage sender_addr;
    socklen_t sender_addr_len = 0;
    char addr_presentation[INET6_ADDRSTRLEN];
    FILE *output_file = NULL;

    while((retval = getopt(argc, argv, "b:t:l:s:d:p:w:r")) != -1){
        switch(retval){
            case 'b':
                bandwidth = atoi(optarg);
                break;
            case 't':
                duration = atoi(optarg);
                break;
            case 'l':
                payload_len = atoi(optarg);
                break;
            case 's':
                src_ip = optarg;
                break;
            case 'd':
                sender_ip = optarg;
                break;
            case 'p':
                sender_port = optarg;
                break;
            case 'w':
                file_name = optarg;
                break;
            case 'r':
                use_tcp = 1;
                break;
            default:
                usage();
                exit(EXIT_FAILURE);
        }
    }

    if(duration == 0 || src_ip == NULL || sender_ip == NULL || 
            sender_port == NULL){
        usage();
        exit(EXIT_FAILURE);
    }

    if(!use_tcp && (bandwidth == 0 || payload_len == 0)){
        usage();
        exit(EXIT_FAILURE);
    }

    if(payload_len > MAX_PAYLOAD_LEN){
        fprintf(stdout, "Payload length exceeds limit (%d)\n", MAX_PAYLOAD_LEN);
        exit(EXIT_FAILURE);
    }

    if(file_name != NULL && ((output_file = fopen(file_name, "w")) == NULL)){
        fprintf(stdout, "Failed to open output file\n");
        exit(EXIT_FAILURE);
    }

    //Use stdout for all non-essential information
    fprintf(stdout, "Bandwidth %d Mbit/s, Duration %d sec, Payload length " 
            "%d bytes, Source IP %s\n", bandwidth, duration, payload_len, src_ip);

    if(use_tcp)
        socktype = SOCK_STREAM;

    //Bind network socket
    if((socket_fd = bind_local(src_ip, NULL, socktype)) == -1){
        fprintf(stdout, "Binding to local IP failed\n");
        exit(EXIT_FAILURE);
    }

    fprintf(stdout, "Network socket %d\n", socket_fd);

    memset(&sender_addr, 0, sizeof(struct sockaddr_storage));
    
    if(!(sender_addr_len = fill_sender_addr(&sender_addr, sender_ip, sender_port))){
        fprintf(stdout, "Could not fill sender address struct. Is the address " 
                "correct?\n");
        exit(EXIT_FAILURE);
    }

    //I could just have outputted the command line paramteres, but this serves
    //asa nice example on how use inet_ntop + sockaddr_storage
    if(sender_addr.ss_family == AF_INET){
        inet_ntop(AF_INET, &(((struct sockaddr_in *) &sender_addr)->sin_addr),
                addr_presentation, INET6_ADDRSTRLEN);
        fprintf(stdout, "Sender (IPv4) %s:%s\n", addr_presentation, sender_port);
    } else if(sender_addr.ss_family == AF_INET6){
        inet_ntop(AF_INET6, &(((struct sockaddr_in6 *) &sender_addr)->sin6_addr),
                addr_presentation, INET6_ADDRSTRLEN);
        fprintf(stdout, "Sender (IPv6) %s:%s\n", addr_presentation, sender_port);
    }

    if(use_tcp){
        //I could use connect with UDP too, but I have some bad experiences with
        //side-effects of doing that.
        if(sender_addr.ss_family == AF_INET)
            retval = connect(socket_fd, (struct sockaddr *) &sender_addr, 
                    sizeof(struct sockaddr_in));
        else
            retval = connect(socket_fd, (struct sockaddr *) &sender_addr, 
                    sizeof(struct sockaddr_in6));

        if(retval < 0){
            fprintf(stdout, "Could not connect to sender, aborting\n");
            exit(EXIT_FAILURE);
        }

        network_loop_tcp(socket_fd, duration, output_file);
    } else
        network_loop_udp(socket_fd, bandwidth, duration, payload_len, &sender_addr, 
                sender_addr_len, output_file);

    return 0;
}
