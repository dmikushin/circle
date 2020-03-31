#ifndef LIB_H
#define LIB_H

#include <config.hpp>

#include "libcircle.hpp"
#include "queue.hpp"

namespace circle {

typedef struct input_st {
    circle::cb create_cb;
    circle::cb process_cb;

    circle::cb_reduce_init_fn reduce_init_cb;
    circle::cb_reduce_op_fn   reduce_op_cb;
    circle::cb_reduce_fini_fn reduce_fini_cb;
    void*  reduce_buf;
    size_t reduce_buf_size;
    int reduce_period;

    MPI_Comm comm;

    circle::RuntimeFlags options;

    /* width of internal communication k-ary tree */
    int tree_width;

    circle::internal_queue_t* queue;
} input_st;

} // namespace circle

#endif /* LIB_H */
