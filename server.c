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

typedef enum
{
    DONE,
    FILENAME_ACK,
    SEND_DATA,
    WAIT_EOF_ACK 
} ServerState;

int main(int argc, char *argv[])
{

    int socketNum = 0;
    int portNumber = 0;

    portNumber = checkArgs(argc, argv);

    // Get socket number and add to poll set.
    socketNum = udpServerSetup(portNumber);
    setupPollSet();
    addToPollSet(socketNum);

    server_FSM(socketNum);

    close(socketNum);

    return 0;
}

// This function sends a filename ack
void send_filename_ack(int socketNum, struct sockaddr_in6 *client)
{
    uint8_t out_packet[7]; // Packet to be constructed
    int packet_len = 0;    // Packet size

    // Build packet
    packet_len = build_packet(out_packet, 0, FLAG_FILENAME_ACK, NULL, 0);

    // Calculate addr_len for safeSendTo
    int addr_len = sizeof(struct sockaddr_in6);

    // Send to client socket
    safeSendto(socketNum, out_packet, packet_len, 0, (struct sockaddr *)client, addr_len);
}

// This function sends a filename_error
void send_filename_error(int socketNum, struct sockaddr_in6 *client)
{
    uint8_t out_packet[7]; // Packet to be constructed
    int packet_len = 0;    // Packet size

    // Build packetwit
    packet_len = build_packet(out_packet, 0, FLAG_FILENAME_ERROR, NULL, 0);

    // Calculate addr_len for safeSendTo
    int addr_len = sizeof(struct sockaddr_in6);

    // Send to client socket
    safeSendto(socketNum, out_packet, packet_len, 0, (struct sockaddr *)client, addr_len);
}

