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
namespace internal {

int8_t ABORT_FLAG;

} // namespace impl
} // namespace circle

using namespace circle;
using namespace circle::internal;

/*
 * Define as described in gethostent(3) for backwards compatibility.
 */
#ifndef h_addr
#define h_addr h_addr_list[0]
#endif /* h_addr */

/**
 * @brief Function to be called in the event of an MPI error.
 *
 * This function get registered with MPI to be called
 * in the event of an MPI Error.  It attempts
 * to checkpoint.
 */
#pragma GCC diagnostic ignored "-Wunused-parameter"
static void MPI_error_handler(MPI_Comm *comm, int *err, ...) {
  const char *bt = backtrace(1);
#if 0
  // TODO Attach circle to communicator with MPI_Comm_create_keyval
  char name[MPI_MAX_OBJECT_NAME];
  int namelen;
  MPI_Comm_get_name(*comm, name, &namelen);

  if (*err == CIRCLE_MPI_ERROR) {
    LOG(LogLevel::Error, "Libcircle received abort signal, checkpointing.");
  } else {
    char error[MPI_MAX_ERROR_STRING];
    int error_len = 0;
    MPI_Error_string(*err, error, &error_len);
    LOG(LogLevel::Error, "MPI Error in Comm [%s]: %s", name, error);
    LOG(LogLevel::Error, "Backtrace:\n%s\n", bt);
    LOG(LogLevel::Error, "Libcircle received MPI error, checkpointing.");
  }
  
  checkpoint();
#endif
  abort();
}
#pragma GCC diagnostic warning "-Wunused-parameter"

#if 0
// TODO class Handle or WorkQueue
/**
 * Wrapper for pushing an element on the queue
 *
 */
int Queue::enqueue(const std::vector<uint8_t> &element) {
  return push(parent->impl->queue, element);
}

int Queue::enqueue(const std::string &element)
{
  std::vector<uint8_t> content(element.begin(), element.end());
  return enqueue(content);
}

/**
 * Wrapper for popping an element
 */
int Queue::dequeue(std::vector<uint8_t> &element)
{
  return pop(parent->impl->queue, element);
}

int Queue::dequeue(std::string &element)
{
  std::vector<uint8_t> content;
  int result = dequeue(content);
  std::copy(content.begin(), content.end(), element.begin());
  return result;
}

/**
 * Wrapper for getting the local queue size
 */
uint32_t Queue::localQueueSize(void) {
  return (uint32_t)count;
}
#endif

/**
 * Call this function to read in libcircle restart files.
 */
int8_t Worker::readRestarts() {
  return parent->impl->queue->read(parent->getRank());
}

/**
 * Call this function to read in libcircle restart files.  Each rank
 * writes a file called circle<rank>.txt
 */
