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

#define WINDOW_SIZE

#define MAXBUF 80

void talkToServer(int socketNum, struct sockaddr_in6 *server);
void send_filename(int socketNum, struct sockaddr_in6 *server, uint32_t window_size, uint32_t buffer_size, char *filename);
void rcopy_FSM(int sockfd, struct sockaddr_in6 *server, char *argv[]);
int filename_exchange(int socketNum, struct sockaddr_in6 *server, char *argv[]);
void send_SREJ(int sockfd, struct sockaddr_in6 *server, uint32_t missing_seq);
void send_rr(int sockfd, struct sockaddr_in6 *server, uint32_t next_expected_seq);

int readFromStdin(char *buffer);
int checkArgs(int argc, char *argv[]);

typedef enum
{
	DONE,
	SEND_FILENAME,
	RECEIVE_DATA,
	FILE_OK
} RcopyState;

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

	// talkToServer(socketNum, &server);

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
	}
	else if (flag == FLAG_FILENAME_ACK){
		printf("Filename exist, the server will be sending data\n");
		return 1;
	}else{
		return 0;
	}

	return 0;
}

int filename_exchange(int socketNum, struct sockaddr_in6 *server, char *argv[])
{
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

		if (readySocket == socketNum)
		{ // Data available
			int recvLen = safeRecvfrom(socketNum, buffer, MAX_PDU, 0, (struct sockaddr *)server, (int *)&serverLen);
			printf("Incoming bytes: %d, Received response from server: %s\n", recvLen, buffer);

			// Process the respnse from server, check the flag.
			if (1 == process_filename_response(buffer, recvLen)){
				return 1; // Successful response, exit function
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
	return 0;
	exit(1);
}

/*This function parses the incoming function and will send an SREJ or RR accordingly
  returns the flag in the packet
  returns -1 if checksum failed.
  returns 0 if EOF*/
int process_packet_in(int socketNum, struct sockaddr_in6 *server, CircularBuffer *buffer, FILE *outputFile, uint8_t *in_packet, int in_packet_len)
{
	// Verify checksum
	if (in_cksum((unsigned short *)in_packet, in_packet_len) != 0){
		printf("Checksum error, packet will be dropped\n");
		return -1;
	}

	// Get the sequence number
	uint32_t seq_num;
	memcpy(&seq_num, in_packet, 4);
	seq_num = ntohl(seq_num);

	printf("Incoming bytes: %d \n", in_packet_len);
	printf("Incoming Packet Sequence num: %d\n", seq_num);
	uint8_t message[in_packet_len - 6];
	memset(message, '\0', in_packet_len - 6);
	memcpy(message, in_packet + 7, in_packet_len - 7);
	printf("Data in: %s\n", message);
	printf("\n");

	// Grab data chunk
	uint8_t data[in_packet_len - HEADER_SIZE];
	memcpy(data, in_packet + 7, sizeof(data));

	// Check for EOF
	if (in_packet[6] == FLAG_EOF && (buffer->current > buffer->highest)){
		printf("EOF DETECTED~~~~~~~~~ Last sequence: %d\n", buffer->current);
		send_eof(socketNum, server); 
		return 0;
	}

	printf("Current: %d ---------------\n", buffer->current);

	// Logic for writing and buffering (FSM sort of)
	if (seq_num == buffer->current){ // Write to disk (inorder)
		fwrite(data, in_packet_len - HEADER_SIZE, 1, outputFile); // Write to file
		printf("Sending RR%d\n", buffer->current + 1);
		send_rr(socketNum, server, buffer->current + 1);
		buffer->current++;

		// Check the buffer
		int index = buffer->current % buffer->size;
		while (buffer->entries[index].valid_flag == 1 && buffer->current == buffer->entries[index].sequence_num){
			fwrite(buffer->entries[index].data, buffer->entries[index].data_len, 1, outputFile); // Corrected line
			printf("~~FLUSH buffer~~ %d\n", buffer->entries[index].sequence_num);
			buffer->entries[index].valid_flag = 0;
			buffer->current++;
			index = buffer->current % buffer->size;
		}
	}else{
		if (seq_num > buffer->current) {  // Only buffer packets ahead of current
			printf("------- SREJ for Packet: %d------- \n", buffer->current);
			buffer_add(buffer, seq_num, data, in_packet_len - HEADER_SIZE);
			int index = seq_num % buffer->size;
			buffer->entries[index].data_len = in_packet_len - HEADER_SIZE;
			send_SREJ(socketNum, server, buffer->current);  // Request missing current seq
		}
	}

	return in_packet[6];
}

/*Returns bytes received
  Returns -1, when EOF detected*/
int receive_data(int socketNum, struct sockaddr_in6 *server, CircularBuffer *buffer, FILE *outputFile)
{
	uint8_t in_packet[MAX_PDU];
	memset(in_packet, '\0', sizeof(in_packet));
	socklen_t addr_len = sizeof(server);
	fflush(outputFile);

	int attempts = 1;
	while (attempts <= 10){
		int readySocket = pollCall(1000); // 1000ms timeout
		if (readySocket > 0){
			printf("--------------------------------------------------------------------\n\n");

			int recvLen = safeRecvfrom(socketNum, in_packet, MAX_PDU, 0, (struct sockaddr *)server, (int *)&addr_len);
			// Grab the sequence number and check that it is the expected.
			// Process packet
			if (process_packet_in(socketNum, server, buffer, outputFile, in_packet, recvLen) == 0){ // returns 0 if EOF
				return -1;
			}

			// Process the packet spending on the flag.
			return recvLen;
			break;
		}else if (readySocket == -1){
			printf("Timeout: No response received. Attempt: %d\n", attempts);
		}else{
			perror("Error with poll call\n");
			exit(1);
		}
		send_SREJ(socketNum, server, buffer->current);
		attempts++;
		printf("\n");
	}
	return 0;
}

////////////////////////////////Functions for Sending Packets///////////////////////////////

/*This function sends an SREJ to the server*/
void send_SREJ(int sockfd, struct sockaddr_in6 *server, uint32_t missing_seq)
{
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
void send_rr(int sockfd, struct sockaddr_in6 *server, uint32_t next_expected_seq)
{
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

/////////////////////////////////////////FSM///////////////////////////////////////////

void rcopy_FSM(int sockfd, struct sockaddr_in6 *server, char *argv[])
{
	RcopyState state = SEND_FILENAME;

	// Initiate buffer
	CircularBuffer *buffer = (CircularBuffer *)malloc(sizeof(CircularBuffer));
	buffer_init(buffer, atoi(argv[3]), atoi(argv[4]));

	// Open a file for writing
	FILE *output_file = fopen(argv[2], "wb");
	if (output_file == NULL){
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
			if (1 == filename_exchange(sockfd, server, argv)){
				state = RECEIVE_DATA;
				printf("File Ok state reached\n");
			}
			break;
		case FILE_OK:
			break;
		case RECEIVE_DATA:
			int readBytes = 1; // Start at 1 to start the while loop
			// Recieve until there is no more EOF, however we need to check that the data is coming in order
			while (readBytes > 0){
				if ((readBytes = receive_data(sockfd, server, buffer, output_file)) == -1){
					state = DONE;
					break;
				}
			}
			break;
		default:
			state = DONE;
			printf("HELLLLLLLOOOOOOOOOOOOOOOOOOOO\n");
			fflush(output_file);
			fclose(output_file);
			break;
		}
	}
}

void talkToServer(int socketNum, struct sockaddr_in6 *server)
{
	int serverAddrLen = sizeof(struct sockaddr_in6);
	char *ipString = NULL;
	int dataLen = 0;
	char buffer[MAXBUF + 1];

	buffer[0] = '\0';
	while (buffer[0] != '.')
	{
		dataLen = readFromStdin(buffer);

		printf("Sending: %s with len: %d\n", buffer, dataLen);

		safeSendto(socketNum, buffer, dataLen, 0, (struct sockaddr *)server, serverAddrLen);

		safeRecvfrom(socketNum, buffer, MAXBUF, 0, (struct sockaddr *)server, &serverAddrLen);

		// print out bytes received
		ipString = ipAddressToString(server);
		printf("Server with ip: %s and port %d said it received %s\n", ipString, ntohs(server->sin6_port), buffer);
	}
}

int readFromStdin(char *buffer)
{
	char aChar = 0;
	int inputLen = 0;

	// Important you don't input more characters than you have space
	buffer[0] = '\0';
	printf("Enter data: ");
	while (inputLen < (MAXBUF - 1) && aChar != '\n'){
		aChar = getchar();
		if (aChar != '\n')
		{
			buffer[inputLen] = aChar;
			inputLen++;
		}
	}

	// Null terminate the string
	buffer[inputLen] = '\0';
	inputLen++;

	return inputLen;
}

int checkArgs(int argc, char *argv[])
{
	int portNumber = 0;

	/* check command line arguments  */
	if (argc != 8)
	{
		printf("usage: %s from-filename to-filename window-size buffer-size error-rate remote-machine remote-number \n", argv[0]);
		exit(1);
	}

	// Check that there are filenames
	if (strlen(argv[1]) == 0 || strlen(argv[2]) == 0)
	{
		printf("Error: from-filename and/or to-filename are/is empty.\n");
		exit(1);
	}

	// Check filename lengths
	if (strlen(argv[1]) >= 100 || strlen(argv[2]) >= 100)
	{
		printf("Error: from-filename and/or to-filename exceeds 99 characters.\n");
		exit(1);
	}

	// Check argv[3] for window size, shouldn't be less than 0
	// Window size max i 2^30
	char *remainderPtr = NULL;
	int window_size = strtol(argv[3], &remainderPtr, 10);
	if (*remainderPtr != '\0' || window_size <= 0 || window_size >= (1 << 30))
	{
		printf("Error: Invalid Window Size\n");
		exit(1);
	}

	// Check argv[4], buffer-size, is a valid input and number.
	// Not sure about the max buffer size
	int buffer_size = strtol(argv[4], &remainderPtr, 10);
	if (*remainderPtr != '\0' || buffer_size <= 0 || buffer_size >= 1400)
	{
		printf("Error: Invalid Buffer Size\n");
		exit(1);
	}

	// Check argv[5] error rate with strtof
	float error_rate = strtof(argv[5], &remainderPtr);
	if (*remainderPtr != '\0' || error_rate < 0 || error_rate > 1)
	{
		printf("Error: Invalid error rate, the acceptable range is [0,1]\n");
		exit(1);
	}

	// Check argv[6], remote server name
	if (strlen(argv[6]) == 0)
	{
		printf("Error: Remote-machine is empty\n");
	}

	// Validate remote port number (argv[7]) using strtol
	portNumber = strtol(argv[7], &remainderPtr, 10);
	if (*remainderPtr != '\0' || portNumber <= 0 || portNumber > 65535)
	{
		printf("Error: remote-port must be a valid port number.\n");
		exit(1);
	}

	return portNumber;
}
