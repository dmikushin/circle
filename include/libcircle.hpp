#ifndef LIBCIRCLE_H
#define LIBCIRCLE_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

/**
 * The maximum length of a string value which is allowed to be placed on the
 * queue structure.
 */
#ifdef PATH_MAX
#define CIRCLE_MAX_STRING_LEN PATH_MAX
#else
#define CIRCLE_MAX_STRING_LEN (4096)
#endif

namespace circle {

/**
 * Run time flags for the behavior of splitting work.
 */
enum class RuntimeFlags : unsigned {
  None = 0,
  SplitRandom = 1 << 0,      /* Split work randomly. */
  SplitEqual = 1 << 1,       /* Split work evenly */
  CreateGlobal = 1 << 2,     /* Call create callback on all procs */
  TermTree = 1 << 3,         /* Use tree-based termination */
  DefaultFlags = SplitEqual, /* Default behavior is random work stealing */
};

inline constexpr RuntimeFlags operator&(RuntimeFlags x, RuntimeFlags y) {
  return static_cast<RuntimeFlags>(static_cast<unsigned>(x) &
                                   static_cast<unsigned>(y));
}

inline constexpr RuntimeFlags operator|(RuntimeFlags x, RuntimeFlags y) {
  return static_cast<RuntimeFlags>(static_cast<unsigned>(x) |
                                   static_cast<unsigned>(y));
}

/**
 * The various logging levels that libcircle will output.
 */
enum class LogLevel : unsigned {
  None = 0,
  Fatal = 1,
  Error = 2,
  Warning = 3,
  Info = 4,
  Debug = 5
};

/**
 * The interface to the work queue. This can be accessed from within the
 * process and create work callbacks.
 */
class WorkQueue {

public :

  int enqueue(const std::vector<uint8_t> &element);
  int enqueue(const std::string &element);

  int dequeue(std::vector<uint8_t> &element);
  int dequeue(std::string &element);

  uint32_t localQueueSize();
};

/**
 * The type for defining callbacks for create and process.
 */
typedef void (*cb)(circle::WorkQueue *handle);

/**
 * Callbacks for initializing, executing, and obtaining final result
 * of a reduction
 */
typedef void (*cb_reduce_init_fn)(void);
typedef void (*cb_reduce_op_fn)(const void *buf1, size_t size1,
                                const void *buf2, size_t size2);
typedef void (*cb_reduce_fini_fn)(const void *buf, size_t size);

/**
 *  Produce a stack trace with demangled function and method names.
 */
const char *backtrace(int skip);

/**
 * Initialize internal state needed by libcircle. This should be called before
 * any other libcircle API call. This returns the MPI rank value.
 */
int init(int argc, char *argv[], circle::RuntimeFlags options);

/**
 * Change run time flags
 */
void set_options(circle::RuntimeFlags options);

/**
 * Change the width of the k-ary communication tree.
 */
void set_tree_width(int width);

/**
 * Change the number of seconds between consecutive reductions.
 */
void set_reduce_period(int secs);

/**
 * Processing and creating work is done through callbacks. Here's how we tell
 * libcircle about our function which creates work. This call is optional.
 */
void cb_create(circle::cb func);

/**
 * After you give libcircle a way to create work, you need to tell it how that
 * work should be processed.
 */
void cb_process(circle::cb func);

/**
 * Specify function that libcircle should call to get initial data for
 * a reduction.
 */
void cb_reduce_init(circle::cb_reduce_init_fn);

/**
 * Specify function that libcircle should call to execute a reduction
 * operation.
 */
void cb_reduce_op(circle::cb_reduce_op_fn);

/**
 * Specify function that libcicle should invoke at end of reduction.
 * This function is only invoked on rank 0.
 */
void cb_reduce_fini(circle::cb_reduce_fini_fn);

/**
 * Provide libcircle with initial reduction data during initial
 * and intermediate reduction callbacks, libcircle makes a copy
 * of the data so the user buffer can be immediately released.
 */
void reduce(const void *buf, size_t size);

/**
 * Once you've defined and told libcircle about your callbacks, use this to
 * execute your program.
 */
void begin(void);

/**
 * Call this function to have all ranks dump a checkpoint file and exit.
 */
void abort(void);

/**
 * Call this function to checkpoint libcircle's distributed queue. Each rank
 * writes a file called circle<rank>.txt
 */
int8_t checkpoint(void);

/**
 * Function to return a pointer to the handle.  Useful for threaded
 * applications. You are responsible for maintaining mutual exclusion.
 */
WorkQueue *get_handle(void);

/**
 * Call this function to initialize libcircle queues from restart files
 * created by checkpoint.
 */
int8_t read_restarts(void);

/**
 * After your program has executed, give libcircle a chance to clean up after
 * itself by calling this. This should be called after all libcircle API calls.
 */
void finalize(void);

/**
 * Define the detail of logging that libcircle should output.
 */
void enable_logging(enum LogLevel level);

/**
 * Returns an elapsed time on the calling processor for benchmarking purposes.
 */
double wtime(void);

} // namespace circle

#endif /* LIBCIRCLE_H */
