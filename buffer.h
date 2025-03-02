#ifndef BUFFER_H
#define BUFFER_H

#include <stdio.h> 
#include <stdlib.h> 
#include <stdbool.h> 
#include <stdint.h> 
#include <string.h> 
#include "communication.h"

//SIZE
#define WINDOW_SIZE 3

typedef struct{
    Packet *packet; 
    int valid_flag; 
}BufferEntry; 

typedef struct{
    BufferEntry *entries; 
    int highest; 
    int expected;
}Circular_Buffer;

void buffer_init(Circular_Buffer *buff); 
void buffer_add(Circular_Buffer *c, int sequence_num, Packet in_packet);
void buffer_free(Circular_Buffer *buff); 


#endif 