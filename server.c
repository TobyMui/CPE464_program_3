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

#define MAXBUF 80

uint32_t WINDOW_SIZE = 0; 
uint32_t BUFFER_SIZE = 0; 

void processClient(int socketNum);
int checkArgs(int argc, char *argv[]);

int main ( int argc, char *argv[]  )
{ 
	int socketNum = 0;				
	int portNumber = 0;

	portNumber = checkArgs(argc, argv);


	socketNum = udpServerSetup(portNumber);
    while(1){
	processClient(socketNum);
    }

	close(socketNum);

	
	return 0;
}

void processFilenameAck(int socketNum, struct sockaddr_in6 *client, int clientAddrLen, uint8_t *in_buffer, int in_buff_len) {
    // Extract sequence number
    uint32_t sequence_num = ntohl(*(uint32_t *)in_buffer);

    // Extract window size and buffer size
    WINDOW_SIZE = ntohl(*(uint32_t *)(in_buffer + 7));
    BUFFER_SIZE = ntohl(*(uint32_t *)(in_buffer + 11));

    // Extract filename safely
    char filename[100] = {0};  // Ensure it's null-terminated
    int filename_len = in_buff_len - 15;
    if (filename_len > 99) filename_len = 99;  // Prevent buffer overflow
    strncpy(filename, (char *)(in_buffer + 15), filename_len);
    filename[filename_len] = '\0'; // Ensure null termination

    printf("Filename requested: %s\n", filename);
    printf("Window Size: %u, Buffer Size: %u\n", WINDOW_SIZE, BUFFER_SIZE);

    // Verify checksum
    if (in_cksum((unsigned short *)in_buffer, in_buff_len) != 0) {
        fprintf(stderr, "Checksum verification failed\n");
        return;
    }
    printf("Checksum Passed!\n");

    // Send acknowledgment back to the client using sendtoErr
    uint8_t ack_packet[MAX_PDU] = {0};
    *(uint32_t *)ack_packet = htonl(sequence_num); // Echo the sequence number
    ack_packet[6] = FLAG_FILENAME_ACK; // Set the acknowledgment flag

    if (sendtoErr(socketNum, ack_packet, 7, 0, (struct sockaddr *)client, clientAddrLen) < 0) {
        perror("Failed to send ACK packet");
    } else {
        printf("ACK sent successfully\n");
    }

    // Attempt to open the requested file
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file");
        return;
    }
    fclose(file);
}

void processClient(int socketNum) {
    uint8_t buffer[MAX_PDU];
    struct sockaddr_in6 client;
    int clientAddrLen = sizeof(client);

    // Receive the filename packet
    int dataLen = safeRecvfrom(socketNum, buffer, MAX_PDU, 0, (struct sockaddr *)&client, &clientAddrLen);
    if (dataLen < 0) {
        perror("Failed to receive filename packet");
        return;
    }

    // Process filename and send acknowledgment
    processFilenameAck(socketNum, &client, clientAddrLen, buffer, dataLen);
}



int checkArgs(int argc, char *argv[])
{
	// Checks args and returns port number
	int portNumber = 0;

	if (argc > 2)
	{
		fprintf(stderr, "Usage %s [optional port number]\n", argv[0]);
		exit(-1);
	}
	
	if (argc == 2)
	{
		portNumber = atoi(argv[1]);
	}
	
	return portNumber;
}


