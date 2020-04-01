/**
 * @file
 *
 * The abstraction of a worker process.
 */

#include <arpa/inet.h>
#include <dirent.h>
#include <inttypes.h>
#include <mpi.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "lanl_circle.hpp"
#include "circle_impl.hpp"
#include "log.hpp"
#include "token.hpp"
#include "worker.hpp"

namespace circle {
namespace impl {

int8_t ABORT_FLAG;

extern circle::Circle circle;

} // namespace impl
} // namespace circle

using namespace circle::impl;

circle::WorkQueue queue_handle;

/*
 * Define as described in gethostent(3) for backwards compatibility.
 */
#ifndef h_addr
#define h_addr h_addr_list[0]
#endif /* h_addr */

const char *circle::backtrace(int skip);

/**
 * @brief Function to be called in the event of an MPI error.
 *
 * This function get registered with MPI to be called
 * in the event of an MPI Error.  It attempts
 * to checkpoint.
 */
#pragma GCC diagnostic ignored "-Wunused-parameter"
static void MPI_error_handler(MPI_Comm *comm, int *err, ...) {
  const char *backtrace = circle::backtrace(1);

  char name[MPI_MAX_OBJECT_NAME];
  int namelen;
  MPI_Comm_get_name(*comm, name, &namelen);

  if (*err == LIBCIRCLE_MPI_ERROR) {
    LOG(circle::LogLevel::Error, "Libcircle received abort signal, checkpointing.");
  } else {
    char error[MPI_MAX_ERROR_STRING];
    int error_len = 0;
    MPI_Error_string(*err, error, &error_len);
    LOG(circle::LogLevel::Error, "MPI Error in Comm [%s]: %s", name, error);
    LOG(circle::LogLevel::Error, "Backtrace:\n%s\n", backtrace);
    LOG(circle::LogLevel::Error, "Libcircle received MPI error, checkpointing.");
  }

  circle::checkpoint();
  exit(EXIT_FAILURE);
}
#pragma GCC diagnostic warning "-Wunused-parameter"

/**
 * Wrapper for pushing an element on the queue
 *
 */
int circle::WorkQueue::enqueue(const std::vector<uint8_t> &element) {
  return circle::internal_queue_push(circle::impl::circle.impl->queue, element);
}

int circle::WorkQueue::enqueue(const std::string &element)
{
  std::vector<uint8_t> content(element.begin(), element.end());
  return enqueue(content);
}

/**
 * Wrapper for popping an element
 */
int circle::WorkQueue::dequeue(std::vector<uint8_t> &element)
{
  return circle::internal_queue_pop(circle::impl::circle.impl->queue, element);
}

int circle::WorkQueue::dequeue(std::string &element)
{
  std::vector<uint8_t> content;
  int result = dequeue(content);
  std::copy(content.begin(), content.end(), element.begin());
  return result;
}

/**
 * Wrapper for getting the local queue size
 */
uint32_t circle::WorkQueue::localQueueSize(void) {
  return (uint32_t)circle::impl::circle.impl->queue->count;
}

/**
 * Call this function to read in libcircle restart files.
 */
int8_t circle::read_restarts(void) {
  return circle::internal_queue_read(circle::impl::circle.impl->queue, circle::global_rank);
}

/**
 * Call this function to read in libcircle restart files.  Each rank
 * writes a file called circle<rank>.txt
 */
int8_t circle::checkpoint(void) {
  return circle::internal_queue_write(circle::impl::circle.impl->queue, circle::global_rank);
}

/**
 * Initializes all variables local to a rank
 */
