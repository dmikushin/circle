#include "lanl_circle.h"
#include "circle.hpp"
#include "lanl_circle.hpp"

#include <string.h>

using CallbackType = void(circle::Circle *);
using reduceOperationCallbackType = void(circle::Circle *circle,
                                         const void *buf1, size_t size1,
                                         const void *buf2, size_t size2);
using reduceFinalizeCallbackType = void(circle::Circle *circle, const void *buf,
                                        size_t size);

/**
 * Initialize a Circle instance for parallel processing.
 */
Circle circle_create_simple(circle_callback_func circle_create_callback,
                            circle_callback_func circle_process_callback,
                            CircleRuntimeFlags runtime_flags) {
  circle::Circle *circle = new circle::Circle(
      reinterpret_cast<CallbackType *>(circle_create_callback),
      reinterpret_cast<CallbackType *>(circle_process_callback),
      (circle::RuntimeFlags)runtime_flags);
  return (Circle)circle;
}

/**
 * Initialize a Circle instance for parallel processing and reduction.
 */
Circle circle_create(
    circle_callback_func circle_create_callback,
    circle_callback_func circle_process_callback,
    circle_callback_func circle_reduce_init_callback,
    circle_reduce_operation_callback_func circle_reduce_operation_callback,
    circle_reduce_finalize_callback_func circle_reduce_finalize_callback,
    CircleRuntimeFlags runtime_flags) {
  circle::Circle *circle = new circle::Circle(
      reinterpret_cast<CallbackType *>(circle_create_callback),
      reinterpret_cast<CallbackType *>(circle_process_callback),
      reinterpret_cast<CallbackType *>(circle_reduce_init_callback),
      reinterpret_cast<reduceOperationCallbackType *>(
          circle_reduce_operation_callback),
      reinterpret_cast<reduceFinalizeCallbackType *>(
          circle_reduce_finalize_callback),
      (circle::RuntimeFlags)runtime_flags);
  return (Circle)circle;
}

/*
 * Dispose the specified Circle instance.
 */
void circle_free(Circle circle) {
  delete reinterpret_cast<circle::Circle *>(circle);
}

enum CircleLogLevel circle_get_log_level(Circle circle) {
  return (CircleLogLevel) reinterpret_cast<circle::Circle *>(circle)
      ->getLogLevel();
}

/**
 * Define the detail of logging that Circle should output.
 */
void circle_set_log_level(Circle circle, enum CircleLogLevel level) {
  reinterpret_cast<circle::Circle *>(circle)->setLogLevel(
      (circle::LogLevel)level);
}

FILE *circle_get_log_stream(Circle circle) {
  return reinterpret_cast<circle::Circle *>(circle)->getLogStream();
}

enum CircleRuntimeFlags circle_get_runtime_flags(Circle circle) {
  return (CircleRuntimeFlags) reinterpret_cast<circle::Circle *>(circle)
      ->getRuntimeFlags();
}

/**
 * Change run time flags.
 */
void circle_set_runtime_flags(Circle circle, enum CircleRuntimeFlags options) {
  reinterpret_cast<circle::Circle *>(circle)->setRuntimeFlags(
      (circle::RuntimeFlags)options);
}

int circle_get_tree_width(Circle circle) {
  return reinterpret_cast<circle::Circle *>(circle)->getTreeWidth();
}

/**
 * Change the width of the k-ary communication tree.
 */
void circle_set_tree_width(Circle circle, int width) {
  reinterpret_cast<circle::Circle *>(circle)->setTreeWidth(width);
}

int circle_get_reduce_period(Circle circle) {
  return reinterpret_cast<circle::Circle *>(circle)->getReducePeriod();
}

/**
 * Change the number of seconds between consecutive reductions.
 */
void circle_set_reduce_period(Circle circle, int secs) {
  reinterpret_cast<circle::Circle *>(circle)->setReducePeriod(secs);
}

/**
 * Get an MPI rank corresponding to the current process.
 */
int circle_get_rank(Circle circle) {
  return reinterpret_cast<circle::Circle *>(circle)->getRank();
}

void circle_reduce(Circle circle, const void *buf, size_t size) {
  reinterpret_cast<circle::Circle *>(circle)->reduce(buf, size);
}

/**
 * Once you've defined and told Circle about your callbacks, use this to
 * execute your program.
 */
void circle_execute(Circle circle) {
  reinterpret_cast<circle::Circle *>(circle)->execute();
}

/**
 * Call this function to have all ranks dump a checkpoint file and exit.
 */
void circle_abort(Circle circle) {
  reinterpret_cast<circle::Circle *>(circle)->abort();
}

/**
 * Call this function to read in Circle restart files.
 */
int8_t circle_read_restarts(Circle circle) {
  return reinterpret_cast<circle::Circle *>(circle)->readRestarts();
}

/**
 * Call this function to read in Circle restart files.  Each rank
 * writes a file called circle<rank>.txt
 */
int8_t circle_checkpoint(Circle circle) {
  return reinterpret_cast<circle::Circle *>(circle)->checkpoint();
}

/**
 * The interface to the work queue. This can be accessed from within the
 * process and create work callbacks.
 */
int circle_enqueue(Circle circle, const uint8_t *element, size_t szelement) {
  std::vector<uint8_t> content(szelement);
  std::copy(element, element + szelement, content.begin());
  return reinterpret_cast<circle::Circle *>(circle)->enqueue(content);
}

namespace circle {
namespace internal {

int circle_dequeue(circle::Circle *circle, uint8_t *element,
                   size_t *szelement) {
  if (!element && !szelement)
    return -1;

  if (!element && szelement) {
    *szelement = circle->getImpl()->queue->lastSize();
    return 0;
  }

  std::vector<uint8_t> content;
  circle->dequeue(content);
  memcpy(element, reinterpret_cast<uint8_t *>(&content[0]),
         szelement ? std::min(*szelement, content.size()) : content.size());

  if (szelement)
    *szelement = content.size();

  return 0;
}

} // namespace internal
} // namespace circle

int circle_dequeue(Circle circle, uint8_t *element, size_t *szelement) {
  return circle::internal::circle_dequeue(
      reinterpret_cast<circle::Circle *>(circle), element, szelement);
}

uint32_t circle_get_local_queue_size(Circle circle) {
  return reinterpret_cast<circle::Circle *>(circle)->getLocalQueueSize();
}

/**
 *  Produce a stack trace with demangled function and method names.
 */
const char *circle_backtrace(int skip) { return circle::backtrace(skip); }

/**
 * Initialize internal state needed by Circle. This should be called before
 * any other Circle API call. This returns the MPI rank value.
 */
int circle_init(int *argc, char **argv[]) { return circle::init(argc, argv); }

/**
 * Returns an elapsed time on the calling processor for benchmarking purposes.
 */
double circle_wtime(void) { return circle::wtime(); }
