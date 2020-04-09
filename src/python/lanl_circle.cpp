#include <pybind11/pybind11.h>

#include "lanl_circle.h"

namespace py = pybind11;

PYBIND11_MODULE(hddm_solver, m)
{
    m.doc() = "Python interface for LANL Circle";

    py::enum_<CircleRuntimeFlags>(m, "RuntimeFlags", py::arithmetic(), "Run time flags for the behavior of splitting work")
        .value("SplitRandom", CircleSplitRandom, "Split work randomly")
        .value("SplitEqual", CircleSplitEqual, "Split work evenly")
	.value("CreateGlobal", CircleCreateGlobal, "Call create callback on all procs")
	.value("TermTree", CircleTermTree, "Use tree-based termination")
	.value("DefaultFlags", CircleSplitEqual, "Default behavior is random work stealing");

    py::enum_<CircleLogLevel>(m, "LogLevel", py::arithmetic(), "The various logging levels that Circle will output")
        .value("CircleNone", CircleNone)
	.value("CircleFatal", CircleFatal)
	.value("CircleError", CircleError)
	.value("CircleWarning", CircleWarning)
	.value("CircleInfo", CircleInfo)
	.value("CircleDebug", CircleDebug);

#if 0
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
    enum CircleRuntimeFlags runtime_flags);

/**
 * Initialize a Circle instance for parallel processing and reduction.
 */
Circle circle_create(
    circle_callback_func create_callback,
    circle_callback_func circle_process_callback,
    circle_reduce_init_callback_func circle_reduce_init_callback,
    circle_reduce_operation_callback_func circle_reduce_operation_callback,
    circle_reduce_finalize_callback_func circle_reduce_finalize_callback,
    enum CircleRuntimeFlags runtime_flags);

/*
 * Dispose the specified Circle instance.
 */
void circle_free(Circle circle);

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
 * Initialize internal state needed by Circle. This should be called before
 * any other Circle API call. This returns the MPI rank value.
 */
int circle_init(int *argc, char **argv[]);

/**
 * Returns an elapsed time on the calling processor for benchmarking purposes.
 */
double circle_wtime(void);
#endif
}


