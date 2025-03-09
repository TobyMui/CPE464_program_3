#include "communication.h"

/*This Function is for building packets
  For building headers, set payload to NULL and payload_size to 0*/
  int build_packet(uint8_t *packet, uint32_t seq_num, uint8_t flag, uint8_t *payload, int payload_size){
    memset(packet, 0, 7 + payload_size); // Zero out the packet
    int packet_len = 0;

    // Set sequence number (bytes 0-3)
    uint32_t net_seq_num = htonl(seq_num);
    memcpy(packet, &net_seq_num, 4);

    // Set flag (byte 6)
    packet[6] = flag;

    // Copy payload if provided
    if (payload != NULL && payload_size > 0)
    {
        memcpy(packet + 7, payload, payload_size);
        packet_len = HEADER_SIZE + payload_size;
    }else{
        packet_len = HEADER_SIZE;
    }

    // Compute checksum (bytes 4-5)
    memset(packet + 4, 0, 2); // Reset checksum field to 0 before computing
    uint16_t checksum = in_cksum((unsigned short *)packet, 7 + payload_size);
    memcpy(packet + 4, &checksum, 2);

    return packet_len;
}
