#ifndef BUFFER_H
#define BUFFER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t *data;    // Will be set by buffer-size
    int sequence_num; // Packet sequence number
    bool valid_flag;  // If the chunk is stored in the buffer
    int data_len;      // length of data
} BufferEntry;

typedef struct {
    BufferEntry *entries;
    int highest;  // Highest sent sequence number
    int lowest;   // Lowest unacknowledged sequence number
    int current;  // Next sequence number that can be sent
    int size;     // Window size 
    int buffer_size; //Buffer Size 
} CircularBuffer;

void buffer_init(CircularBuffer *buff, int window_size, int chunk_size, int highest);
void buffer_add(CircularBuffer *buff, int sequence_num, uint8_t *data, int data_size);
void buffer_free(CircularBuffer *buff);

#endif