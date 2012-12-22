#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>

#include "bw_estimation_packets.h"

//Idea is that:
//Client sends a request to the sender stating desired bandwidth and duration
//This request is sent 10 times, five seconds apart, unless a reply is received
//If no reply is received 60 seconds after last reply, exit with failure
//A similar approach is taken for the sender when it comes to END_SESSION

void usage(){
    fprintf(stderr, "All command line arguments are required\n");
    fprintf(stderr, "-b : Bandwidth (in Mbit/s, only integers)\n");
    fprintf(stderr, "-t : Duration of test (in seconds)\n");
    fprintf(stderr, "-l : Payload length (in bytes)\n");
    fprintf(stderr, "-s : Source IP to bind to\n");
    fprintf(stderr, "-d : Destion IP\n");
    fprintf(stderr, "-p : Destion port\n");
}

int main(int argc, char *argv[]){
    uint16_t bandwidth = 0, duration = 0, payloadLen = 0;
    uint8_t *srcIp = NULL, *senderIp = NULL, *senderPort = NULL;
    int32_t c;

    //Arguments + values
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

    //Bind UDP socket

    return 0;
}
