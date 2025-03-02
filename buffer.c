#include <stdio.h> 
#include <stdlib.h> 
#include "buffer.h"
#include "communication.h"

#define WINDOW_SIZE 3 

//Initializes a buffer 
void buffer_init(Circular_Buffer *c){
    c->entries = malloc(WINDOW_SIZE * sizeof(BufferEntry)); //Dynamically Allocate Memory base off of window size
    if(c->entries == NULL){
        perror("Memory Allocation Failure in Buffer Init");
        exit(0); 
    }
    c->highest = -1; 
    c->expected = 0; 

    //Set all valid flags to zero 
    for(int i = 0; i < WINDOW_SIZE;i++){
        c->entries[i].valid_flag = 0; 
    }
}   


//This function adds an out of order packet to the buffer. 
void buffer_add(Circular_Buffer *c, int sequence_num, Packet in_packet){
   
    //Put packet into index and make index valid
    int index = sequence_num % WINDOW_SIZE;
    // c->entries[index].packet = in_packet;
    c->entries[index].valid_flag = 1; 



    // Check and update highest. 
    if (sequence_num  > c->highest) {
        c->highest = in_packet.sequence_num;
    }
}


void buffer_write_to_disk(Circular_Buffer *c, FILE *out_file){
    //While expected is valid, we write to buffer. 
    //Do the calculation for the buffer. 
    int index = c->expected % WINDOW_SIZE; 
    // while(c->entries[index].packet.sequence_num == c->expected && c->entries[index].valid_flag){
        
    // }
}


// Frees the dynamically allocated entries
void buffer_free(Circular_Buffer *c) {
    free(c->entries);
    c->entries = NULL; 
}

