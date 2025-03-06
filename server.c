/* Server side - UDP Code				    */
/* By Hugh Smith	4/1/2017	*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "communication.h"
#include "checksum.h"
#include "cpe464.h"
#include "buffer.h"
#include "pollLib.h"

#define MAXBUF 80

float ERROR_RATE = 0.0; 

void server_FSM(int socketNum);
int checkArgs(int argc, char *argv[]);

typedef enum{
    DONE, FILENAME_ACK, SEND_DATA
}ServerState; 

int main ( int argc, char *argv[]  )
{ 
    
	int socketNum = 0;				
	int portNumber = 0;

	portNumber = checkArgs(argc, argv);

    //Get socket number and add to poll set.
	socketNum = udpServerSetup(portNumber);
    setupPollSet();
    addToPollSet(socketNum);

	server_FSM(socketNum);

	close(socketNum);

	return 0;
}


/*This Function is for building packets
  For building headers, set payload to NULL and payload_size to 0*/
int build_packet(uint8_t *packet, uint32_t seq_num, uint8_t flag, uint8_t *payload, int payload_size) {
    memset(packet, 0, 7 + payload_size);  // Zero out the packet
    int packet_len = 0; 

    // Set sequence number (bytes 0-3)
    uint32_t net_seq_num = htonl(seq_num);
    memcpy(packet, &net_seq_num, 4);

    // Set flag (byte 6)
    packet[6] = flag;

    // Copy payload if provided
    if (payload != NULL && payload_size > 0) {
        memcpy(packet + 7, payload, payload_size);
        packet_len = HEADER_SIZE + payload_size;
    }else{
        packet_len = HEADER_SIZE; 
    }

    // Compute checksum (bytes 4-5)
    memset(packet + 4, 0, 2);  // Reset checksum field to 0 before computing
    uint16_t checksum = in_cksum((unsigned short *)packet, 7 + payload_size);
    memcpy(packet + 4, &checksum, 2);

    return packet_len;
}

// This function sends a filename ack
void send_filename_ack(int socketNum, struct sockaddr_in6 *client) {
    uint8_t out_packet[7]; // Packet to be constructed
    int packet_len = 0; // Packet size

    //Build packetwit
    packet_len = build_packet(out_packet, 0,FLAG_FILENAME_ACK,NULL, 0 ); 

    //Calculate addr_len for safeSendTo
    int addr_len = sizeof(struct sockaddr_in6);

    //Send to client socket
	safeSendto(socketNum, out_packet, packet_len, 0, (struct sockaddr *) client, addr_len);
}

//This function sends a filename_error
void send_filename_error(int socketNum, struct sockaddr_in6 *client){
    uint8_t out_packet[7]; // Packet to be constructed
    int packet_len = 0; // Packet size

    //Build packetwit
    packet_len = build_packet(out_packet, 0,FLAG_FILENAME_ERROR,NULL, 0 ); 

    //Calculate addr_len for safeSendTo
    int addr_len = sizeof(struct sockaddr_in6);

    //Send to client socket
	safeSendto(socketNum, out_packet, packet_len, 0, (struct sockaddr *) client, addr_len);
}

/*This function processes the filename packet from rcopy.
  It sets the filename, window-size, and buffer-size*/
FILE *processFilenameAck(int socketNum,CircularBuffer *window, struct sockaddr_in6 *client ) {
    // Initialize buffer and client address
    uint8_t buffer[MAX_PDU];
    int clientAddrLen = sizeof(struct sockaddr_in6); 

    // Receive the filename packet
    int dataLen = safeRecvfrom(socketNum, buffer, MAX_PDU, 0, (struct sockaddr *)client, &clientAddrLen);
    if (dataLen < 0) {
        perror("Failed to receive filename packet");
        return NULL;
    }

    // Verify checksum
    if (in_cksum((unsigned short *)buffer, dataLen) != 0) {
        fprintf(stderr, "Checksum verification failed\n");
        return NULL;
    }
    printf("Checksum Passed!\n");

    // Get window size and buffer size and init of buffer
    int window_size = ntohl(*(uint32_t *)(buffer + 7));
    int buffer_size = ntohl(*(uint32_t *)(buffer + 11));
    buffer_init(window, window_size,buffer_size); 

    // Get filename, ensuring proper bounds
    char filename[100] = {0};  // Ensure it's null-terminated
    int filename_len = dataLen - 15;
    if (filename_len > 99) filename_len = 99;  // Prevent buffer overflow
    strncpy(filename, (char *)(buffer + 15), filename_len);
    filename[filename_len] = '\0';  // Ensure null termination

    printf("Filename requested: %s\n", filename);
    printf("Window Size: %u, Buffer Size: %u\n", window_size, buffer_size);
    

    // Attempt to open the requested file
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file");
        send_filename_error(socketNum, client);
        return NULL;
    }

    // Send acknowledgment after successfully opening the file
    send_filename_ack(socketNum, client); 

    return file;
}

