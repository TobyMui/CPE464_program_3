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

#define WINDOW_SIZE


#define MAXBUF 80

void talkToServer(int socketNum, struct sockaddr_in6 * server);
void send_filename(int socketNum, struct sockaddr_in6 *server, uint32_t window_size, uint32_t buffer_size, char *filename); 
void rcopy_FSM(int sockfd, struct sockaddr_in6 *server, char *argv[]);
void filename_exchange(int socketNum, struct sockaddr_in6 *server, char *argv[]);



int readFromStdin(char * buffer);
int checkArgs(int argc, char * argv[]);

typedef enum{
	DONE, SEND_FILENAME, RECEIVE_DATA, FILE_OK 
}RcopyState; 


int main (int argc, char *argv[])
 {
	int socketNum = 0;				
	struct sockaddr_in6 server;		// Supports 4 and 6 but requires IPv6 struct
	int portNumber = 0;
	
	//Check arguments 
	portNumber = checkArgs(argc, argv);

	//0.rcopy , 1.from-file, 2 to-file, 3 window-size, 4 buffer-size, 5 error-rate, 6 remote machine, 7 remote port. 
	socketNum = setupUdpClientToServer(&server, argv[6], portNumber);

	rcopy_FSM(socketNum, &server, argv);

	// talkToServer(socketNum, &server);
	
	close(socketNum);

	return 0;
}

//In this function we are going send the init packet to the server
//The 7 bytes header follows 32 bits seq# , 16 bits checksum,8 bits flag, and then filename. 
//Max filename is 100 characters. 
void send_filename(int socketNum, struct sockaddr_in6 *server, uint32_t window_size, uint32_t buffer_size, char *filename) {
    printf("Window size: %d\n", window_size);
    printf("Buffer size: %d\n", buffer_size);
    printf("Filename: %s\n", filename);

    uint8_t out_packet[MAX_PDU]; // Packet to be constructed
    memset(out_packet, 0, sizeof(out_packet)); // Zero out the packet.

    // Add the sequence number (bytes 0-3)
    uint32_t sequence_num = htonl(0);
    memcpy(out_packet, &sequence_num, 4);

    // Add flag at index 6
    out_packet[6] = FLAG_FILENAME;

    // Add Window Size (bytes 7-10)
    uint32_t net_window_size = htonl(window_size);
    memcpy(out_packet + 7, &net_window_size, 4);

    // Add Buffer Size (bytes 11-14)
    uint32_t net_buffer_size = htonl(buffer_size);
    memcpy(out_packet + 11, &net_buffer_size, 4);

    // Add Filename (starting at byte 15)
    strncpy((char *)(out_packet + 15), filename, strlen(filename));

    // Set checksum field (bytes 4-5) to 0 before computing checksum
    memset(out_packet + 4, 0, 2);

    // Calculate checksum
    uint16_t checksum = in_cksum((unsigned short *)out_packet, 15 + strlen(filename));

    // Store checksum in bytes 4-5
    memcpy(out_packet + 4, &checksum, 2);

    // Send filename packet ot the server
	int serverAddrLen = sizeof(struct sockaddr_in6);
	int out_packet_len = 15 + strlen(filename); 
	safeSendto(socketNum, out_packet, out_packet_len, 0, (struct sockaddr *) server, serverAddrLen);
}

void filename_exchange(int socketNum, struct sockaddr_in6 *server, char *argv[]) {
    int attempts = 1;
    char buffer[MAXBUF];
    socklen_t serverLen = sizeof(struct sockaddr_in6);
    
    // Setup the poll set and add our socket to it
    setupPollSet();
    addToPollSet(socketNum);

    while (attempts < 10) {
        send_filename(socketNum, server, atoi(argv[3]), atoi(argv[4]), argv[1]);
        printf("Attempt %d: Sent filename packet\n", attempts + 1);

        // Wait for up to 1 second for a response
        int readySocket = pollCall(1000);  // 1000ms timeout

        if (readySocket == socketNum) {  // Data available
            int recvLen = safeRecvfrom(socketNum, buffer, MAXBUF, 0, 
                                       (struct sockaddr *)server, (int *)&serverLen);
            printf("Received response from server: %s\n", buffer);
            return;  // Successful response, exit function
        } else if (readySocket == -1) {  // Timeout
            printf("Timeout: No response received. Attempt: %d\n" , attempts);
        } else {
            perror("pollCall");
            exit(EXIT_FAILURE);
        }
        attempts++;
    }

    printf("Failed to receive response after 10 attempts.\n");
    exit(EXIT_FAILURE);
}



