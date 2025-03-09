#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "checksum.h"

//Constraints 
#define MAX_PAYLOAD_SIZE 1400
#define HEADER_SIZE 7 
#define MAX_FILENAME_SIZE 100
#define MAX_PDU 1407

//Packet Flags 
#define FLAG_RR             5 
#define FLAG_SREJ           6
#define FLAG_FILENAME       8 
#define FLAG_FILENAME_ACK   9 
#define FLAG_EOF            10
#define FLAG_DATA           16
#define FLAG_RESENT_DATA    17
#define FLAG_RESENT_TIMEOUT 18
#define FLAG_FILENAME_ERROR 32

//Struct for packete 
typedef struct {
    uint32_t sequence_num; //In network order
    uint16_t checksum; //2 bytes
    uint8_t flag;      //1 byte
    uint8_t payload[MAX_PAYLOAD_SIZE]; 
}Packet;


int build_packet(uint8_t *packet, uint32_t seq_num, uint8_t flag, uint8_t *payload, int payload_size);

#endif