/*Returns -1 when EOF*/
int read_file_to_buffer(CircularBuffer *window, FILE *export_file){
    size_t bytesRead; //Bytes read from fread
    uint8_t tempBuff[window->buffer_size]; //Buffer for storing data from the file
    memset(tempBuff, '\0', window->buffer_size); 
    int sequence_num = window->current;

    bytesRead = fread(tempBuff,1,window->buffer_size, export_file);

    if (bytesRead == 0) {  
        //switch state to eof 
        return -1;  // End of file
    }

    printf("----------------Seq Num: %d---------------------\n", sequence_num); 
    printf("Highest: %d, Current: %d, Lowest: %d\n", window->highest, window->current, window->lowest);
    printf("\n");

    int index = sequence_num % window->size; 

    //Add the data to window data structure :) 
    buffer_add(window, sequence_num, tempBuff, bytesRead); 

    printf("Data to be sent: %s\n", window->entries[index].data); 

    return bytesRead; 
}

//Function for sending data 
void send_data(int socketNum,struct sockaddr_in6 *client,  CircularBuffer *window,  FILE *export_file, int bytesRead){ 

    //Variables for sending data
    int sequence_num = window->current;
    int index = sequence_num % window->size; 

    printf("Current index: %d\n", index);
    //Build packet to be sent.
    uint8_t out_packet[MAX_PDU]; //Packet to be built

    //Build packet with data from buffer.
    build_packet(out_packet, sequence_num, FLAG_DATA,  window->entries[index].data, bytesRead); 

    printf("Highest: %d, Current: %d, Lowest: %d\n", window->highest, window->current, window->lowest);

    //Send filename packet ot the server
	int addr_Len = sizeof(struct sockaddr_in6);
	int out_packet_len = 7 + bytesRead; 
	printf("packet length = %d\n", out_packet_len );

    //Send packet to rcopy
	safeSendto(socketNum, out_packet, out_packet_len, 0, (struct sockaddr *)client, addr_Len);

    //Increase current after sending
    window->current++;
}

//Function for resending data. 
void resend_packet(int socketNum, struct sockaddr_in6 *client, uint32_t seq_num, CircularBuffer *window) {
    int index = seq_num % window->size;

    // Ensure the packet exists before resending
    if (!window->entries[index].valid_flag || window->entries[index].sequence_num != seq_num) {
        printf("Error: Cannot resend missing packet #%d (Not in buffer)\n", seq_num);
        return;
    }

    printf("Resending packet #%d from buffer index %d\n", seq_num, index);

    // Build and send packet
    uint8_t out_packet[MAX_PDU];
    memset(out_packet, 0, MAX_PDU);

    uint32_t net_seq_num = htonl(seq_num);
    memcpy(out_packet, &net_seq_num, 4);
    out_packet[6] = FLAG_DATA;
    memcpy(out_packet + 7, window->entries[index].data, window->buffer_size);

    // Calculate checksum
    memset(out_packet + 4, 0, 2);
    uint16_t checksum = in_cksum((unsigned short *)out_packet, window->buffer_size + 7);
    memcpy(out_packet + 4, &checksum, 2);

    // Send packet
    int addr_len = sizeof(struct sockaddr_in6);
    int out_packet_len = 7 + window->buffer_size;
    safeSendto(socketNum, out_packet, out_packet_len, 0, (struct sockaddr *)client, addr_len);
}

