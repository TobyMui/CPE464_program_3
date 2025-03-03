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

	socketNum = udpServerSetup(portNumber);

	server_FSM(socketNum);

	close(socketNum);

	return 0;
}

// This function sends a filename ack
void send_filename_ack(int socketNum, struct sockaddr_in6 *server) {
    uint8_t out_packet[7]; // Packet to be constructed
    memset(out_packet, 0, sizeof(out_packet)); // Zero out the packet.

    //Add sequence number (bytes 0-3)
    uint32_t seq_num = htonl(0);
    memcpy(out_packet, &seq_num,4); 

    //Add flag at index 6 
    out_packet[6] = FLAG_FILENAME_ACK;

    //Calculate checksum 
    memset(out_packet + 4, 0, 2);
    uint16_t checksum = in_cksum((unsigned short *) out_packet, 7 );

    //Add checksum (bytes 4-5)
    memcpy(out_packet + 4, &checksum, 2);

    // Send filename packet ot the server
    int serverAddrLen = sizeof(struct sockaddr_in6);
	int out_packet_len = 7; 
	printf("packet length = %d\n", out_packet_len );

	safeSendto(socketNum, out_packet, out_packet_len, 0, (struct sockaddr *) server, serverAddrLen);
}

//This function sends a filename_error
void send_filename_error(int socketNum, struct sockaddr_in6 *server){
    uint8_t out_packet[7]; // Packet to be constructed
    memset(out_packet, 0, sizeof(out_packet)); // Zero out the packet.

    //Add sequence number (bytes 0-3)
    uint32_t seq_num = htonl(0);
    memcpy(out_packet, &seq_num,4); 

    //Add flag at index 6 
    out_packet[6] = FLAG_FILENAME_ERROR;

    //Calculate checksum 
    memset(out_packet + 4, 0, 2);
    uint16_t checksum = in_cksum((unsigned short *) out_packet, 7 );

    //Add checksum (bytes 4-5)
    memcpy(out_packet + 4, &checksum, 2);
    
    int check = in_cksum((unsigned short *) out_packet, 7); 
    printf("Checksum checker: %d\n", check); 

     // Send filename packet ot the server
	int serverAddrLen = sizeof(struct sockaddr_in6);
	int out_packet_len = 7; 
	printf("packet length = %d\n", out_packet_len );

	safeSendto(socketNum, out_packet, out_packet_len, 0, (struct sockaddr *)server, serverAddrLen);
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

/* IN PROGRESS WORKING ON SLIDING WINDOW FOR THIS SHIT*/
void send_data(int socketNum, CircularBuffer *buffer,  FILE *export_file){ 
    size_t bytesRead;  
    uint8_t tempBuff[buffer->buffer_size];
    int sequence_num = buffer->current;

    bytesRead = fread(tempBuff,1,buffer->buffer_size, export_file);

    if (bytesRead == 0) {  
        return;  // End of file
    }

    buffer_add(buffer,sequence_num,tempBuff,bytesRead);

    printf("text: %s", tempBuff);

    //Lets try sending buffer size of 200 words. 


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
                send_data(socketNum,window,export_file); 
                state = FILENAME_ACK;
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