static void init_local_state(MPI_Comm comm, circle::state_st *local_state) {
  /* get our rank and number of ranks in communicator */
  int rank, size;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  /* set rank and size in state */
  local_state->comm = circle::impl::circle.impl->comm;
  local_state->rank = rank;
  local_state->size = size;

  /* start the termination token on rank 0 */
  local_state->token_is_local = 0;

  if (rank == 0) {
    local_state->token_is_local = 1;
  }

  /* identify ranks for the token ring */
  local_state->token_src = (rank - 1 + size) % size;
  local_state->token_dest = (rank + 1 + size) % size;

  /* initialize token state */
  local_state->token_proc = circle::WHITE;
  local_state->token_buf = circle::BLACK;
  local_state->token_send_req = MPI_REQUEST_NULL;

  /* allocate memory for our offset arrays */
  int32_t offsets = circle::impl::circle.impl->queue->str_count;
  local_state->offsets_count = offsets;
  local_state->offsets_send_buf = (int *)calloc((size_t)offsets, sizeof(int));
  local_state->offsets_recv_buf = (int *)calloc((size_t)offsets, sizeof(int));

  /* allocate array for work request */
  size_t array_elems = (size_t)size;
  local_state->requestors = (int *)malloc(sizeof(int) * array_elems);

  /* randomize the first task we request work from */
  local_state->seed = (unsigned)rank;
  circle::get_next_proc(local_state);

  /* initialize work request state */
  local_state->work_requested = 0;

  /* determine whether we are using tree-based or circle-based
   * termination detection */
  local_state->term_tree_enabled = 0;
  if ((circle::impl::circle.runtimeFlags & circle::RuntimeFlags::TermTree) !=
      circle::RuntimeFlags::None) {
    local_state->term_tree_enabled = 1;
  }

  /* create our collective tree */
  int tree_width = circle::impl::circle.impl->tree_width;
  circle::tree_init(rank, size, tree_width, local_state->comm,
                    &local_state->tree);

  /* init state for progress reduction operations */
  local_state->reduce_enabled = 0;
  double secs = (double)circle::impl::circle.reduce_period;
  if (secs > 0.0) {
    local_state->reduce_enabled = 1;
  }
  local_state->reduce_time_last = MPI_Wtime();
  local_state->reduce_time_interval = secs;
  local_state->reduce_outstanding = 0;

  /* init state for cleanup barrier operations */
  local_state->barrier_started = 0;
  local_state->barrier_up = 0;
  local_state->barrier_replies = 0;

  /* init state for termination allreduce operations */
  local_state->work_outstanding = 0;
  local_state->term_flag = 1;
  local_state->term_up = 0;
  local_state->term_replies = 0;

  /* init state for abort broadcast tree */
  local_state->abort_state = 0;
  local_state->abort_outstanding = 0;

  /* compute number of MPI requets we'll use in abort
   * (parent + num_children)*2 for one isend/irecv each */
  int num_req = local_state->tree.children;
  if (local_state->tree.parent_rank != MPI_PROC_NULL) {
    num_req++;
  }
  num_req *= 2;

  local_state->abort_num_req = num_req;
  local_state->abort_req =
      (MPI_Request *)calloc((size_t)num_req, sizeof(MPI_Request));

  int i;
  for (i = 0; i < num_req; i++) {
    local_state->abort_req[i] = MPI_REQUEST_NULL;
  }

  /* initalize counters */
  local_state->local_objects_processed = 0;
  local_state->local_work_requested = 0;
  local_state->local_no_work_received = 0;

  return;
}

/**
 * Free memory associated with state
 */
static void finalize_local_state(circle::state_st *local_state) {
  circle::tree_free(&local_state->tree);
  circle::free(&local_state->abort_req);
  circle::free(&local_state->offsets_send_buf);
  circle::free(&local_state->offsets_recv_buf);
  circle::free(&local_state->requestors);
  return;
}

/**
 * @brief Function that actually does work, calls user callback.
 *
 * This is the main body of execution.
 *
 * - For every work loop execution, the following happens:
 *     -# Check for work requests from other ranks.
 *     -# If this rank doesn't have work, ask a random rank for work.
 *     -# If this rank has work, call the user callback function.
 *     -# If after requesting work, this rank still doesn't have any,
 *        check for termination conditions.
 */
