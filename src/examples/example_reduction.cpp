#include <iostream>
#include <libcircle.h>
#include <ghc/filesystem.hpp>

namespace fs = ghc::filesystem;
using namespace std;

static size_t sztotal_partial = 0;

/*
 * The reduce_init callback provides the memory address and size of the
 * variable(s) to use as input on each process to the reduction
 * operation.  One can specify an arbitrary block of data as input.
 * When a new reduction is started, libcircle invokes this callback on
 * each process to snapshot the memory block specified in the call to
 * CIRCLE_reduce.  The library makes a memcpy of the memory block, so
 * its contents can be safely changed or go out of scope after the call
 * to CIRCLE_reduce returns.
 */
static void reduce_init(void)
{
    /*
     * We give the starting memory address and size of a memory
     * block that we want libcircle to capture on this process when
     * it starts a new reduction operation.
     *
     * In this example, we capture a single uint64_t value,
     * which is the global reduce_count variable.
     */
    CIRCLE_reduce(&sztotal_partial, sizeof(size_t));
}

/*
 * On intermediate nodes of the reduction tree, libcircle invokes the
 * reduce_op callback to reduce two data buffers.  The starting
 * address and size of each data buffer are provided as input
 * parameters to the callback function.  An arbitrary reduction
 * operation can be executed.  Then libcircle snapshots the memory
 * block specified in the call to CIRCLE_reduce to capture the partial
 * result.  The library makes a memcpy of the memory block, so its
 * contents can be safely changed or go out of scope after the call to
 * CIRCLE_reduce returns.
 *
 * Note that the sizes of the input buffers do not have to be the same,
 * and the output buffer does not need to be the same size as either
 * input buffer.  For example, one could concatentate buffers so that
 * the reduction actually performs a gather operation.
 */
static void reduce_op(const void* buf1, size_t size1, const void* buf2, size_t size2)
{
    /*
     * Here we are given the starting address and size of two input
     * buffers.  These could be the initial memory blocks copied during
     * reduce_init, or they could be intermediate results copied from a
     * reduce_op call.  We can execute an arbitrary operation on these
     * input buffers and then we save the partial result to a call
     * to CIRCLE_reduce.
     *
     * In this example, we sum two input uint64_t values and
     * libcircle makes a copy of the result when we call CIRCLE_reduce.
     */
    uint64_t a = *(const uint64_t*) buf1;
    uint64_t b = *(const uint64_t*) buf2;
    uint64_t sum = a + b;
    CIRCLE_reduce(&sum, sizeof(uint64_t));
}

/*
 * The reduce_fini callback is only invoked on the root process.  It
 * provides a buffer holding the final reduction result as in input
 * parameter. Typically, one might print the result in this callback.
 */
static void reduce_fini(const void* buf, size_t size)
{
    /*
     * In this example, we get the reduced sum from the input buffer,
     * and we compute the average processing rate.  We then print
     * the count, time, and rate of items processed.
     */

    // get result of reduction
    const size_t sztotal = *reinterpret_cast<const size_t*>(buf);
    cout << "sztotal = " << sztotal << endl;
}

/* An example of a create callback defined by your program */
static void my_create_some_work(CIRCLE_handle *handle)
{
    /*
     * This is where you should generate work that needs to be processed.
     * For example, if your goal is to size files on a cluster filesystem,
     * this is where you would read directory and and enqueue directory names.
     *
     * By default, the create callback is only executed on the root
     * process, i.e., the process whose call to CIRCLE_init returns 0.
     * If the CIRCLE_CREATE_GLOBAL option flag is specified, the create
     * callback is invoked on all processes.
     */

    const fs::path directory = "/bin/";
    if (fs::exists(directory) && fs::is_directory(directory))
    {
        for (fs::directory_iterator i(directory), ie; i != ie; i++)
        {
                if (!fs::exists(i->status()) || !fs::is_regular_file(i->status()))
                        continue;

                const char* filename = i->path().string().c_str();
                handle->enqueue(filename);
        }
    }
}

void store_in_database(size_t finished_work)
{
    sztotal_partial += finished_work;
}

/* An example of a process callback defined by your program. */
static void my_process_some_work(CIRCLE_handle *handle)
{
    /*
     * This is where work should be processed. For example, this is where you
     * should size one of the files which was placed on the queue by your
     * create_some_work callback. You should try to keep this short and block
     * as little as possible.
     */
    char my_data[1024];
    handle->dequeue(my_data);

    size_t finished_work = fs::file_size(my_data);

    store_in_database(finished_work);
}

int main(int argc, char* argv[])
{
    /*
     * Do partial computations with libcircle.
     */
    int rank = CIRCLE_init(argc, argv, CIRCLE_DEFAULT_FLAGS);
    CIRCLE_cb_create(&my_create_some_work);
    CIRCLE_cb_process(&my_process_some_work);

    /*
     * Reduce the final result.
     */
    CIRCLE_cb_reduce_init(&reduce_init);
    CIRCLE_cb_reduce_op(&reduce_op);
    CIRCLE_cb_reduce_fini(&reduce_fini);

    /*
     * Specify time period between consecutive reductions.
     * Here we set a time period of 10 seconds.
     */
    CIRCLE_set_reduce_period(10);

    CIRCLE_begin();
    CIRCLE_finalize();

    return 0;
}

