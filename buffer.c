#include "buffer.h"

// Initialize the circular buffer
void buffer_init(CircularBuffer *buff, int window_size, int chunk_size) {
    buff->entries = (BufferEntry *)malloc(window_size * sizeof(BufferEntry));
    if (buff->entries == NULL) {
        perror("Memory Allocation Failure in Buffer Init");
        exit(1);
    }

    buff->highest = window_size - 1;  // No packets sent yet
    buff->lowest = 0;    // Lowest unacknowledged packet
    buff->current = 0;   // Next sequence number that can be sent
    buff->size = window_size;
    buff->buffer_size = chunk_size; 

    // Allocate memory for each chunk and mark invalid
    for (int i = 0; i < window_size; i++) {
        buff->entries[i].data = (char *)malloc(chunk_size);
        if (buff->entries[i].data == NULL) {
            perror("Memory Allocation Failure for BufferEntry Data");
            exit(1);
        }
        buff->entries[i].valid_flag = false;
        buff->entries[i].sequence_num = -1;
    }
}

// Add a data chunk to the buffer
void buffer_add(CircularBuffer *buff, int sequence_num, uint8_t *data, int data_size) {
    int index = sequence_num % buff->size;  // Circular index calculation

    // Store the data chunk
    memcpy(buff->entries[index].data, data, data_size);
    buff->entries[index].sequence_num = sequence_num;
    buff->entries[index].valid_flag = 1;

    // Update highest sequence number seen
    if (sequence_num > buff->highest) {
        buff->highest = sequence_num;
    }

    // Move current forward if this was the next expected packet
    if (sequence_num == buff->current) {
        buff->current++;
    }
}

// Remove an acknowledged packet from the buffer
void buffer_remove(CircularBuffer *buff, int sequence_num) {
    int index = sequence_num % buff->size;

    // Ensure it's a valid packet before removing
    if (buff->entries[index].valid_flag && buff->entries[index].sequence_num == sequence_num) {
        buff->entries[index].valid_flag = false;
        buff->entries[index].sequence_num = -1;

        // Move `lowest` forward if this was the lowest unacknowledged packet
        if (sequence_num == buff->lowest) {
            while (buff->entries[buff->lowest % buff->size].valid_flag == false &&
                   buff->lowest <= buff->highest) {
                buff->lowest++;
            }
        }
    }
}

// Free dynamically allocated memory
void buffer_free(CircularBuffer *buff) {
    for (int i = 0; i < buff->size; i++) {
        free(buff->entries[i].data);
    }
    free(buff->entries);
}
