#include <lanl_circle.h>
#include <stdio.h>
#include <stdlib.h>

static const int npoints = 100000, njobs = 10;
static double pi_partial;

/*
 * The reduce_init callback provides the memory address and size of the
 * variable(s) to use as input on each process to the reduction
 * operation.  One can specify an arbitrary block of data as input.
 * When a new reduction is started, Circle invokes this callback on
 * each process to snapshot the memory block specified in the call to
 * circle::reduce.  The library makes a memcpy of the memory block, so
 * its contents can be safely changed or go out of scope after the call
 * to circle::reduce returns.
 */
static void my_reduce_init(Circle example) {
  /*
   * We give the starting memory address and size of a memory
   * block that we want Circle to capture on this process when
   * it starts a new reduction operation.
   *
   * In this example, we capture a single floating-point value,
   * which is the global pi_partial variable.
   */
  circle_reduce(example, &pi_partial, sizeof(pi_partial));
}

/*
 * On intermediate nodes of the reduction tree, Circle invokes the
 * reduce_op callback to reduce two data buffers.  The starting
 * address and size of each data buffer are provided as input
 * parameters to the callback function.  An arbitrary reduction
 * operation can be executed.  Then Circle snapshots the memory
 * block specified in the call to circle::reduce to capture the partial
 * result.  The library makes a memcpy of the memory block, so its
 * contents can be safely changed or go out of scope after the call to
 * circle::reduce returns.
 *
 * Note that the sizes of the input buffers do not have to be the same,
 * and the output buffer does not need to be the same size as either
 * input buffer.  For example, one could concatentate buffers so that
 * the reduction actually performs a gather operation.
 */
static void my_reduce_op(Circle example, const void* a, size_t a_size, const void* b, size_t b_size) {
  /*
   * Here we are given the starting address and size of two input
   * buffers.  These could be the initial memory blocks copied during
   * reduce_init, or they could be intermediate results copied from a
   * reduce_op call.  We can execute an arbitrary operation on these
   * input buffers and then we save the partial result to a call
   * to circle::reduce.
   *
   * In this example, we sum two input uint64_t values and
   * Circle makes a copy of the result when we call circle::reduce.
   */
  const double res = *(const double*)a + *(const double*)b;
  circle_reduce(example, &res, sizeof(res));
}

/*
 * The reduce_fini callback is only invoked on the root process.  It
 * provides a buffer holding the final reduction result as in input
 * parameter. Typically, one might print the result in this callback.
 */
static void my_reduce_fini(Circle example, const void* pi_total, size_t size) {
  /*
   * In this example, we get the reduced sum from the input buffer,
   * and we compute the approximate value of PI.
   */
  printf("result = %f\n", (*(const double*)pi_total) * 4 / njobs);
}

/**
 * An example of a create callback defined by your program.
 */
static void my_create_some_work(Circle example) {
  /*
   * This is where you should generate work that needs to be processed.
   * For example, we can generate RNG seeds to be used by worker processes.
   *
   * By default, the create callback is only executed on the root
   * process, i.e., the process whose call to circle::init returns 0.
   * If the circle::CREATE_GLOBAL option flag is specified, the create
   * callback is invoked on all processes.
   */

  for (int i = 0; i < njobs; i++) {
    int seed = i;
    circle_enqueue(example, (const uint8_t *)&seed, sizeof(seed));
  }
}

/**
 * An example of a process callback defined by your program.
 */
static void my_process_some_work(Circle example) {
  /*
   * Master process sends us the random seed that he generated,
   * as an example of data sharing. We use this seed in our processing.
   */
  size_t szseed = 0;
  circle_dequeue(example, NULL, &szseed);
  unsigned int* seed = malloc(szseed);
  circle_dequeue(example, (uint8_t *)seed, &szseed);
  printf("Rank %d received seed = %d\n", circle_get_rank(example), seed[0]);
  srand(*seed);
  free(seed);

  /*
   * This is where work should be processed. For example, this is where you
   * should size one of the files which was placed on the queue by your
   * create_some_work callback. You should try to keep this short and block
   * as little as possible.
   */
  int ncircle = 0;
  for (int i = 0; i < npoints; i++) {
    double rval[2];
    rval[0] = rand();
    rval[1] = rand();
    rval[0] /= (double)RAND_MAX;
    rval[1] /= (double)RAND_MAX;
    if (rval[0] * rval[0] + rval[1] * rval[1] <= 1.0)
      ncircle++;
  }

  pi_partial += (double)ncircle / npoints;
}

int main(int argc, char* argv[]) {
  circle_init(&argc, &argv);
  Circle example = circle_create(my_create_some_work, my_process_some_work,
                     my_reduce_init, my_reduce_op, my_reduce_fini,
                     CircleDefaultFlags);
  circle_set_log_level(example, CircleInfo);

  pi_partial = 0.0;

  /*
   * Specify time period between consecutive reductions.
   * Here we set a time period of 10 seconds.
   */
  circle_set_reduce_period(example, 10);

  circle_execute(example);

  return 0;
}

