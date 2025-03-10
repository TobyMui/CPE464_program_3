// Client side - UDP Code
// By Hugh Smith	4/1/2017

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

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "communication.h"
#include "checksum.h"
#include "cpe464.h"
#include "pollLib.h"
#include "buffer.h"

typedef enum{
	DONE, 
	SEND_FILENAME,
	RECEIVE_DATA,
	FILE_OK
} RcopyState;

typedef enum{
	INORDER,
	BUFFER, 
	FLUSH,
	EXIT
} recvState;

void send_filename(int socketNum, struct sockaddr_in6 *server, uint32_t window_size, uint32_t buffer_size, char *filename);
void rcopy_FSM(int sockfd, struct sockaddr_in6 *server, char *argv[]);
RcopyState filename_exchange(int socketNum, struct sockaddr_in6 *server, char *argv[]);
void send_SREJ(int sockfd, struct sockaddr_in6 *server, uint32_t missing_seq);
void send_rr(int sockfd, struct sockaddr_in6 *server, uint32_t next_expected_seq);

int checkArgs(int argc, char *argv[]);
int eof_seq_num = 0; //Store seq num of EOF packet

int main(int argc, char *argv[])
{
	int socketNum = 0;
	struct sockaddr_in6 server; // Supports 4 and 6 but requires IPv6 struct
	int portNumber = 0;

	// Check arguments
	portNumber = checkArgs(argc, argv);

	// 0.rcopy , 1.from-file, 2 to-file, 3 window-size, 4 buffer-size, 5 error-rate, 6 remote machine, 7 remote port.
	socketNum = setupUdpClientToServer(&server, argv[6], portNumber);

	rcopy_FSM(socketNum, &server, argv);

	close(socketNum);

	return 0;
}

/*In this function we are going send the init packet to the server
The 7 bytes header follows 32 bits seq# , 16 bits checksum,8 bits flag, and then filename.
Max filename is 100 characters. */
void send_filename(int socketNum, struct sockaddr_in6 *server, uint32_t window_size, uint32_t buffer_size, char *filename)
{
	// printf("Window size: %d\n", window_size);
	// printf("Buffer size: %d\n", buffer_size);
	// printf("Filename: %s\n", filename);

	uint8_t out_packet[MAX_PDU];			   // Packet to be constructed
	memset(out_packet, 0, sizeof(out_packet)); // Zero out the packet.

	// Add the sequence number (bytes 0-3)
	uint32_t sequence_num = htonl(0);
	memcpy(out_packet, &sequence_num, 4);

	// Add flag at index 6
	out_packet[6] = FLAG_FILENAME;

	// Add Window Size (bytes 7-10)
	uint32_t network_window_size = htonl(window_size);
	memcpy(out_packet + 7, &network_window_size, 4);

	// Add Buffer Size (bytes 11-14)
	uint32_t network_buffer_size = htonl(buffer_size);
	memcpy(out_packet + 11, &network_buffer_size, 4);

	// Add Filename (starting at byte 15)
	strncpy((char *)(out_packet + 15), filename, strlen(filename));

	// Set checksum field (bytes 4-5) to 0 before computing checksum
	memset(out_packet + 4, 0, 2);

	uint16_t checksum = in_cksum((unsigned short *)out_packet, 15 + strlen(filename));

	// Store checksum in bytes 4-5
	memcpy(out_packet + 4, &checksum, 2);

	// Send filename packet ot the server
	int serverAddrLen = sizeof(struct sockaddr_in6);
	int out_packet_len = 15 + strlen(filename);
	printf("packet length = %d\n", out_packet_len);

	safeSendto(socketNum, out_packet, out_packet_len, 0, (struct sockaddr *)server, serverAddrLen);
}