void process_rr_srej(int socketNum, CircularBuffer *window, struct sockaddr_in6 *client) {
    uint8_t in_packet[MAX_PDU];  
	int addr_len = sizeof(client);

    // Receive the packet
    int recv_len = safeRecvfrom(socketNum, in_packet, MAX_PDU, 0, (struct sockaddr *)&client, &addr_len);
    if (recv_len < 0) {
        perror("Error receiving acknowledgment");
        return;
    }

    // Verify checksum
    uint16_t received_checksum;
    memcpy(&received_checksum, in_packet + 4, 2);
    memset(in_packet + 4, 0, 2);
    if (in_cksum((unsigned short *)in_packet, recv_len) != received_checksum) {
        printf("Checksum error in acknowledgment packet. Ignoring.\n");
        return;
    }

    //Get SREJ/RR value in le payload
    uint32_t seq_num;
    memcpy(&seq_num, in_packet + 7, 4);
    seq_num = ntohl(seq_num);

    // Extract flag
    uint8_t flag = in_packet[6];

    if (flag == FLAG_RR) {
        printf("Received RR for packet #%d. Moving window forward.\n", seq_num);
        window->lowest = seq_num; 
        window->highest = window->lowest + window->size; 
    } 
    else if (flag == FLAG_SREJ) {
        printf("Received SREJ for packet #%d. Resending...\n", seq_num);
        resend_packet(socketNum, client, seq_num, window);
    } 
    else {
        printf("Unexpected acknowledgment flag received. Ignoring.\n");
    }
}

void send_eof(int socketNum, struct sockaddr_in6 *client) {
    uint8_t eof_packet[7];  // EOF packet size
    int packet_len = 0; 

    //Build the packet with eof flag 
    packet_len = build_packet(eof_packet, 0, FLAG_EOF, NULL, 0); 

    // Send the EOF packet
    int addr_len = sizeof(struct sockaddr_in6);
    safeSendto(socketNum, eof_packet, packet_len, 0, (struct sockaddr *)client, addr_len);

    printf("Sent EOF packet to client.\n");
}

void server_FSM(int socketNum) {
    //Initialize state for the server. 
    ServerState state = FILENAME_ACK;
    FILE *export_file;

    //Initialize sendtoErr for simulating errors
    sendErr_init(ERROR_RATE, 1, 1, 1, 1);

    //Client Socket
    struct sockaddr_in6 client;

    //Initialize Circular Buffer
    CircularBuffer *window = (CircularBuffer *)malloc(sizeof(CircularBuffer));

    while (state != DONE) {
        switch (state) {
            case FILENAME_ACK:
                // Change to send_data state if file ack is successful. 
                export_file = processFilenameAck(socketNum, window, &client);
                if (export_file == NULL) {
                    state = DONE;  // Transition to DONE if file was not successfully opened                    
                } else {
                    state = SEND_DATA;
                    printf("Sending data...\n");
                }
                break;
            case SEND_DATA:
                //This continues to send data until E
                int readBytes = 1; 
                while(readBytes != 0){ 
                    while(window->current < window->highest){
                        if((readBytes = read_file_to_buffer(window,export_file)) > 0 ){
                            send_data(socketNum, &client, window, export_file, readBytes); 
                        }else if(readBytes == -1){
                            send_eof(socketNum, &client); 
                            state = DONE;
                        }
                        while(pollCall(0) != -1){
                            //Process the RRs/SREJs
                            printf("----------------babbyyyy------------------------\n");
                            process_rr_srej(socketNum,window, &client);
                        }
                    }
                    while(window->current == window->highest){
                        while(pollCall(1000) == 1){ //While poll call times out
                            //resend lowest packet 
                            //count++ 
                        }
                    }
                }
                break;
            case DONE:
                // Clean up resources or perform final actions
                if (export_file != NULL) {

                    fclose(export_file);
                }
                printf("Transfer completed.\n");
                break;
            default:
                state = DONE;  // Default state to end the FSM
        }
    }
}

int checkArgs(int argc, char *argv[])
{
	// Checks args and returns port number
	int portNumber = 0;

	if (argc < 2 || argc > 3) {  
        fprintf(stderr, "Usage: %s [error_rate] [optional port number]\n", argv[0]);
        exit(1);
    }
    
    //Check argv[1] error rate with strtof
    char *remainderPtr = NULL; 
    ERROR_RATE = strtof( argv[1], &remainderPtr);
    if(*remainderPtr != '\0' || ERROR_RATE < 0 || ERROR_RATE > 1){
        printf("Error: Invalid error rate, the acceptable range is [0,1]\n");
        exit(1); 
    }
    printf("Server Error_rate: %f\n", ERROR_RATE ); 
    
	if (argc == 3){
		portNumber = atoi(argv[2]);
	}   

    printf("\n");
	return portNumber;
}


