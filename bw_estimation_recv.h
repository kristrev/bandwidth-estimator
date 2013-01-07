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

#ifndef BW_ESTIMATION_RECV
#define BW_ESTIMATION_RECV

#define NEW_SESSION_TIMEOUT 5 //Timeout between new session packets
#define RETRANSMISSION_THRESHOLD 5
#define DEFAULT_TIMEOUT 60 //Timeout waiting for the next data pack/END_SESSION

//Define three states, STARTING, RECEIVING, ENDING
//- STARTING: Send NEW_SESSION every TIMEOUT second, up to N times. Abort if
//no reply is received X seconds after last message
//- RECEIVING: SESSION started, receiving data. Wait DURATION seconds for
//data, move to ENDING after DURATION has passed. Abort if no packets have
//been received. Reset DURATION upon first packet
//- ENDING: Wait ENDING_TIMEOUT seconds before aborting. This state is
//included in receiving

typedef enum{
    STARTING = 0,
    RECEIVING //Receiving also covers the ending state, as the timer is reset for each data packet
} bwrecv_state;

#endif
