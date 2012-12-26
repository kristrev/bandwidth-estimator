#include <stdint.h>

//To be on the safe size wrt headers and stuff
#define MAX_PAYLOAD_LEN 1400

typedef enum{
    NEW_SESSION=0, //The sender should start a new send session
    DATA, //Pkt shall be used for measurement
    END_SESSION, //Sent from sender after last data packet
} pktType;

#define BW_MANDATORY \
    uint8_t type;

struct pktHdr{
    uint8_t type;  
};

//Will I need a timestamp? All NEW_SESSION packets have same src/port, so I can just check
//for that
struct newSessionPkt{
    BW_MANDATORY;
    uint16_t duration;
    uint16_t bw; //Bw in Mbit/s, only allow integers for now
    uint16_t payload_len;
};

struct dataPkt{
    BW_MANDATORY;
    //Buffers are MAX_PAYLOAD_LEN, so I dont need a separate buf here
};

//Not sure if I need more here
struct endSessionPkt{
    BW_MANDATORY;  
};
