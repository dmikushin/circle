#ifndef WORKER_H
#define WORKER_H

#include "libcircle.hpp"
#include "token.hpp"

#define LIBCIRCLE_MPI_ERROR 32

namespace circle {

int8_t worker(void);
void reset_request_vector(circle::state_st *st);

/* provides address of pointer, and if value of pointer is not NULL,
 * frees memory and sets pointer value to NULL */
template <typename T> void free(T **pptr) {
  if (pptr != NULL) {
    if (*pptr != NULL) {
      ::free(*pptr);
      *pptr = NULL;
    }
  }

  return;
}

} // namespace circle

#endif /* WORKER_H */
