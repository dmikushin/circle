#ifndef WORKER_H
#define WORKER_H

#include "libcircle.hpp"
#include "token.hpp"

#define LIBCIRCLE_MPI_ERROR 32

namespace circle {

int8_t worker(void);
void reset_request_vector(circle::state_st* st);

/* TODO: move me to a util file */
void free(void* ptr);

} // namespace circle

#endif /* WORKER_H */