/*Sends eof flag*/
void send_eof(int socketNum, struct sockaddr_in6 *client){
    uint8_t eof_packet[7]; // EOF packet size
    int packet_len = 0;

    // Build the packet with eof flag
    packet_len = build_packet(eof_packet, 0, FLAG_EOF, NULL, 0);

    // Send the EOF packet
    int addr_len = sizeof(struct sockaddr_in6);
    safeSendto(socketNum, eof_packet, packet_len, 0, (struct sockaddr *)client, addr_len);

    printf("Sent EOF packet to client.\n");
}

// Check the response from the server after sending a filename return 1, if no error.
int process_filename_response(uint8_t *in_buffer, int in_buff_len)
{
	// Verify checksum
	if (in_cksum((unsigned short *)in_buffer, in_buff_len) != 0){
		fprintf(stderr, "Checksum verification failed, packet dropped\n");
		return 0;
	}

	// Check the flag
	int flag = 0;
	memcpy(&flag, &in_buffer[6], 1);
	printf("Flag: %d\n", flag);

	if (flag == FLAG_FILENAME_ERROR){
		printf("Filename doesn't exist, please retry\n");
		exit(1);
	}else if (flag == FLAG_FILENAME_ACK){
		printf("Filename exist, the server will be sending data\n");
		return 1;
	}else if(flag == FLAG_DATA){
		printf("Filename Ack lost, but received data");
		return 1; 
	}else{
		return 0;
	}

	return 0;
}

/*This function attempts to send the filename to the server
*/
RcopyState filename_exchange(int socketNum, struct sockaddr_in6 *server, char *argv[]){
	int attempts = 1;
	uint8_t buffer[MAX_PDU];
	socklen_t serverLen = sizeof(struct sockaddr_in6);

	// Setup the poll set and add our socket to it
	setupPollSet();
	addToPollSet(socketNum);

	while (attempts <= 10){
		send_filename(socketNum, server, atoi(argv[3]), atoi(argv[4]), argv[1]);
		printf("Attempt %d: Sent filename packet\n", attempts);

		// Wait for up to 1 second for a response
		int readySocket = pollCall(1000); // 1000ms timeout

		if (readySocket == socketNum){ // Data available
			int recvLen = safeRecvfrom(socketNum, buffer, MAX_PDU, 0, (struct sockaddr *)server, (int *)&serverLen);
			printf("Incoming bytes: %d, Received response from server: %s\n", recvLen, buffer);

			// Process the respnse from server, check the flag.
			if (1 == process_filename_response(buffer, recvLen)){
				return RECEIVE_DATA; // Successful response, exit function
			}
		}else if (readySocket == -1){ // Timeout
			printf("Timeout: No response received. Attempt: %d\n", attempts);
		}else{
			perror("Error with poll call\n");
			exit(1);
		}
		attempts++;
		printf("\n");
	}

	printf("Failed to receive response after 10 attempts.\n");
	return DONE;
	exit(1);
}
  
////////////////////////////////Functions for Sending Packets///////////////////////////////

/*This function sends an SREJ to the server*/
void send_SREJ(int sockfd, struct sockaddr_in6 *server, uint32_t missing_seq){
	uint8_t srej_packet[11];						// packet to be built
	memset(srej_packet, '\0', sizeof(srej_packet)); // set buffer to null
	socklen_t addr_len = sizeof(struct sockaddr_in6);

	uint32_t net_seq_num = htonl(0);
	uint32_t net_nack_seq = htonl(missing_seq);

	// Add data
	memcpy(srej_packet, &net_seq_num, 4);
	srej_packet[6] = FLAG_SREJ;
	memcpy(srej_packet + 7, &net_nack_seq, 4);

	// Compute checksum and insert into (5-6)
	memset(srej_packet + 4, 0, 2);
	uint16_t checksum = in_cksum((unsigned short *)srej_packet, 11);
	memcpy(srej_packet + 4, &checksum, 2);

	// Send SREJ packet
	safeSendto(sockfd, srej_packet, 11, 0, (struct sockaddr *)server, addr_len);
}

