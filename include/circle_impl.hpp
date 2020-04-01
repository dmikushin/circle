#ifndef CIRCLE_IMPL_H
#define CIRCLE_IMPL_H

#include "queue.hpp"

#include <mpi.h>

namespace circle {

class Circle;

namespace internal {

class CircleImpl {

public : // TODO remove

  MPI_Comm comm;

  /* width of internal communication k-ary tree */
  int tree_width;

  circle::internal_queue_t *queue;

  friend class circle::Circle;
};

} // namespace internal
} // namespace circle

#endif /* CIRCLE_IMPL_H */

