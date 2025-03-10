#include "buffer.h"

// Initialize the circular buffer
void buffer_init(CircularBuffer *buff, int window_size, int chunk_size, int highest) {
    buff->entries = (BufferEntry *)malloc(window_size * sizeof(BufferEntry));
    if (buff->entries == NULL) {
        perror("Memory Allocation Failure in Buffer Init");
        exit(1);
    }

    buff->highest = highest; 
    buff->lowest = 0;  
    buff->current = 0;  //Also known as expected for rcopy
    buff->size = window_size;
    buff->buffer_size = chunk_size; 

    // Allocate memory for each chunk and mark invalid
    for (int i = 0; i < window_size; i++) {
        buff->entries[i].data = (uint8_t *)malloc(chunk_size);
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
    memset(buff->entries[index].data, 0, buff->buffer_size);
    memcpy(buff->entries[index].data, data, data_size);
    buff->entries[index].sequence_num = sequence_num;
    buff->entries[index].valid_flag = 1;
}

// Free dynamically allocated memory
void buffer_free(CircularBuffer *buff) {
    for (int i = 0; i < buff->size; i++) {
        free(buff->entries[i].data);
    }
    free(buff->entries);
    free(buff); 
}
