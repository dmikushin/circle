#ifndef LANL_CIRCLE_H
#define LANL_CIRCLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run time flags for the behavior of splitting work.
 */
enum CircleRuntimeFlags {
  CircleSplitRandom = 1 << 0,      /* Split work randomly. */
  CircleSplitEqual = 1 << 1,       /* Split work evenly */
  CircleCreateGlobal = 1 << 2,     /* Call create callback on all procs */
  CircleTermTree = 1 << 3,         /* Use tree-based termination */
  CircleDefaultFlags = CircleSplitEqual, /* Default behavior is random work stealing */
};

/**
 * The various logging levels that Circle will output.
 */
enum CircleLogLevel { CircleNone = 0, CircleFatal = 1, CircleError = 2, CircleWarning = 3, CircleInfo = 4, CircleDebug = 5 };

typedef struct _circle *Circle;

/**
 * The type for defining callbacks for create and process.
 */
typedef void (*circle_callback_func)(Circle circle);

/**
 * Callbacks for initializing, executing, and obtaining final result
 * of a reduction
 */
typedef void (*circle_reduce_init_callback_func)(Circle circle);
typedef void (*circle_reduce_operation_callback_func)(Circle circle,
                                                      const void *buf1,
                                                      size_t size1,
                                                      const void *buf2,
                                                      size_t size2);
typedef void (*circle_reduce_finalize_callback_func)(Circle circle,
                                                     const void *buf,
                                                     size_t size);

/**
 * Initialize a Circle instance for parallel processing.
 */
Circle circle_create_simple(
    circle_callback_func create_callback,
    circle_callback_func circle_process_callback,
    CircleRuntimeFlags runtime_flags);

/**
 * Initialize a Circle instance for parallel processing and reduction.
 */
Circle circle_create(
    circle_callback_func create_callback,
    circle_callback_func circle_process_callback,
    circle_reduce_init_callback_func circle_reduce_init_callback,
    circle_reduce_operation_callback_func circle_reduce_operation_callback,
    circle_reduce_finalize_callback_func circle_reduce_finalize_callback,
    CircleRuntimeFlags runtime_flags);

enum CircleLogLevel circle_get_log_level(Circle circle);

/**
 * Define the detail of logging that Circle should output.
 */
void circle_set_log_level(Circle circle, enum CircleLogLevel level);

FILE *circle_get_log_stream(Circle circle);

enum CircleRuntimeFlags circle_get_runtime_flags(Circle circle);

/**
 * Change run time flags.
 */
void circle_set_runtime_flags(Circle circle, enum CircleRuntimeFlags options);

int circle_get_tree_width(Circle circle);

/**
 * Change the width of the k-ary communication tree.
 */
void circle_set_tree_width(Circle circle, int width);

int circle_get_reduce_period(Circle circle);

/**
 * Change the number of seconds between consecutive reductions.
 */
void circle_set_reduce_period(Circle circle, int secs);

/**
 * Get an MPI rank corresponding to the current process.
 */
int circle_get_rank(Circle circle);

void circle_reduce(Circle circle, const void *buf, size_t size);

/**
 * Once you've defined and told Circle about your callbacks, use this to
 * execute your program.
 */
void circle_execute(Circle circle);

/**
 * Call this function to have all ranks dump a checkpoint file and exit.
 */
void circle_abort(Circle circle);

/**
 * Call this function to read in Circle restart files.
 */
int8_t circle_read_restarts(Circle circle);

/**
 * Call this function to read in Circle restart files.  Each rank
 * writes a file called circle<rank>.txt
 */
int8_t circle_checkpoint(Circle circle);

/**
 * The interface to the work queue. This can be accessed from within the
 * process and create work callbacks.
 */
int circle_enqueue(Circle circle, const uint8_t *element, size_t szelement);

int circle_dequeue(Circle circle, uint8_t *element, size_t *szelement);

uint32_t circle_get_local_queue_size(Circle circle);

/**
 *  Produce a stack trace with demangled function and method names.
 */
const char *circle_backtrace(int skip);

/**
 * Initialize internal state needed by Circle. This should be called before
 * any other Circle API call. This returns the MPI rank value.
 */
int circle_init(int *argc, char **argv[]);

/**
 * Returns an elapsed time on the calling processor for benchmarking purposes.
 */
double circle_wtime(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* LANL_CIRCLE_H */