static void work_loop(circle::state_st *sptr, circle::WorkQueue *q_handle) {
  int cleanup = 0;

  /* Loop until done, we break on normal termination or abort */
  while (1) {
    /* Check for and service work requests */
    circle::workreq_check(circle::impl::circle.impl->queue, sptr, cleanup);

    /* process any incoming work receipt messages */
    circle::workreceipt_check(circle::impl::circle.impl->queue, sptr);

    /* check for incoming abort messages */
    circle::abort_check(sptr, cleanup);

    /* Make progress on any outstanding reduction */
    if (sptr->reduce_enabled) {
      circle::reduce_check(sptr, sptr->local_objects_processed, cleanup);
    }

    /* If I have no work, request work from another rank */
    if (circle::impl::circle.impl->queue->count == 0) {
      circle::request_work(circle::impl::circle.impl->queue, sptr, cleanup);
    }

    /* If I have some work and have not received a signal to
     * abort, process one work item */
    if (circle::impl::circle.impl->queue->count > 0 && !ABORT_FLAG) {
      (*(circle::impl::circle.process_cb))(q_handle);
      sptr->local_objects_processed++;
    }
    /* If I don't have work, or if I received signal to abort,
     * check for termination */
    else {
      /* check whether we have terminated */
      int term_status;
      if (sptr->term_tree_enabled) {
        term_status = circle::check_for_term_allreduce(sptr);
      } else {
        term_status = circle::check_for_term(sptr);
      }

      if (term_status == circle::TERMINATE) {
        /* got the terminate signal, break the loop */
        LOG(circle::LogLevel::Debug, "Received termination signal.");
        break;
      }
    }
  }

  /* We get here either because all procs have completed all work,
   * or we got an ABORT message. */

  /* We need to be sure that all sent messages have been received
   * before returning control to the user, so that if the caller
   * invokes libcirlce again, no messages from this invocation
   * interfere with messages from the next.  Since receivers do not
   * know wether a message is coming to them, a sender records whether
   * it has a message outstanding.  When a receiver gets a message,
   * it acknowledges receipt by sending a reply back to the sender,
   * and at that point, the sender knows its message is no longer
   * outstanding.  All processes continue to receive and reply to
   * incoming messages until all senders have declared that they
   * have no more outstanding messages. */

  /* To get here when using the termination allreduce, we can be sure
   * that there is both no outstanding work transfer message for this
   * process nor any additional termination allreduce messages to
   * cleanup.  This was not immediately obvious, so here is a proof
   * to convince myself:
   *
   * Assumptions (requirements):
   * a) A process only makes progress on the current termination
   *    reduction when its queue is empty or it is in abort state.
   * b) If a process is in abort state, it does not transfer work.
   * c) If a process transferred work at any point before sending
   *    to its parent, it will force a new reduction iteration after
   *    the work has been transferred to the new process by setting
   *    its reduction flag to 0.
   *
   * Question:
   * - Can a process have an outstanding work transfer at the point
   *   it receives 1 from the termination allreduce?
   *
   * Answer: No (why?)
   * In order to send to its parent, this process (X) must have an
   * empty queue or is in the abort state.  If it transferred data
   * before calling the reduction, it would set its flag=0 and
   * force a new reduction iteration.  So to get a 1 from the
   * reduction it must not have transferred data before sending to
   * its parent.  If the process was is abort state, it cannot have
   * transferred work after sending to its parent, so it can only be
   * that its queue must have been empty, then it sent flag=1 to its
   * parent, and then transferred work.
   *
   * For its queue to have been empty and then for it to have
   * transferred work later, it must have received work items from
   * another process (Y).  If that sending process (Y) was from a
   * process yet to contribute its flag to the allreduce, the current
   * iteration would return 0, so it must be from a process that had
   * already contributed a 1.  For that process (Y) to have sent 1 to
   * its own parent, it must have an empty queue or be in the abort
   * state.  If it was in abort state, it could not have transferred
   * work, so it must have had an empty queue, meaning that it must
   * have acquired the work items from another process (Z) and
   * process Z cannot be either process X or Y.
   *
   * We can apply this logic recursively until we rule out all
   * processes in the tree, i.e., no process yet to send to its
   * parent and no process that has already sent a 1 to its parent,
   * and of course if any process had sent 0 to its parent, then
   * the allreduce will not return 1 on that iteration.
   *
   * Thus, there is no need to cleanup work transfer or termination
   * allreduce messages at this point. */

  cleanup = 1;

  /* clear up any MPI messages that may still be outstanding */
  while (1) {
    /* start a non-blocking barrier once we have no outstanding
     * items */
    if (!sptr->work_requested && !sptr->reduce_outstanding &&
        !sptr->abort_outstanding && sptr->token_send_req == MPI_REQUEST_NULL) {
      circle::barrier_start(sptr);
    }

    /* break the loop when the non-blocking barrier completes */
    if (circle::barrier_test(sptr)) {
      break;
    }

    /* send no work message for any work request that comes in */
    circle::workreq_check(circle::impl::circle.impl->queue, sptr, cleanup);

    /* cleanup any outstanding reduction */
    if (sptr->reduce_enabled) {
      circle::reduce_check(sptr, sptr->local_objects_processed, cleanup);
    }

    /* receive any incoming work reply messages */
    circle::request_work(circle::impl::circle.impl->queue, sptr, cleanup);

    /* drain any outstanding abort messages */
    circle::abort_check(sptr, cleanup);

    /* if we're using circle-based token passing, drain any
     * messages still outstanding */
    if (!sptr->term_tree_enabled) {
      /* check for and receive any incoming token */
      circle::token_check(sptr);

      /* if we have an outstanding token, check whether it's been received */
      if (sptr->token_send_req != MPI_REQUEST_NULL) {
        int flag;
        MPI_Status status;
        MPI_Test(&sptr->token_send_req, &flag, &status);
      }
    }
  }

  /* execute final, synchronous reduction if enabled, this ensures
   * that we fire at least one reduce and one with the final result */
  if (sptr->reduce_enabled) {
    circle::reduce_sync(sptr, sptr->local_objects_processed);
  }

  /* if any process is in the abort state,
   * set all to be in the abort state */
  circle::abort_reduce(sptr);

  return;
}

