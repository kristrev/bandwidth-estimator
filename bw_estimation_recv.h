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