/*This function processes the filename packet from rcopy.
  It sets the filename, window-size, and buffer-size*/
  FILE *processFilenameAck(int socketNum, CircularBuffer *window, struct sockaddr_in6 *client) {
    uint8_t buffer[MAX_PDU];  
    socklen_t addr_len = sizeof(struct sockaddr_in6);
    int attempts = 0;

    while (attempts < 10) {  // Allow up to 10 attempts
        printf("Attempt %d: Waiting for filename packet...\n", attempts + 1);
        // Receive the filename packet
        int dataLen = safeRecvfrom(socketNum, buffer, MAX_PDU, 0, (struct sockaddr *)client, (int *)&addr_len);
        if (dataLen < 0) {
            perror("Failed to receive filename packet");
            attempts++;
            continue;  // Retry if reception fails
        }

        // Verify checksum
        if (in_cksum((unsigned short *)buffer, dataLen) != 0) {
            fprintf(stderr, "Checksum verification failed (Attempt %d/10)\n", attempts + 1);
            attempts++;
            continue;  // Retry if checksum fails
        }

        printf("Checksum Passed!\n");

        // Extract window size and buffer size
        int window_size = ntohl(*(uint32_t *)(buffer + 7));
        int buffer_size = ntohl(*(uint32_t *)(buffer + 11));
        buffer_init(window, window_size, buffer_size, window_size);

        // Extract filename safely
        char filename[101] = {0};
        int filename_len = dataLen - 15;
        if (filename_len > 100) {
            filename_len = 100;
        }
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
    printf("Error: Max attempts (10) reached. Failed to receive valid filename packet.\n");
    return NULL;  // Return NULL after 10 failed attempts
}

/*Returns -1 when EOF*/
int read_file_to_buffer(CircularBuffer *window, FILE *export_file){
    size_t bytesRead; // Bytes read from fread

    uint8_t tempBuff[window->buffer_size]; //
    memset(tempBuff, 0, window->buffer_size);  // Clear buffer

    int sequence_num = window->current;
    int index = sequence_num % window->size;

    bytesRead = fread(tempBuff, 1, window->buffer_size, export_file);

    if (bytesRead == 0){
        // switch state to eof
        printf("END OF FILE!!!!!!!!!!!\n");
        return -1; // End of file
    }

    printf("----------------Seq Num: %d---------------------\n", sequence_num);
    printf("Highest: %d, Current: %d, Lowest: %d\n", window->highest, window->current, window->lowest);
    printf("\n");

    // Add the data to window data structure :)
    buffer_add(window, sequence_num, tempBuff, bytesRead);
    window->entries[index].data_len = bytesRead; //Add length of data to the index 


    printf("Data to be sent: %s\n", window->entries[index].data);

    return bytesRead;
}

// Function for sending data
void send_data(int socketNum, struct sockaddr_in6 *client, CircularBuffer *window, FILE *export_file, int bytesRead){

    // Variables for sending data
    int sequence_num = window->current;
    int index = sequence_num % window->size;

    printf("Current index: %d\n", index);
    // Build packet to be sent.
    uint8_t out_packet[MAX_PDU]; // Packet to be built

    // Build packet with data from buffer.
    build_packet(out_packet, sequence_num, FLAG_DATA, window->entries[index].data, bytesRead);

    printf("\n"); 
    printf("Highest: %d, Current: %d, Lowest: %d\n", window->highest, window->current, window->lowest);

    // Send filename packet ot the server
    int addr_Len = sizeof(struct sockaddr_in6);
    int out_packet_len = 7 + bytesRead;
    printf("packet length = %d\n", out_packet_len);

    // Send packet to rcopy
    safeSendto(socketNum, out_packet, out_packet_len, 0, (struct sockaddr *)client, addr_Len);

    // Increase current after sending
    window->current++;
}

/*This function is for resending a packet
  flag_option is for picking what flag to put in the header*/
  void resend_packet(int socketNum, struct sockaddr_in6 *client, uint32_t seq_num, CircularBuffer *window, int flag_option) {
    int index = seq_num % window->size;  // Get circular buffer index

    // // Ensure the packet exists before resending
    // if (!window->entries[index].valid_flag || window->entries[index].sequence_num != seq_num) {
    //     printf("Error: Cannot resend missing packet #%d (Not in buffer)\n", seq_num);
    //     return;
    // }

    printf("Resending packet #%d from buffer index %d\n", seq_num, index);

    // Get the correct data size
    int data_size = window->entries[index].data_len;  // Ensure we use the correct stored size

    // Build packet to be sent
    uint8_t out_packet[MAX_PDU];
    int packet_size = build_packet(out_packet, seq_num, flag_option, window->entries[index].data, data_size);

    printf("SIZE OF PACKET:%d\n",packet_size);

    // Send packet using correct length
    int addr_len = sizeof(struct sockaddr_in6);
    safeSendto(socketNum, out_packet, packet_size, 0, (struct sockaddr *)client, addr_len);
}

/*This function processes the packets coming from the client
  It returns the flag from the packet
  Returns -1 on error*/
int process_rr_srej_eof(int socketNum, struct sockaddr_in6 *client, CircularBuffer *window){
    uint8_t in_packet[MAX_PDU];
    int addr_len = sizeof(struct sockaddr_in6);

    // Receive the packet
    int recv_len = safeRecvfrom(socketNum, in_packet, MAX_PDU, 0, (struct sockaddr *)client, &addr_len);
    if (recv_len < 0){
        perror("Error receiving acknowledgment");
        return -1;
    }

    // Verify checksum
    if (in_cksum((unsigned short *)in_packet, recv_len) != 0){
        printf("Checksum error in acknowledgment packet. Ignoring.\n");
        return -1;
    }

    // Get SREJ/RR from the incoming packet
    uint32_t seq_num;
    memcpy(&seq_num, in_packet + 7, 4);
    seq_num = ntohl(seq_num);
    printf("Client is requesting packet:%d \n------", seq_num); 

    // Extract flag
    uint8_t flag = in_packet[6];
    printf("Flag from packet: %d\n", flag);

    //Check the flag and call send either RR or SREJ
    if (flag == FLAG_RR){
        printf("Received RR for packet #%d. Moving window forward.\n", seq_num);
        window->lowest = seq_num;
        window->highest = window->lowest + window->size;
    }else if (flag == FLAG_SREJ){
        printf("\n"); 
        printf("Received SREJ for packet #%d. Resending...\n", seq_num);
        resend_packet(socketNum, client, seq_num, window,FLAG_RESENT_DATA);
    }else if(flag == FLAG_EOF){
        printf("EOF FLAG DETECTED\n"); 

    }else{
        printf("Unexpected acknowledgment flag received. Ignoring.\n");
    }

    return flag; 
}

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

ServerState handle_send_data(int socketNum, struct sockaddr_in6 *client, CircularBuffer *window, FILE *export_file){
    // Send data packets while window is open
    while (window->current < window->highest){
        int readBytes = read_file_to_buffer(window, export_file);
        if (readBytes == -1){
            return WAIT_EOF_ACK; // EOF detected, transition to DONE
        }

        send_data(socketNum, client, window, export_file, readBytes);
    
        // Process acknowledgments (RR/SREJ)
        while ((pollCall(0)) >= 0){
            printf("--------------------------Server Checking for Data------------------------\n");
            process_rr_srej_eof(socketNum, client, window);
        }
        // If window is full, wait for acknowledgments
        int attempts = 0;
        while (window->current == window->highest){
            if(pollCall(1000) == -1){ // Poll with 1s timeout
                printf("--------------------------Server Resending Lowest Packet: %d------------------------\n", window->lowest);
                if (++attempts >= 10){
                    printf("Client timed out. Ending transfer.\n"); //Probably have to change to something else. 
                    return DONE;
                }
                resend_packet(socketNum, client, window->lowest, window,FLAG_RESENT_TIMEOUT);
            }else{
                process_rr_srej_eof(socketNum, client, window);
            }
        }
    }
    return SEND_DATA; // Continue sending data
}


ServerState handle_wait_EOF_ack(int socketNum, struct sockaddr_in6 *client, CircularBuffer *window) {
    int attempt = 0;
    int pollResult;

    while (attempt < 10) {
        send_eof(socketNum, client);
        printf("Sent EOF (Attempt %d/10)\n", attempt + 1);

        pollResult = pollCall(1000);
        
        if(pollResult >= 0) {
           if(FLAG_EOF == process_rr_srej_eof(socketNum,client, window)){
            return DONE;
            break;
           }
        } else if(pollResult == -1) {
            printf("Timeout waiting for EOF_ACK\n");
            attempt++;
        }
    }
    printf("EOF_ACK not received after 10 attempts. Terminating.\n");
    return DONE;
}

void server_FSM(int socketNum)
{
    // Initialize state for the server.
    ServerState state = FILENAME_ACK;
    FILE *export_file;

    // Initialize sendtoErr for simulating errors
    sendErr_init(ERROR_RATE, 1, 1, 1, 1);

    // Client Socket
    struct sockaddr_in6 client;

    // Initialize Circular Buffer
    CircularBuffer *window = (CircularBuffer *)malloc(sizeof(CircularBuffer));

    while (state != DONE){
        switch (state){
        case FILENAME_ACK:
            // Change to send_data state if file ack is successful.
            export_file = processFilenameAck(socketNum, window, &client);
            if (export_file == NULL){
                state = DONE; // Transition to DONE if file was not successfully opened
            }else{
                state = SEND_DATA;
                printf("Sending data...\n");
            }
            break;
        case SEND_DATA:
            state = handle_send_data(socketNum, &client, window, export_file);
            break;
        case WAIT_EOF_ACK:
            state = handle_wait_EOF_ack(socketNum,&client, window);
            if(state == DONE){
                if (export_file != NULL){
                    fclose(export_file);
                }
                printf("Transfer completed.\n");
                break;
            }
            break;
        default:
            state = DONE; // Default state to end the FSM
        }
    }
    buffer_free(window);
}

int checkArgs(int argc, char *argv[])
{
    // Checks args and returns port number
    int portNumber = 0;

    if (argc < 2 || argc > 3)
    {
        fprintf(stderr, "Usage: %s [error_rate] [optional port number]\n", argv[0]);
        exit(1);
    }

    // Check argv[1] error rate with strtof
    char *remainderPtr = NULL;
    ERROR_RATE = strtof(argv[1], &remainderPtr);
    if (*remainderPtr != '\0' || ERROR_RATE < 0 || ERROR_RATE > 1)
    {
        printf("Error: Invalid error rate, the acceptable range is [0,1]\n");
        exit(1);
    }
    printf("Server Error_rate: %f\n", ERROR_RATE);

    if (argc == 3)
    {
        portNumber = atoi(argv[2]);
    }

    printf("\n");
    return portNumber;
}