/**
 * @brief Sets up libcircle, calls work loop function
 *
 * - Main worker function. This function:
 *     -# Initializes MPI
 *     -# Initializes internal libcircle data structures
 *     -# Calls libcircle's main work loop function.
 *     -# Checkpoints if circle::abort has been called by a rank.
 */
int8_t circle::worker() {
  /* Holds all worker state */
  circle::state_st local_state;
  circle::state_st *sptr = &local_state;

  /* get MPI communicator */
  MPI_Comm comm = circle::impl::circle.impl->comm;

  /* get our rank and the size of the communicator */
  int rank, size;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  /* initialize all local state variables */
  init_local_state(comm, sptr);

  /* setup an MPI error handler */
  MPI_Errhandler circle_err;
  MPI_Comm_create_errhandler(MPI_error_handler, &circle_err);
  MPI_Comm_set_errhandler(comm, circle_err);

  /* print settings of some runtime tunables */
  if ((circle::impl::circle.runtimeFlags & circle::RuntimeFlags::SplitEqual) !=
      circle::RuntimeFlags::None) {
    LOG(circle::LogLevel::Debug, "Using equalized load splitting.");
  }

  if ((circle::impl::circle.runtimeFlags & circle::RuntimeFlags::SplitRandom) !=
      circle::RuntimeFlags::None) {
    LOG(circle::LogLevel::Debug, "Using randomized load splitting.");
  }

  if ((circle::impl::circle.runtimeFlags & circle::RuntimeFlags::CreateGlobal) !=
      circle::RuntimeFlags::None) {
    LOG(circle::LogLevel::Debug, "Create callback enabled on all ranks.");
  } else {
    LOG(circle::LogLevel::Debug, "Create callback enabled on rank 0 only.");
  }

  if ((circle::impl::circle.runtimeFlags & circle::RuntimeFlags::TermTree) !=
      circle::RuntimeFlags::None) {
    LOG(circle::LogLevel::Debug, "Using tree termination detection.");
  } else {
    LOG(circle::LogLevel::Debug, "Using circle termination detection.");
  }

  LOG(circle::LogLevel::Debug, "Tree width: %d", circle::impl::circle.impl->tree_width);
  LOG(circle::LogLevel::Debug, "Reduce period (secs): %d", circle::impl::circle.reduce_period);

  /**********************************
   * this is where the heavy lifting is done
   **********************************/

  /* add initial work to queues by calling create_cb,
   * only invoke on master unless CREATE_GLOBAL is set */
  if (rank == 0 || (circle::impl::circle.runtimeFlags & circle::RuntimeFlags::CreateGlobal) !=
                       circle::RuntimeFlags::None) {
    (*(circle::impl::circle.create_cb))(&queue_handle);
  }

  /* work until we get a terminate message */
  work_loop(sptr, &queue_handle);

  /* we may have dropped out early from an abort signal,
   * in which case we should checkpoint here */
  if (ABORT_FLAG) {
    circle::checkpoint();
  }

  /**********************************
   * end work
   **********************************/

  /* optionally print summary info */
  if (circle::debug_level >= circle::LogLevel::Info) {
    /* allocate memory for summary data */
    size_t array_elems = (size_t)size;
    uint32_t *total_objects_processed_array =
        (uint32_t *)calloc(array_elems, sizeof(uint32_t));
    uint32_t *total_work_requests_array =
        (uint32_t *)calloc(array_elems, sizeof(uint32_t));
    uint32_t *total_no_work_received_array =
        (uint32_t *)calloc(array_elems, sizeof(uint32_t));

    /* gather and reduce summary info */
    MPI_Gather(&sptr->local_objects_processed, 1, MPI_INT,
               &total_objects_processed_array[0], 1, MPI_INT, 0, comm);
    MPI_Gather(&sptr->local_work_requested, 1, MPI_INT,
               &total_work_requests_array[0], 1, MPI_INT, 0, comm);
    MPI_Gather(&sptr->local_no_work_received, 1, MPI_INT,
               &total_no_work_received_array[0], 1, MPI_INT, 0, comm);

    int total_objects_processed = 0;
    MPI_Reduce(&sptr->local_objects_processed, &total_objects_processed, 1,
               MPI_INT, MPI_SUM, 0, comm);

    /* print summary from rank 0 */
    if (rank == 0) {
      int i;
      for (i = 0; i < size; i++) {
        LOG(circle::LogLevel::Info, "Rank %d\tObjects Processed %d\t%0.3lf%%", i,
            total_objects_processed_array[i],
            (double)total_objects_processed_array[i] /
                (double)total_objects_processed * 100.0);
        LOG(circle::LogLevel::Info, "Rank %d\tWork requests: %d", i,
            total_work_requests_array[i]);
        LOG(circle::LogLevel::Info, "Rank %d\tNo work replies: %d", i,
            total_no_work_received_array[i]);
      }

      LOG(circle::LogLevel::Info, "Total Objects Processed: %d",
          total_objects_processed);
    }

    /* free memory */
    circle::free(&total_no_work_received_array);
    circle::free(&total_work_requests_array);
    circle::free(&total_objects_processed_array);
  }

  /* restore original error handler and free our custom one */
  MPI_Comm_set_errhandler(comm, MPI_ERRORS_ARE_FATAL);
  MPI_Errhandler_free(&circle_err);

  /* free memory */
  finalize_local_state(sptr);

  return 0;
}

/* EOF */