/*This function sends an RR to the server. */
void send_rr(int sockfd, struct sockaddr_in6 *server, uint32_t next_expected_seq){
	uint8_t rr_packet[11];					 // packet to be built
	memset(rr_packet, 0, sizeof(rr_packet)); // set buffer to null
	socklen_t addr_len = sizeof(struct sockaddr_in6);

	// sequence number
	uint32_t net_seq_num = htonl(0);
	uint32_t net_ack_seq = htonl(next_expected_seq);

	// Set sequence number, flag, and RR sequence number
	memcpy(rr_packet, &net_seq_num, 4);
	rr_packet[6] = FLAG_RR;
	memcpy(rr_packet + 7, &net_ack_seq, 4);

	// Compute checksum and insert into (5-6)
	memset(rr_packet + 4, 0, 2);
	uint16_t checksum = in_cksum((unsigned short *)rr_packet, 11);
	memcpy(rr_packet + 4, &checksum, 2);

	// Send RR packet
	safeSendto(sockfd, rr_packet, 11, 0, (struct sockaddr *)server, addr_len);
}

recvState handle_flush(int sockNum, struct sockaddr_in6 *server, CircularBuffer *buffer, FILE *outFile) {
    while(1) {
        // Calculate current index using modulo for circular buffer
		printf("buffering\n"); 
        int current_index = buffer->current % buffer->size;
        
        // Exit loop if current entry is invalid
        if (buffer->entries[current_index].valid_flag != 1) break;

        // Write the actual buffered data
        fwrite(buffer->entries[current_index].data, 1, buffer->entries[current_index].data_len,  outFile);
        
        buffer->entries[current_index].valid_flag = 0;
        
        // Move to next sequence number
        buffer->current++;

        // Send RR for the NEXT expected sequence number
        send_rr(sockNum, server, buffer->current);

		printf("flushing!!!!!\n");
    }

    // Check if we need to request missing packets
    if (buffer->current < buffer->highest && buffer->entries[buffer->current % buffer->size].valid_flag == 0) {
        send_SREJ(sockNum, server, buffer->current);
		printf("Sending RR and SREJ in flush:%d \n", buffer->current); 
		send_rr(sockNum, server, buffer->current);

        return BUFFER;
    }

    // Return to normal processing if we're caught up
    return INORDER;
}

recvState handle_buffer(int sockNum, struct sockaddr_in6 *server, CircularBuffer *buffer, FILE *outFile){
	//Init for FSM
	int recvLen = 0; 
	int socketReady; 
	uint8_t in_packet[MAX_PDU]; //Packet to be received
	memset(in_packet, 0, sizeof(in_packet)); //Zero out the packet
	socklen_t addr_len = sizeof(server);

	socketReady = pollCall(10000);
	if(socketReady == sockNum){
		recvLen = safeRecvfrom(sockNum, in_packet, MAX_PDU, 0, (struct sockaddr *)server, (int *)&addr_len); //Recv data
			
		//Check the checksum
		if (in_cksum((unsigned short *)in_packet, recvLen) != 0){
				printf("Checksum error, packet will be dropped\n");
				return BUFFER; 
		}

		//Get sequence number
		// Get the sequence number
		uint32_t seq_num;
		memcpy(&seq_num, in_packet, 4);
		seq_num = ntohl(seq_num);

		//Check for EOF flag 
		if(in_packet[6] == FLAG_EOF){
			eof_seq_num = seq_num; 
		}

		if(in_packet[6] == FLAG_EOF && buffer->current > eof_seq_num){
			printf("Exiting tranfer complete"); 
			send_eof(sockNum,server);
			return EXIT; 
		}

		//Algorithm for determining the next state
		if(seq_num == buffer->current){ //Move to flush state; 
			fwrite(in_packet + HEADER_SIZE , recvLen - HEADER_SIZE, 1, outFile); // Write to file go to inorder 
			fflush(outFile);
			buffer->current++;
			return FLUSH; 
		}else if(seq_num > buffer->current){ // return out of order and buffer
			buffer_add(buffer, seq_num, in_packet + HEADER_SIZE, recvLen - HEADER_SIZE ); 
			buffer->highest = seq_num;
			return BUFFER;
		}
	}else if(socketReady == -1){ 
		exit(-1); 
	}
	return BUFFER; 
}

