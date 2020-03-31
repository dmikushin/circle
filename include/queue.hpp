#ifndef INTERNAL_QUEUE_H
#define INTERNAL_QUEUE_H

#include <stdint.h>

/* The initial queue size for malloc. */
#ifndef CIRCLE_INITIAL_INTERNAL_QUEUE_SIZE
#define CIRCLE_INITIAL_INTERNAL_QUEUE_SIZE 4096
#endif

namespace circle {

typedef struct internal_queue_t {
    char* base;         /* Base of the memory pool */
    size_t bytes;       /* current capacity of queue in bytes */
    uintptr_t head;     /* The location of the next free byte */
    uintptr_t* strings; /* The string data */
    int32_t str_count;  /* The maximum number of strings the queue can hold */
    int32_t count;      /* The number of actively queued strings */
} internal_queue_t;

circle::internal_queue_t* internal_queue_init(void);
int8_t internal_queue_free(circle::internal_queue_t* qp);

int8_t internal_queue_push(circle::internal_queue_t* qp, const char* str);
int8_t internal_queue_pop(circle::internal_queue_t* qp, char* str);

void internal_queue_dump(circle::internal_queue_t* qp);
void internal_queue_print(circle::internal_queue_t* qp);

int8_t internal_queue_write(circle::internal_queue_t* qp, int rank);
int8_t internal_queue_read(circle::internal_queue_t* qp, int rank);
int8_t internal_queue_extend(circle::internal_queue_t* qp, size_t size);
int8_t internal_queue_str_extend(circle::internal_queue_t* qp, int32_t count);

} // namespace circle

#endif /* QUEUE_H */