/////////////////////////////////////////FSM///////////////////////////////////////////

void rcopy_FSM(int sockfd, struct sockaddr_in6 *server, char *argv[]){
	RcopyState state = SEND_FILENAME;
	
	//Initialize the trouble maker
	float error_rate = atof(argv[5]);
	printf("Error_rate: %f\n", error_rate );
	sendtoErr_init(error_rate, 1, 1, 0, 1);

	while(state!=DONE){
		switch(state){
			case SEND_FILENAME: 
				filename_exchange(sockfd, server, argv);
				printf("Sending Init Packet to \n");
				state = DONE; 
				break; 
			case FILE_OK: 
				break; 
			case RECEIVE_DATA: 
				break;
			default: 
				state = DONE; 
		}

	}
}


void talkToServer(int socketNum, struct sockaddr_in6 * server)
{
	int serverAddrLen = sizeof(struct sockaddr_in6);
	char * ipString = NULL;
	int dataLen = 0; 
	char buffer[MAXBUF+1];
	
	buffer[0] = '\0';
	while (buffer[0] != '.')
	{
		dataLen = readFromStdin(buffer);

		printf("Sending: %s with len: %d\n", buffer,dataLen);
	
		safeSendto(socketNum, buffer, dataLen, 0, (struct sockaddr *) server, serverAddrLen);
		
		safeRecvfrom(socketNum, buffer, MAXBUF, 0, (struct sockaddr *) server, &serverAddrLen);
		
		// print out bytes received
		ipString = ipAddressToString(server);
		printf("Server with ip: %s and port %d said it received %s\n", ipString, ntohs(server->sin6_port), buffer);
	      
	}
}

int readFromStdin(char * buffer)
{
	char aChar = 0;
	int inputLen = 0;        
	
	// Important you don't input more characters than you have space 
	buffer[0] = '\0';
	printf("Enter data: ");
	while (inputLen < (MAXBUF - 1) && aChar != '\n')
	{
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

int checkArgs(int argc, char * argv[])
{
        int portNumber = 0;
	
        /* check command line arguments  */
	if (argc != 8)
	{
		printf("usage: %s from-filename to-filename window-size buffer-size error-rate remote-machine remote-number \n", argv[0]);
		exit(1);
	}

	//Check that there are filenames
	if(strlen(argv[1]) == 0 || strlen(argv[2]) == 0){
		printf("Error: from-filename and/or to-filename are/is empty.\n");
		exit(1); 
	}
	
	//Check filename lengths
	if (strlen(argv[1]) >= 100 || strlen(argv[2]) >= 100) {
		printf("Error: from-filename and/or to-filename exceeds 99 characters.\n");
		exit(1);
	}

	//Check argv[3] for window size, shouldn't be less than 0
	if(atoi(argv[3]) <= 0){
		printf("Error: Invalid Window Size\n");
		exit(1);
	}

	//Check argv[4] for buffer size
	if(atoi(argv[4]) <= 0){
		printf("Error: Invalid Buffer Size\n");
		exit(1);
	}

	//Check argv[5] error rate
	if(atoi(argv[5]) < 0 || atoi(argv[5]) > 1){
		printf("Error: Invalid error rate, the acceptable range is [0,1]\n");
		exit(1); 
	}

	//Check argv[6], remote server name
	if(strlen(argv[6]) == 0  ){
		printf("Error: Remote-machine is empty\n");
	}

	 //Check argv[7] port number.
	 int remote_port = atoi(argv[7]);
	 if (remote_port <= 0 || remote_port > 65535) {
		 printf("Error: remote-port must be a valid port number.\n");
		 exit(1);
	 }
	
	
	portNumber = atoi(argv[7]);
	
	return portNumber;
}