recvState handle_inorder(int sockNum, struct sockaddr_in6 *server, CircularBuffer *buffer, FILE *outFile){
	//Init for FSM
	int recvLen = 0; 
	int socketReady; 
	uint8_t in_packet[MAX_PDU]; //Packet to be received
	memset(in_packet, 0, sizeof(in_packet)); //Zero out the packet
	socklen_t addr_len = sizeof(server);

	socketReady = pollCall(10000);
	if(socketReady == sockNum){
		recvLen = safeRecvfrom(sockNum, in_packet, MAX_PDU, 0, (struct sockaddr *)server, (int *)&addr_len); //Recv data
			
		//Check the checksum
		if (in_cksum((unsigned short *)in_packet, recvLen) != 0){
				printf("Checksum error, packet will be dropped\n");
				return INORDER; 
		}
		//Get sequence number
		// Get the sequence number
		uint32_t seq_num;
		memcpy(&seq_num, in_packet, 4);
		seq_num = ntohl(seq_num);

		//Check for EOF flag 
		if(in_packet[6] == FLAG_EOF){
			eof_seq_num = seq_num; 
		}

		if(in_packet[6] == FLAG_EOF && buffer->current > eof_seq_num){
			printf("Exiting tranfer complete"); 
			send_eof(sockNum,server);
			return EXIT; 
		}

		//Algorithm for determining the next state
		printf("~~~~~~~~~~~Highest: %d, Current: %d, Lowest: %d~~~~~~~~~~~~~~~~~\n", buffer->highest, buffer->current, buffer->lowest);

		if( seq_num == buffer->current){
			fwrite(in_packet + HEADER_SIZE , recvLen - HEADER_SIZE, 1, outFile); // Write to file go to inorder
			fflush(outFile);
			buffer->highest = buffer->current; 
			buffer->current++;
			send_rr(sockNum,server, buffer->current); 
			printf("Sending RR and SREJ in buffer:%d \n", buffer->current); 
			return INORDER; 
		}else if(seq_num > buffer->current){ // return out of order and buffer
			send_SREJ(sockNum, server, buffer->current); 
			printf("Added to buffer======%d", seq_num);
			buffer_add(buffer, seq_num, in_packet + HEADER_SIZE, recvLen - HEADER_SIZE ); 
			buffer->highest = seq_num;
			return BUFFER;
		}
		send_rr(sockNum, server, buffer->current);
	}else if(socketReady == -1){ 
		exit(-1); 
	}
	return INORDER; 
}

RcopyState receive_data_fsm(int sockNum, struct sockaddr_in6 *server, CircularBuffer *buffer, FILE *outFile, recvState current, recvState next){
	printf("~~~~~Expected: %d  ~~~~~~~ \n", buffer->current); 	
	switch(current){
			case INORDER: 
				next = handle_inorder(sockNum, server, buffer, outFile);
				if (next == EXIT) {
					printf("Exiting from INORDER\n");
					return DONE; 
				}
				break;
			case BUFFER:
				printf("BUFFERING:\n");
				next = handle_buffer(sockNum, server, buffer, outFile);
				if (next == EXIT) {
					printf("Exiting from BUFFER\n");
					return DONE; 

				}
				break; 
			case FLUSH:
				next = handle_flush(sockNum, server, buffer, outFile);
				if (next == EXIT) {
					printf("Exiting from FLUSH\n");
					return DONE; 
				}
				break; 
			case EXIT: 
				break;
			default:
				next = EXIT; 
	}
	return RECEIVE_DATA;
}

