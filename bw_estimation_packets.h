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

#include <stdint.h>

//To be on the safe size wrt headers and stuff
#define MAX_PAYLOAD_LEN 1000

typedef enum{
    NEW_SESSION=0, //The sender should start a new send session
    DATA, //Pkt shall be used for measurement
    END_SESSION, //Sent from sender after last data packet
    SENDER_FULL
} pkt_type;

#define BW_MANDATORY \
    uint8_t type;

struct pkt_hdr{
    uint8_t type;  
};

//Will I need a timestamp? All NEW_SESSION packets have same src/port, so I 
//can just check for that
struct new_session_pkt{
    BW_MANDATORY;
    uint16_t duration;
    uint16_t bw; //Bw in Mbit/s, only allow integers for now
    uint16_t payload_len;
	uint16_t iat; //Time between packets in ms, only used for TCP
};

struct data_pkt{
    BW_MANDATORY;
    //Buffers are MAX_PAYLOAD_LEN, so I dont need a separate buf here
};

//Not sure if I need more here
struct end_session_pkt{
    BW_MANDATORY;  
};