int8_t Worker::checkpoint() {
  return parent->impl->queue->write(parent->getRank());
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
void Worker::mainLoop(State *sptr) {
  int cleanup = 0;

  /* Loop until done, we break on normal termination or abort */
  while (1) {
    /* Check for and service work requests */
    sptr->workreqCheck(parent->impl->queue, cleanup);

    /* process any incoming work receipt messages */
    sptr->workreceiptCheck(parent->impl->queue);

    /* check for incoming abort messages */
    sptr->abortCheck(cleanup);

    /* Make progress on any outstanding reduction */
    if (sptr->reduce_enabled) {
      sptr->reduceCheck(sptr->local_objects_processed, cleanup);
    }

    /* If I have no work, request work from another rank */
    if (parent->impl->queue->count == 0) {
      sptr->requestWork(parent->impl->queue, cleanup);
    }

    /* If I have some work and have not received a signal to
     * abort, process one work item */
    if (parent->impl->queue->count > 0 && !ABORT_FLAG) {
      (*(parent->process_cb))(parent);
      sptr->local_objects_processed++;
    }
    /* If I don't have work, or if I received signal to abort,
     * check for termination */
    else {
      /* check whether we have terminated */
      int term_status;
      if (sptr->term_tree_enabled) {
        term_status = sptr->checkForTermAllReduce();
      } else {
        term_status = sptr->checkForTerm();
      }

      if (term_status == TERMINATE) {
        /* got the terminate signal, break the loop */
        LOG(LogLevel::Debug, "Received termination signal.");
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
      sptr->barrierStart();
    }

    /* break the loop when the non-blocking barrier completes */
    if (sptr->barrierTest()) {
      break;
    }

    /* send no work message for any work request that comes in */
    sptr->workreqCheck(parent->impl->queue, cleanup);

    /* cleanup any outstanding reduction */
    if (sptr->reduce_enabled) {
      sptr->reduceCheck(sptr->local_objects_processed, cleanup);
    }

    /* receive any incoming work reply messages */
    sptr->requestWork(parent->impl->queue, cleanup);

    /* drain any outstanding abort messages */
    sptr->abortCheck(cleanup);

    /* if we're using circle-based token passing, drain any
     * messages still outstanding */
    if (!sptr->term_tree_enabled) {
      /* check for and receive any incoming token */
      sptr->tokenCheck();

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
    sptr->reduceSync(sptr->local_objects_processed);
  }

  /* if any process is in the abort state,
   * set all to be in the abort state */
  sptr->abortReduce();

  return;
}

Worker::Worker(Circle* parent_) : parent(parent_) { }

/**
 * @brief Sets up libcircle, calls work loop function
 *
 * - Main worker function. This function:
 *     -# Initializes MPI
 *     -# Initializes internal libcircle data structures
 *     -# Calls libcircle's main work loop function.
 *     -# Checkpoints if abort has been called by a rank.
 */
int Worker::execute() {
  /* initialize all local state variables */
  State state(parent);

  /* Holds all worker state */
  State *sptr = &state;

  /* get MPI communicator */
  MPI_Comm& comm = parent->impl->comm;

  /* get our rank and the size of the communicator */
  int rank, size;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  /* setup an MPI error handler */
  MPI_Errhandler circle_err;
  MPI_Comm_create_errhandler(MPI_error_handler, &circle_err);
  MPI_Comm_set_errhandler(comm, circle_err);

  /* print settings of some runtime tunables */
  if ((parent->impl->runtimeFlags & RuntimeFlags::SplitEqual) !=
      RuntimeFlags::None) {
    LOG(LogLevel::Debug, "Using equalized load splitting.");
  }

  if ((parent->impl->runtimeFlags & RuntimeFlags::SplitRandom) !=
      RuntimeFlags::None) {
    LOG(LogLevel::Debug, "Using randomized load splitting.");
  }

  if ((parent->impl->runtimeFlags & RuntimeFlags::CreateGlobal) !=
      RuntimeFlags::None) {
    LOG(LogLevel::Debug, "Create callback enabled on all ranks.");
  } else {
    LOG(LogLevel::Debug, "Create callback enabled on rank 0 only.");
  }

  if ((parent->impl->runtimeFlags & RuntimeFlags::TermTree) !=
      RuntimeFlags::None) {
    LOG(LogLevel::Debug, "Using tree termination detection.");
  } else {
    LOG(LogLevel::Debug, "Using circle termination detection.");
  }

  LOG(LogLevel::Debug, "Tree width: %d", parent->impl->tree_width);
  LOG(LogLevel::Debug, "Reduce period (secs): %d", parent->impl->reduce_period);

  /**********************************
   * this is where the heavy lifting is done
   **********************************/

  /* add initial work to queues by calling create_cb,
   * only invoke on master unless CREATE_GLOBAL is set */
  if (rank == 0 || (parent->impl->runtimeFlags & RuntimeFlags::CreateGlobal) !=
                       RuntimeFlags::None) {
    (*(parent->create_cb))(parent);
  }

  /* work until we get a terminate message */
  mainLoop(sptr);

  /* we may have dropped out early from an abort signal,
   * in which case we should checkpoint here */
  if (ABORT_FLAG) {
    checkpoint();
  }

  /**********************************
   * end work
   **********************************/

  /* optionally print summary info */
  if (parent->impl->logLevel >= LogLevel::Info) {
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
        LOG(LogLevel::Info, "Rank %d\tObjects Processed %d\t%0.3lf%%", i,
            total_objects_processed_array[i],
            (double)total_objects_processed_array[i] /
                (double)total_objects_processed * 100.0);
        LOG(LogLevel::Info, "Rank %d\tWork requests: %d", i,
            total_work_requests_array[i]);
        LOG(LogLevel::Info, "Rank %d\tNo work replies: %d", i,
            total_no_work_received_array[i]);
      }

      LOG(LogLevel::Info, "Total Objects Processed: %d",
          total_objects_processed);
    }

    /* free memory */
    free(&total_no_work_received_array);
    free(&total_work_requests_array);
    free(&total_objects_processed_array);
  }

  /* restore original error handler and free our custom one */
  MPI_Comm_set_errhandler(comm, MPI_ERRORS_ARE_FATAL);
  MPI_Errhandler_free(&circle_err);

  return 0;
}