/////////////////////////////////////////FSM///////////////////////////////////////////

void rcopy_FSM(int sockfd, struct sockaddr_in6 *server, char *argv[]){
	RcopyState state = SEND_FILENAME;

	// Initiate buffer
	CircularBuffer *buffer = (CircularBuffer *)malloc(sizeof(CircularBuffer));
	buffer_init(buffer, atoi(argv[3]), atoi(argv[4]), 0);

	//Init for recvFSM
	recvState currentRecvState = INORDER; 
	recvState nextRecvState =  INORDER; 

	// Open a file for writing
	FILE *outFile = fopen(argv[2], "wb");
	if (outFile == NULL){
		perror("Error openning file");
		exit(1);
	}

	// Initialize the trouble maker
	float error_rate = atof(argv[5]);
	printf("Error_rate: %f\n", error_rate);
	sendtoErr_init(error_rate, 1, 1, 1, 1);

	while (state != DONE){
		switch (state){
		case SEND_FILENAME:
			state = filename_exchange(sockfd, server, argv);
			if(state == DONE){
				buffer_free(buffer); 
				break;
			}
			printf("File Ok state reached\n");
			break;
		case RECEIVE_DATA:
			currentRecvState = nextRecvState; 
			state = receive_data_fsm(sockfd,server, buffer, outFile, currentRecvState, nextRecvState);
			if(state == DONE){
				fflush(outFile);
				fclose(outFile);
				buffer_free(buffer); 
			}
			break;
		default:
			state = DONE;
			fflush(outFile);
			fclose(outFile);
			buffer_free(buffer); 
			break;
		}
	}
}

int checkArgs(int argc, char *argv[])
{
	int portNumber = 0;

	/* check command line arguments  */
	if (argc != 8){
		printf("usage: %s from-filename to-filename window-size buffer-size error-rate remote-machine remote-number \n", argv[0]);
		exit(1);
	}

	// Check that there are filenames
	if (strlen(argv[1]) == 0 || strlen(argv[2]) == 0){
		printf("Error: from-filename and/or to-filename are/is empty.\n");
		exit(1);
	}

	// Check filename lengths
	if (strlen(argv[1]) >= 100 || strlen(argv[2]) >= 100){
		printf("Error: from-filename and/or to-filename exceeds 99 characters.\n");
		exit(1);
	}

	// Check argv[3] for window size, shouldn't be less than 0
	// Window size max i 2^30
	char *remainderPtr = NULL;
	int window_size = strtol(argv[3], &remainderPtr, 10);
	if (*remainderPtr != '\0' || window_size <= 0 || window_size >= (1 << 30)){
		printf("Error: Invalid Window Size\n");
		exit(1);
	}

	// Check argv[4], buffer-size, is a valid input and number.
	// Not sure about the max buffer size
	int buffer_size = strtol(argv[4], &remainderPtr, 10);
	if (*remainderPtr != '\0' || buffer_size <= 0 || buffer_size > 1400){
		printf("Error: Invalid Buffer Size\n");
		exit(1);
	}

	// Check argv[5] error rate with strtof
	float error_rate = strtof(argv[5], &remainderPtr);
	if (*remainderPtr != '\0' || error_rate < 0 || error_rate > 1){
		printf("Error: Invalid error rate, the acceptable range is [0,1]\n");
		exit(1);
	}

	// Check argv[6], remote server name
	if (strlen(argv[6]) == 0){
		printf("Error: Remote-machine is empty\n");
	}

	// Validate remote port number (argv[7]) using strtol
	portNumber = strtol(argv[7], &remainderPtr, 10);
	if (*remainderPtr != '\0' || portNumber <= 0 || portNumber > 65535){
		printf("Error: remote-port must be a valid port number.\n");
		exit(1);
	}

	return portNumber;
}
