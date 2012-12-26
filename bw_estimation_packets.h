#include <stdint.h>

#define MAX_PAYLOAD_LEN 1500

typedef enum{
    NEW_SESSION=0, //The sender should start a new send session
    DATA, //Pkt shall be used for measurement
    END_SESSION, //Sent from sender after last data packet
} pktType;

#define BW_MANDATORY \
    uint8_t type;

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
    uint8_t *buf; //Can't use a constant as buffer size, it is set at runtime
};

//Not sure if I need more here
struct endSessionPkt{
    BW_MANDATORY;  
};
