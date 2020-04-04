#include <mpi.h>
#include <stdint.h>
#include <stdlib.h>

#include "lanl_circle.hpp"
#include "circle_impl.hpp"
#include "log.hpp"
#include "queue.hpp"
#include "token.hpp"
#include "worker.hpp"

namespace circle {
namespace internal {

extern int8_t ABORT_FLAG;

} // namespace impl
} // namespace circle

using namespace circle;
using namespace circle::internal;

/**
 * Initializes all variables local to a rank
 */
State::State(Circle* parent_) : parent(parent_), comm(parent_->impl->comm) {

  /* get our rank and number of ranks in communicator */
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  /* start the termination token on rank 0 */
  token_is_local = 0;

  if (rank == 0) {
    token_is_local = 1;
  }

  /* identify ranks for the token ring */
  token_src = (rank - 1 + size) % size;
  token_dest = (rank + 1 + size) % size;

  /* initialize token state */
  token_proc = circle::internal::WHITE;
  token_buf = circle::internal::BLACK;
  token_send_req = MPI_REQUEST_NULL;

  /* allocate memory for our offset arrays */
  int32_t offsets = parent->impl->queue.strings.size();
  offsets_count = offsets;
  offsets_send_buf = (int *)calloc((size_t)offsets, sizeof(int));
  offsets_recv_buf = (int *)calloc((size_t)offsets, sizeof(int));

  /* allocate array for work request */
  size_t array_elems = (size_t)size;
  requestors = (int *)malloc(sizeof(int) * array_elems);

  /* randomize the first task we request work from */
  seed = (unsigned)rank;
  getNextProc();

  /* initialize work request state */
  work_requested = 0;

  /* determine whether we are using tree-based or circle-based
   * termination detection */
  term_tree_enabled = 0;
  if ((parent->runtimeFlags & circle::RuntimeFlags::TermTree) !=
      circle::RuntimeFlags::None) {
    term_tree_enabled = 1;
  }

  /* create our collective tree */
  int tree_width = parent->impl->tree_width;
  tree = new TreeState(parent, rank, size, tree_width);

  /* init state for progress reduction operations */
  reduce_enabled = 0;
  double secs = (double)parent->reduce_period;
  if (secs > 0.0) {
    reduce_enabled = 1;
  }
  reduce_time_last = MPI_Wtime();
  reduce_time_interval = secs;
  reduce_outstanding = 0;

  /* init state for cleanup barrier operations */
  barrier_started = 0;
  barrier_up = 0;
  barrier_replies = 0;

  /* init state for termination allreduce operations */
  work_outstanding = 0;
  term_flag = 1;
  term_up = 0;
  term_replies = 0;

  /* init state for abort broadcast tree */
  abort_state = 0;
  abort_outstanding = 0;

  /* compute number of MPI requets we'll use in abort
   * (parent + num_children)*2 for one isend/irecv each */
  int num_req = tree->children;
  if (tree->parent_rank != MPI_PROC_NULL) {
    num_req++;
  }
  num_req *= 2;

  abort_num_req = num_req;
  abort_req = (MPI_Request *)calloc((size_t)num_req, sizeof(MPI_Request));

  int i;
  for (i = 0; i < num_req; i++) {
    abort_req[i] = MPI_REQUEST_NULL;
  }

  /* initalize counters */
  local_objects_processed = 0;
  local_work_requested = 0;
  local_no_work_received = 0;
}

/**
 * Free memory associated with state
 */
State::~State() {
  delete tree;

  circle::internal::free(&abort_req);
  circle::internal::free(&offsets_send_buf);
  circle::internal::free(&offsets_recv_buf);
  circle::internal::free(&requestors);
}

/* initiate and progress a reduce operation at specified interval,
 * ensures progress of reduction in background, stops reduction if
 * cleanup == 1 */
void State::reduceCheck(int count, int cleanup) {
  int i;
  int flag;
  MPI_Status status;

  /* get info about tree */
  int parent_rank = tree->parent_rank;
  int children = tree->children;
  int *child_ranks = tree->child_ranks;

  /* if we have an outstanding reduce, check messages from children,
   * otherwise, check whether we should start a new reduce */
  if (reduce_outstanding) {
    /* got a reduce outstanding, check messages from our children */
    for (i = 0; i < children; i++) {
      /* pick a child */
      int child = child_ranks[i];

      /* check whether this child has sent us a reduce message */
      MPI_Iprobe(child, CIRCLE_TAG_REDUCE, comm, &flag, &status);

      /* if we got a message, receive and reduce it */
      if (flag) {
        /* TODO: change me to uint64_t at some point */

        /* receive message form child, first int contains
         * flag indicating whether message is valid,
         * second int is number of completed libcircle work
         * elements, third int is number of bytes of user data */
        long long int recvbuf[3];
        MPI_Recv(recvbuf, 3, MPI_LONG_LONG, child, CIRCLE_TAG_REDUCE,
                 comm, &status);

        /* increment the number of replies */
        reduce_replies++;

        /* check whether child is sending valid data */
        if (recvbuf[0] == MSG_INVALID) {
          /* child's data is invalid,
           * set our result to invalid */
          reduce_buf[0] = MSG_INVALID;
          continue;
        }

        /* otherwise, we got a real message, combine child's
         * data with our buffer (this step won't hurt even
         * if our buffer has invalid data) */
        reduce_buf[1] += recvbuf[1];

        /* get incoming user data if we have any */
        void *inbuf = NULL;
        size_t insize = (size_t)recvbuf[2];

        if (insize > 0) {
          /* allocate space to hold data */
          inbuf = malloc(insize);

          if (inbuf == NULL) {
          }

          /* receive data */
          int bytes = (int)recvbuf[2];
          MPI_Recv(inbuf, bytes, MPI_BYTE, child, CIRCLE_TAG_REDUCE,
                   comm, &status);
        }

        /* if we have valid data, invoke user's callback to
         * reduce user data */
        if (reduce_buf[0] == MSG_VALID) {
          if (parent->reduce_op_cb != NULL) {
            void *currbuf = parent->reduce_buf;
            size_t currsize = parent->reduce_buf_size;
            (*(parent->reduce_op_cb))(parent, currbuf, currsize, inbuf, insize);
          }
        }

        /* free temporary buffer holding incoming user data */
        free(&inbuf);
      }
    }

    /* check whether we've gotten replies from all children */
    if (reduce_replies == children) {
      /* all children have replied, add our own content to reduce buffer */
      reduce_buf[1] += (long long int)count;

      /* send message to parent if we have one */
      if (parent_rank != MPI_PROC_NULL) {
        /* get size of user data */
        int bytes = (int)parent->reduce_buf_size;
        reduce_buf[2] = (long long int)bytes;

        /* send partial result to parent */
        MPI_Send(reduce_buf, 3, MPI_LONG_LONG, parent_rank,
                 CIRCLE_TAG_REDUCE, comm);

        /* also send along user data if any, and if it is valid */
        if (bytes > 0 && reduce_buf[0] == MSG_VALID) {
          void *currbuf = parent->reduce_buf;
          MPI_Send(currbuf, bytes, MPI_BYTE, parent_rank,
                   CIRCLE_TAG_REDUCE, comm);
        }
      } else {
        /* we're the root, print the results if we have valid data */
        if (reduce_buf[0] == MSG_VALID) {
          LOG(LogLevel::Info, "Objects processed: %lld ...",
              reduce_buf[1]);

          /* invoke callback on root to deliver final result */
          if (parent->reduce_fini_cb != NULL) {
            void *resultbuf = parent->reduce_buf;
            size_t resultsize = parent->reduce_buf_size;
            (*(parent->reduce_fini_cb))(parent, resultbuf, resultsize);
          }
        }
      }

      /* disable flag that indicates we have an outstanding reduce */
      reduce_outstanding = 0;
    }
  } else {
    /* we don't have an outstanding reduction, determine whether a
     * new reduce should be started, only bother checking if we
     * think it's about time or if we're in cleanup mode */
    int start_reduce = 0;
    double time_now = MPI_Wtime();
    double time_next = reduce_time_last + reduce_time_interval;

    if (time_now >= time_next || cleanup) {
      /* time has expired, new reduce should be started */
      if (parent_rank == MPI_PROC_NULL) {
        /* we're the root, kick it off */
        start_reduce = 1;
      } else {
        /* we're not the root, check whether parent sent us a message */
        MPI_Iprobe(parent_rank, CIRCLE_TAG_REDUCE, comm, &flag,
                   &status);

        /* kick off reduce if message came in */
        if (flag) {
          /* receive message from parent and set flag to start reduce */
          MPI_Recv(NULL, 0, MPI_BYTE, parent_rank, CIRCLE_TAG_REDUCE,
                   comm, &status);
          start_reduce = 1;
        }
      }
    }

    /* it's critical that we don't start a reduce if we're in cleanup phase,
     * because we may have already started the non-blocking barrier,
     * just send an invalid message back to our parent */
    if (start_reduce && cleanup) {
      /* avoid starting a reduce below */
      start_reduce = 0;

      /* set message to invalid data, and send it back to parent
       * if we have one */
      if (parent_rank != MPI_PROC_NULL) {
        reduce_buf[0] = MSG_INVALID;
        MPI_Send(reduce_buf, 3, MPI_LONG_LONG, parent_rank,
                 CIRCLE_TAG_REDUCE, comm);
      }
    }

    /* kick off a reduce if it's time */
    if (start_reduce) {
      /* set flag to indicate we have a reduce outstanding
       * and initialize state for a fresh reduction */
      reduce_time_last = time_now;
      reduce_outstanding = 1;
      reduce_replies = 0;
      reduce_buf[0] = MSG_VALID;
      reduce_buf[1] = 0; /* set total to 0 */
      reduce_buf[2] = 0; /* initialize byte count */

      /* invoke callback to get input data,
       * it will be stored in circle after user
       * calls reduce which should be done in callback */
      if (parent->reduce_init_cb != NULL) {
        (*(parent->reduce_init_cb))(parent);
      }

      /* send message to each child */
      for (i = 0; i < children; i++) {
        int child = child_ranks[i];
        MPI_Send(NULL, 0, MPI_BYTE, child, CIRCLE_TAG_REDUCE, comm);
      }
    }
  }

  return;
}

/* executes synchronous reduction with user reduce callbacks */
void State::reduceSync(int count) {
  int i;
  MPI_Status status;

  /* get info about tree */
  int parent_rank = tree->parent_rank;
  int children = tree->children;
  int *child_ranks = tree->child_ranks;

  /* initialize state for a fresh reduction */
  reduce_buf[0] = MSG_VALID;
  reduce_buf[1] = (long long int)count;
  reduce_buf[2] = 0; /* initialize byte count */

  /* invoke callback to get input data,
   * it will be stored in circle after user
   * calls reduce which should be done in callback */
  if (parent->reduce_init_cb != NULL) {
    (*(parent->reduce_init_cb))(parent);
  }

  /* wait for messages from our children */
  for (i = 0; i < children; i++) {
    /* pick a child */
    int child = child_ranks[i];

    /* receive message form child, first int contains
     * flag indicating whether message is valid,
     * second int is number of completed libcircle work
     * elements, third int is number of bytes of user data */
    long long int recvbuf[3];
    MPI_Recv(recvbuf, 3, MPI_LONG_LONG, child, CIRCLE_TAG_REDUCE, comm,
             &status);

    /* combine child's count with ours */
    reduce_buf[1] += recvbuf[1];

    /* get incoming user data if we have any */
    void *inbuf = NULL;
    size_t insize = (size_t)recvbuf[2];

    if (insize > 0) {
      /* allocate space to hold data */
      inbuf = malloc(insize);

      if (inbuf == NULL) {
      }

      /* receive data */
      int bytes = (int)recvbuf[2];
      MPI_Recv(inbuf, bytes, MPI_BYTE, child, CIRCLE_TAG_REDUCE, comm,
               &status);
    }

    /* invoke user's callback to reduce user data */
    if (parent->reduce_op_cb != NULL) {
      void *currbuf = parent->reduce_buf;
      size_t currsize = parent->reduce_buf_size;
      (*(parent->reduce_op_cb))(parent, currbuf, currsize, inbuf, insize);
    }

    /* free temporary buffer holding incoming user data */
    free(&inbuf);
  }

  /* send message to parent if we have one */
  if (parent_rank != MPI_PROC_NULL) {
    /* get size of user data */
    int bytes = (int)parent->reduce_buf_size;
    reduce_buf[2] = (long long int)bytes;

    /* send partial result to parent */
    MPI_Send(reduce_buf, 3, MPI_LONG_LONG, parent_rank,
             CIRCLE_TAG_REDUCE, comm);

    /* also send along user data if any */
    if (bytes > 0) {
      void *currbuf = parent->reduce_buf;
      MPI_Send(currbuf, bytes, MPI_BYTE, parent_rank, CIRCLE_TAG_REDUCE,
               comm);
    }
  } else {
    /* we're the root, print the results if we have valid data */
    LOG(LogLevel::Info, "Objects processed: %lld (done)", reduce_buf[1]);

    /* invoke callback on root to deliver final result */
    if (parent->reduce_fini_cb != NULL) {
      void *resultbuf = parent->reduce_buf;
      size_t resultsize = parent->reduce_buf_size;
      (*(parent->reduce_fini_cb))(parent, resultbuf, resultsize);
    }
  }

  return;
}

/* marks our state as ready for the barrier */
void State::barrierStart() { barrier_started = 1; }

/* process a barrier message */
int State::barrierTest() {
  int flag;
  MPI_Status status;

  /* if we haven't started the barrier, it's not complete */
  if (!barrier_started) {
    return 0;
  }

  /* get info about tree */
  int parent_rank = tree->parent_rank;
  int children = tree->children;
  int *child_ranks = tree->child_ranks;

  /* check whether we have received message from all children (if any) */
  if (barrier_replies < children) {
    /* still waiting on barrier messages from our children */
    MPI_Iprobe(MPI_ANY_SOURCE, CIRCLE_TAG_BARRIER, comm, &flag,
               &status);

    /* if we got a message increase our count */
    if (flag) {
      /* get rank of child */
      int child = status.MPI_SOURCE;

      /* receive message from that child */
      MPI_Recv(NULL, 0, MPI_BYTE, child, CIRCLE_TAG_BARRIER, comm,
               &status);

      /* increase count */
      barrier_replies++;
    }
  }

  /* if we have not sent a message to our parent, and we have
   * received a message from all of our children (or we have
   * no children), send a message to our parent */
  if (!barrier_up && barrier_replies == children) {
    /* send a message to our parent if we have one */
    if (parent_rank != MPI_PROC_NULL) {
      MPI_Send(NULL, 0, MPI_BYTE, parent_rank, CIRCLE_TAG_BARRIER,
               comm);
    }

    /* transition to state where we're waiting for parent
     * to notify us that the barrier is complete */
    barrier_up = 1;
  }

  /* wait for message to come back down from parent to mark end
   * of barrier */
  int complete = 0;

  if (barrier_up) {
    if (parent_rank != MPI_PROC_NULL) {
      /* check for message from parent */
      MPI_Iprobe(parent_rank, CIRCLE_TAG_BARRIER, comm, &flag, &status);

      if (flag) {
        /* got a message, receive message */
        MPI_Recv(NULL, 0, MPI_BYTE, parent_rank, CIRCLE_TAG_BARRIER,
                 comm, &status);

        /* mark barrier as complete */
        complete = 1;
      }
    } else {
      /* if we have no parent, we're the root, so mark
       * barrier as complete */
      complete = 1;
    }
  }

  /* if barrier is complete, send messages to children (if any)
   * and return true */
  if (complete) {
    int i;

    for (i = 0; i < children; i++) {
      /* get rank of child */
      int child = child_ranks[i];

      /* send child a message */
      MPI_Send(NULL, 0, MPI_BYTE, child, CIRCLE_TAG_BARRIER, comm);
    }

    /* reset state for another barrier */
    barrier_started = 0;
    barrier_up = 0;
    barrier_replies = 0;

    /* return that barrier has completed */
    return 1;
  }

  /* barrier is still not complete */
  return 0;
}

/* test whether we have terminated via allreduce.
 *
 * In this algorithm, a non-blocking allreduce
 * is used to determine whether all procs have terminated.
 * There is some complication in dealing with work that
 * may be in flight to a process that otherwise thought
 * it was done when it last contributed its partial result
 * to the termination reduction.
 *
 * This function is only called when a process has either
 * exhausted its local work queue or after it has
 * received an abort message, so the reduction only
 * makes progress when a process is locally done
 * with its work.
 *
 * An integer flag is reduced using an AND operation to
 * determine whether any process has set the flag to 0.
 * Any process can force another termination reduction
 * to be executed by setting its flag to 0, which is done
 * if a process has transferred work to another process.
 * When the final reduction flag is 1, then all processes
 * have terminated.
 *
 * state: waiting for children
 *   (term_replies < children) && (term_up == 0)
 * A process waits until it has received reduction messages
 * from all of its children.  It ANDs the flags from its
 * children with its own flag.  Upon receiving messages
 * from all children, it forwards the partial result to its parent
 * and sets the term_up flag to 1 to remember that it sent
 * to its parent.
 *
 * state: waiting for parent
 *   (term_replies == children) && (term_up == 1)
 * A process waits for its parent.  If the process is the
 * root of the tree or it has received a message from its
 * parent, it forwards the final reduction result to its
 * children, and it resets its state tracking flags:
 *   term_up = 0
 *   term_replies = 0
 *
 * If the result of the reduction is 1, all procs
 * have completed.
 *
 * One complication: a process with an empty queue will
 * be simultaneously progressing the termination reduction
 * while randomly asking other procs for work.  If a process
 * sends work to this process, we cannot allow the process that
 * sent the work to also declare that it is done until the
 * transferred work has been accounted for on the requesting
 * process.  Otherwise, we could terminate without having
 * actually done the work that was transferred.
 *
 * To deal with this, all work transfers must be acknowledged
 * before the sender can assume that it itself is done.  A process
 * sending work to another process records the number of outstanding
 * work transfer messages it has sent.  Upon receiving work,
 * a requesting process sends a work receipt message back to
 * the sender.  Upon receiving a work receipt, the process that
 * sent the work can decrement its count of outstanding work transfer
 * messages.  Any process that has a non-zero work transfer count
 * will not progress the termination reduction up the tree until
 * its count hits zero.  Additionally, upon receiving a work receipt,
 * a process forces another iteration of the termination reduction by
 * setting its term_flag=0 before sending to its parent.  This
 * ensures that the process that received the work participates
 * in the reduction again after having accounting for the work items
 * it just received, since it may have already declared itself done
 * in the current reduction iteration. */
int State::checkForTermAllReduce() {
  int flag;
  MPI_Status status;

  /* get info about tree */
  int parent_rank = tree->parent_rank;
  int children = tree->children;
  int *child_ranks = tree->child_ranks;

  /* check whether we have received message from all children (if any) */
  while (term_replies < children) {
    /* still waiting on input messages from our children,
     * probe to see if we got a message from a child */
    MPI_Iprobe(MPI_ANY_SOURCE, CIRCLE_TAG_TERM, comm, &flag, &status);

    /* break out if there is no message from children */
    if (!flag) {
      break;
    }

    /* got a message, get rank of child */
    int child = status.MPI_SOURCE;

    /* receive message from that child */
    int child_flag;
    MPI_Recv(&child_flag, 1, MPI_INT, child, CIRCLE_TAG_TERM, comm,
             &status);

    /* AND child's flag value with ours */
    term_flag &= child_flag;

    /* increase count */
    term_replies++;
  }

  /* do not allow this allreduce to make progress while
   * we have outstanding work transfer messages, we know
   * the remote process is accounting for that work after
   * it has been acknowledged, we'll also force a fresh
   * allreduce upon any acknowledgement */
  if (work_outstanding > 0) {
    return WHITE;
  }

  /* this will hold result of allreduce */
  int term_flag = 0;

  /* if we have not sent a message to our parent, and we have
   * received a message from all of our children (or we have
   * no children), send a message to our parent */
  if (!term_up && term_replies == children) {
    /* send a message to our parent if we have one */
    if (parent_rank != MPI_PROC_NULL) {
      MPI_Send(&term_flag, 1, MPI_INT, parent_rank, CIRCLE_TAG_TERM,
               comm);
    } else {
      /* we are root, capture result of allreduce */
      term_flag = term_flag;
    }

    /* reset our flag for next iteration */
    term_flag = 1;

    /* transition to state where we're waiting for parent
     * to send us result */
    term_up = 1;
  }

  /* wait for message to come back down from parent to mark end
   * of allreduce */
  int complete = 0;

  /* if we have sent to our parent, check whether our parent
   * has sent the result back down */
  if (term_up) {
    if (parent_rank != MPI_PROC_NULL) {
      /* check for message from parent */
      MPI_Iprobe(parent_rank, CIRCLE_TAG_TERM, comm, &flag, &status);

      if (flag) {
        /* got a message, receive message */
        MPI_Recv(&term_flag, 1, MPI_INT, parent_rank, CIRCLE_TAG_TERM,
                 comm, &status);

        /* mark allreduce as complete */
        complete = 1;
      }
    } else {
      /* if we have no parent, we're the root, so mark
       * allreduce as complete */
      complete = 1;
    }
  }

  /* if allreduce is complete, send messages to children (if any)
   * and return true */
  if (complete) {
    int i;
    for (i = 0; i < children; i++) {
      /* get rank of child */
      int child = child_ranks[i];

      /* send child a message */
      MPI_Send(&term_flag, 1, MPI_INT, child, CIRCLE_TAG_TERM, comm);
    }

    /* reset state for another allreduce */
    term_up = 0;
    term_replies = 0;
  }

  /* if we have result of allreduce, determine
   * whether we have terminated */
  if (complete && term_flag) {
    return TERMINATE;
  }
  return WHITE;
}

/* execute an allreduce to determine whether any rank has entered
 * the abort state, and if so, set all ranks to be in abort state */
void State::abortReduce() {
  MPI_Status status;

  /* get info about tree */
  int parent_rank = tree->parent_rank;
  int children = tree->children;
  int *child_ranks = tree->child_ranks;

  /* initialize flag to our abort state */
  int flag = (int)ABORT_FLAG;

  /* reduce messages from children if any */
  int i;
  for (i = 0; i < children; i++) {
    /* get rank of child */
    int child = child_ranks[i];

    /* receive message from child */
    int child_flag;
    MPI_Recv(&child_flag, 1, MPI_INT, child, CIRCLE_TAG_ABORT_REDUCE,
             comm, &status);

    /* OR child's flag value with ours */
    flag |= child_flag;
  }

  /* send a message to our parent and wait on reply if we have one */
  if (parent_rank != MPI_PROC_NULL) {
    /* send partial result to parent */
    MPI_Send(&flag, 1, MPI_INT, parent_rank, CIRCLE_TAG_ABORT_REDUCE,
             comm);

    /* wait for final result from parent */
    MPI_Recv(&flag, 1, MPI_INT, parent_rank, CIRCLE_TAG_ABORT_REDUCE,
             comm, &status);
  }

  /* forward result to children */
  for (i = 0; i < children; i++) {
    /* get rank of child */
    int child = child_ranks[i];

    /* send child a message */
    MPI_Send(&flag, 1, MPI_INT, child, CIRCLE_TAG_ABORT_REDUCE, comm);
  }

  /* finally, set our abort flags */
  ABORT_FLAG = (int8_t)flag;
  abort_state = flag;

  return;
}

/**
 * Sends an abort message to all ranks.
 *
 * This function is used to send a 'poisoned' work request to each rank, so
 * that they will know to abort.
 */
void bcast_abort(void) {

  /* set global abort variable, this will kick off an abort bcast
   * the next time the worker loop calls abort_check */
  ABORT_FLAG = 1;

  return;
}

/**
 * Transition into abort state and sends abort messages through tree
 * if needed.
 */
void State::abortStart(int cleanup) {
  /* set global abort flag */
  ABORT_FLAG = 1;

  /* if we've already entered our abort state,
   * no need to do it again */
  if (abort_state) {
    return;
  }

  /* transition to local abort state */
  abort_state = 1;

  /* if in cleanup, everyone has terminated and we're trying to drain
   * messages, so don't send more */
  if (cleanup) {
    return;
  }

  /* otherwise, send abort messages through tree,
   * get info about our parent and children */
  int parent_rank = tree->parent_rank;
  int num_children = tree->children;
  int *child_ranks = tree->child_ranks;

  /* index into request array */
  int k = 0;

  /* send abort message to our parent if we have one */
  if (parent_rank != MPI_PROC_NULL) {
    /* post a receive for the reply to our abort request message */
    MPI_Irecv(NULL, 0, MPI_BYTE, parent_rank, CIRCLE_TAG_ABORT_REPLY,
              comm, &abort_req[k++]);

    /* post abort request to our parent */
    MPI_Isend(NULL, 0, MPI_BYTE, parent_rank, CIRCLE_TAG_ABORT_REQUEST,
              comm, &abort_req[k++]);
  }

  /* send abort message to each of our children */
  int i;
  for (i = 0; i < num_children; i++) {
    /* get rank of child */
    int child_rank = child_ranks[i];

    /* post a receive for the reply to our abort request message */
    MPI_Irecv(NULL, 0, MPI_BYTE, child_rank, CIRCLE_TAG_ABORT_REPLY,
              comm, &abort_req[k++]);

    /* post abort request to our child */
    MPI_Isend(NULL, 0, MPI_BYTE, child_rank, CIRCLE_TAG_ABORT_REQUEST,
              comm, &abort_req[k++]);
  }

  /* remember that we've sent our abort messages */
  if (k > 0) {
    abort_outstanding = 1;
  }

  return;
}

/**
 * Check whether we have received abort signal.
 *
 * Check whether we have received abort signal from the calling
 * process or from an abort request message sent by another
 * process, forward abort messages on tree if needed.
 */
void State::abortCheck(int cleanup) {
  /* check whether caller has set global abort variable */
  if (ABORT_FLAG) {
    /* bcast abort messages if needed */
    abortStart(cleanup);
  }

  /* check whether we have received a request to abort
   * from another process */
  int flag;
  MPI_Status status;
  MPI_Iprobe(MPI_ANY_SOURCE, CIRCLE_TAG_ABORT_REQUEST, comm, &flag,
             &status);

  /* process abort request message if we got one */
  if (flag) {
    /* we got a abort request message, get the source rank */
    int rank = status.MPI_SOURCE;

    /* receive the abort request message */
    MPI_Recv(NULL, 0, MPI_BYTE, rank, CIRCLE_TAG_ABORT_REQUEST,
             comm, &status);

    /* send an abort reply back */
    MPI_Send(NULL, 0, MPI_BYTE, rank, CIRCLE_TAG_ABORT_REPLY, comm);

    /* bcast abort messages if needed */
    abortStart(cleanup);
  }

  /* if we have sent abort messages, wait for the replies */
  if (abort_outstanding) {
    /* test whether all abort messages have completed */
    MPI_Testall(abort_num_req, abort_req, &flag, MPI_STATUSES_IGNORE);
    if (flag) {
      /* all requests have completed */
      abort_outstanding = 0;
    }
  }

  return;
}

/* send token using MPI_Issend and update state */
void State::tokenIsSend() {
  /* don't bother sending if we have aborted */
  if (ABORT_FLAG) {
    return;
  }

  /* send token -- it's important that we use issend here,
   * because this way the send won't complete until a matching
   * receive has been posted, which means as long as the send
   * is pending, the message is still on the wire */
  MPI_Issend(&token_buf, 1, MPI_INT, token_dest,
             CIRCLE_TAG_TOKEN, comm, &token_send_req);

  /* remember that we no longer have the token */
  token_is_local = 0;

  return;
}

/* given that we've received a token message,
 * receive it and update our state */
void State::tokenRecv() {
  /* verify that we don't already have a token */
  if (token_is_local) {
    /* ERROR */
  }

  /* get source of token */
  int src = token_src;

  /* receive the token message, this won't block because
   * we assume a message is waiting if to enter this call,
   * we receive to a temporary buffer because token_buf
   * may still be active from a send to another process */
  int token;
  MPI_Status status;
  MPI_Recv(&token, 1, MPI_INT, src, CIRCLE_TAG_TOKEN, comm, &status);

  /* record that token is now local */
  token_is_local = 1;

  /* if we have a token outstanding, at this point
   * we should have received the reply (even if we sent
   * the token to ourself, we just replied above so
   * the send should now complete) */
  if (token_send_req != MPI_REQUEST_NULL) {
    MPI_Wait(&token_send_req, &status);
  }

  /* now that our send is complete,
   * it's safe to overwrite the token buffer */
  token_buf = token;

  /* now set our state based on current state and token value */

  /* what's the purpose of this logic? */
  if (token_proc == BLACK && token == BLACK) {
    token_proc = WHITE;
  }

  /* check for termination conditions */
  int terminate = 0;

  if (rank == 0 && token == WHITE) {
    /* if rank 0 receives a white token,
     * we initiate the termination token */
    LOG(LogLevel::Debug, "Master has detected termination.");
    terminate = 1;
  } else if (token == TERMINATE) {
    /* if we're not rank 0, we just look for the terminate token */
    terminate = 1;
  }

  /* forward the terminate token if we have one */
  if (terminate) {
    /* send the terminate token, don't bother if we're
     * the last rank */
    token_buf = TERMINATE;

    if (rank < size - 1) {
      tokenIsSend();
    }

    /* set our state to terminate */
    token_proc = TERMINATE;
  }

  return;
}

void State::tokenCheck() {
  /* check for token and receive it if it arrived */
  int flag;
  MPI_Status status;
  MPI_Iprobe(token_src, CIRCLE_TAG_TOKEN, comm, &flag, &status);

  /* process it if we found one */
  if (flag) {
    /* found an incoming token, receive and process it */
    tokenRecv();
  }

  return;
}

/**
 * Checks for incoming tokens, determines termination conditions.
 *
 * When the master rank is idle, it generates a token that is initially white.
 * When a node is idle, and can't get work for one loop iteration, then it
 * checks for termination. It checks to see if the token has been passed to it,
 * additionally checking for the termination token. If a rank receives a black
 * token then it forwards a black token. Otherwise it forwards its own color.
 *
 * All nodes start out in the white state. State is *not* the same thing as
 * the token. If a node j sends work to a rank i (i < j) then its state turns
 * black. It then turns the token black when it comes around, forwards it, and
 * turns its state back to white.
 *
 * @param st the libcircle state struct.
 */
int State::checkForTerm() {
  /* if our state is marked TERMINATE, we're done */
  if (token_proc == TERMINATE) {
    return TERMINATE;
  }

#if 0

    /* if we only have one process, we're done */
    if(size == 1) {
        token_proc = TERMINATE;
        return TERMINATE;
    }

#endif

  /* to get here, we're idle, but we haven't yet terminated,
   * if we have the token, send it along, otherwise check to
   * see if it has arrived */
  if (token_is_local) {
    /* we have no work and we have the token,
     * set token color based on our rank and state and
     * its current value */
    if (rank == 0) {
      /* The master rank starts a white token */
      token_buf = WHITE;
    } else if (token_proc == BLACK) {
      /* Others turn the token black if they are
       * in the black state */
      token_buf = BLACK;
    }

    /* send the token */
    tokenIsSend();

    /* flip our color back to white */
    token_proc = WHITE;
  } else {
    /* we have no work but we don't have the token,
     * check whether it's arrived to us */
    tokenCheck();
  }

  /* return our current state */
  int state = token_proc;
  return state;
}

/**
 * This returns a rank (not yourself).
 */
void State::getNextProc() {
  if (size > 1) {
    do {
      next_processor = rand_r(&seed) % size;
    } while (next_processor == rank);
  } else {
    /* for a job size of one, we have no one to ask */
    next_processor = MPI_PROC_NULL;
  }
}

/**
 * @brief Extend the offset arrays.
 */
int8_t State::extendOffsets(int32_t size) {
  /* get current size of offset arrays */
  int32_t count = offsets_count;

  /* if size we need is less than or equal to current size,
   * we don't need to do anything */
  if (size <= count) {
    return 0;
  }

  /* otherwise, allocate more in blocks of 4096 at a time */
  while (count < size) {
    count += 4096;
  }

  LOG(LogLevel::Debug, "Extending offset arrays from %d to %d.",
      offsets_count, count);

  offsets_recv_buf =
      (int *)realloc(offsets_recv_buf, (size_t)count * sizeof(int));

  offsets_send_buf =
      (int *)realloc(offsets_send_buf, (size_t)count * sizeof(int));

  LOG(LogLevel::Debug, "Work offsets: [%p] -> [%p]",
      (void *)offsets_recv_buf,
      (void *)(offsets_recv_buf + ((size_t)count * sizeof(int))));

  LOG(LogLevel::Debug, "Request offsets: [%p] -> [%p]",
      (void *)offsets_send_buf,
      (void *)(offsets_send_buf + ((size_t)count * sizeof(int))));

  /* record new length of offset arrays */
  offsets_count = count;

  if (offsets_recv_buf == NULL || offsets_send_buf == NULL) {
    return -1;
  }

  return 0;
}

/* we execute this function when we have detected incoming work messages */
int32_t State::workReceive(Queue *qp, int source, int size) {
  /* this shouldn't happen, but let's check so we don't blow out
   * memory allocation below */
  if (size <= 0) {
    LOG(LogLevel::Fatal, "size <= 0.");
    MPI_Abort(comm, CIRCLE_MPI_ERROR);
    return -1;
  }

  /* Check to see if the offset array is large enough */
  if (extendOffsets(size) < 0) {
    LOG(LogLevel::Error, "Error: Unable to extend offsets.");
    MPI_Abort(comm, CIRCLE_MPI_ERROR);
    return -1;
  }

  /* Receive item count, character count, and offsets */
  MPI_Status status;
  MPI_Recv(offsets_recv_buf, size, MPI_INT, source,
           CIRCLE_TAG_WORK_REPLY, comm, &status);

  /* the first int has number of items or an ABORT code */
  int items = offsets_recv_buf[0];

  if (items == 0) {
    /* we received 0 elements, there is no follow on message */
    LOG(LogLevel::Debug, "Received no work.");
    local_no_work_received++;
    return 0;
  } else if (items == PAYLOAD_ABORT) {
    /* we've received a signal to kill the job,
     * there is no follow on message in this case */
    ABORT_FLAG = 1;
    return PAYLOAD_ABORT;
  } else if (items < 0) {
    /* TODO: when does this happen? */
    return -1;
  }

  /* the second int is the number of characters we'll receive,
   * make sure our queue has enough storage */
  int chars = offsets_recv_buf[1];
  size_t new_bytes = (size_t)(qp->head + (uintptr_t)chars) * sizeof(char);

  if (new_bytes > qp->base.size()) {
    if (parent->impl->queue.extend(new_bytes) < 0) {
      LOG(LogLevel::Error, "Error: Unable to realloc string pool.");
      MPI_Abort(comm, CIRCLE_MPI_ERROR);
      return -1;
    }
  }

  /* receive second message containing work elements */
  MPI_Recv(&qp->base[0], chars, MPI_CHAR, source, CIRCLE_TAG_WORK_REPLY,
           comm, MPI_STATUS_IGNORE);

  /* make sure we have a pointer allocated for each element */
  int32_t count = items;

  if (count > qp->strings.size()) {
    if (parent->impl->queue.extendStr(count) < 0) {
      LOG(LogLevel::Error, "Error: Unable to realloc string array.");
      MPI_Abort(comm, CIRCLE_MPI_ERROR);
      return -1;
    }
  }

  /* set offset to each element in our queue */
  int32_t i;

  for (i = 0; i < count; i++) {
    qp->strings[i] = (uintptr_t)offsets_recv_buf[i + 2];
  }

  /* double check that the base offset is valid */
  if (qp->strings[0] != 0) {
    LOG(LogLevel::Fatal,
        "The base address of the queue doesn't match what it should be.");
    MPI_Abort(comm, CIRCLE_MPI_ERROR);
    return -1;
  }

  /* we now have count items in our queue */
  qp->count = count;

  /* set head of queue to point just past end of last element string */
  qp->head += (uintptr_t)chars;

  /* log number of items we received */
  LOG(LogLevel::Debug, "Received %d items from %d", count, source);

  /* send receipt back to source to notify we are now
   * accounting for this work */
  MPI_Send(NULL, 0, MPI_BYTE, source, CIRCLE_TAG_WORK_RECEIPT, comm);

  return 0;
}

/**
 * @brief Requests work from other ranks.
 *
 * Request work from a random rank.  If it receives no work
 * in the work reply from that process, a different rank
 * will be asked during the next iteration.
 */
int32_t State::requestWork(Queue *qp, int cleanup) {
  int rc = 0;

  /* check whether we have a work request outstanding, and check for
   * a reply if we do, otherwise send a request so long as we're not
   * in cleanup mode */
  if (work_requested) {
    /* we've already requested work from someone, check whether
     * we got a reply */

    /* get rank of process we requested work from */
    int source = work_requested_rank;

    /* see if we got a work reply from that process */
    int flag;
    MPI_Status status;
    MPI_Iprobe(source, CIRCLE_TAG_WORK_REPLY, comm, &flag, &status);

    /* if we got a reply, process it */
    if (flag) {
      /* get number of integers in reply message */
      int size;
      MPI_Get_count(&status, MPI_INT, &size);

      /* receive message(s) and set return code */
      rc = workReceive(qp, source, size);

      /* flip flag to indicate we're no longer waiting for a reply */
      work_requested = 0;
    }
  } else if (!cleanup && !ABORT_FLAG) {
    /* need to send request, get rank of process to request work from */
    int source = next_processor;

    /* have no one to ask, we're done */
    if (source == MPI_PROC_NULL) {
      return rc;
    }

    LOG(LogLevel::Debug, "Sending work request to %d...", source);

    /* increment number of work requests for profiling */
    local_work_requested++;

    /* TODO: use isend to avoid deadlocks */
    /* send work request */
    MPI_Send(NULL, 0, MPI_BYTE, source, CIRCLE_TAG_WORK_REQUEST, comm);

    /* set flag and source to indicate we requested work */
    work_requested = 1;
    work_requested_rank = source;

    /* randomly pick another source to ask next time */
    getNextProc();
  }

  return rc;
}

/* spread count equally among ranks, handle cases where number
 * of ranks doesn't evenly divide remaining count by scattering
 * remainder across initial ranks */
static void spread_counts(int *sizes, int ranks, int count) {
  int base = count / ranks;
  int extra = count - base * ranks;

  int i = 0;

  while (i < extra) {
    sizes[i] = base + 1;
    i++;
  }

  while (i < ranks) {
    sizes[i] = base;
    i++;
  }

  return;
}

/**
 * Sends a no work reply to someone requesting work.
 */
void State::sendNoWork(int dest) {
  int no_work[2];
  no_work[0] = (ABORT_FLAG) ? PAYLOAD_ABORT : 0;
  no_work[1] = 0;

  MPI_Request r;
  MPI_Isend(&no_work, 1, MPI_INT, dest, CIRCLE_TAG_WORK_REPLY,
            parent->impl->comm, &r);
  MPI_Wait(&r, MPI_STATUS_IGNORE);
}

/**
 * Sends work to a requestor
 */
int State::sendWork(Queue *qp, int dest, int32_t count) {
  if (count <= 0) {
    sendNoWork(dest);
    /* Add cost of message */
    return 0;
  }

  /* For termination detection */
  if (dest < rank || dest == token_src) {
    token_proc = BLACK;
  }

  /* Base address of the buffer to be sent */
  int32_t start_elem = qp->count - count;
  uintptr_t start_offset = qp->strings[start_elem];

  /* Address of the beginning of the last string to be sent */
  int32_t end_elem = qp->count - 1;
  uintptr_t end_offset = qp->strings[end_elem];

  /* Distance between them */
  size_t len = qp->head - start_offset;

  /* TODO: check that len doesn't overflow an int */
  int bytes = (int)len;

  /* total number of ints we'll send */
  int numoffsets = 2 + count;

  /* Check to see if the offset array is large enough */
  if (extendOffsets(numoffsets) < 0) {
    LOG(LogLevel::Error, "Error: Unable to extend offsets.");
    return -1;
  }

  /* offsets[0] = number of strings */
  /* offsets[1] = number of chars being sent */
  offsets_send_buf[0] = (int)count;
  offsets_send_buf[1] = (int)bytes;

  /* now compute offset of each string */
  int32_t i = 0;
  int32_t current_elem = start_elem;

  for (i = 0; i < count; i++) {
    offsets_send_buf[2 + i] =
        (int)(qp->strings[current_elem] - start_offset);
    current_elem++;
  }

  /* TODO; use isend to avoid deadlock, but in that case, be careful
   * to not overwrite space in queue before sends complete */

  /* send item count, total bytes, and offsets of each item */
  MPI_Send(offsets_send_buf, numoffsets, MPI_INT, dest,
           CIRCLE_TAG_WORK_REPLY, comm);

  /* send data */
  char *buf = &qp->base[0] + start_offset;
  MPI_Send(buf, bytes, MPI_CHAR, dest, CIRCLE_TAG_WORK_REPLY, comm);

  LOG(LogLevel::Debug, "Sent %d of %d items to %d.", offsets_send_buf[0],
      qp->count, dest);

  /* subtract elements from our queue */
  qp->count -= count;

  /* adjust the qp->head */
  qp->head = start_offset;

  /* track number of outstanding messages that transfer work */
  work_outstanding++;

  return 0;
}

/**
 * Distributes a random amount of the local work queue to the n requestors.
 */
void State::sendWorkToMany(Queue *qp, int *requestors, int rcount) {
  int i = 0;

  if (rcount <= 0) {
    LOG(LogLevel::Fatal,
        "Something is wrong with the amount of work we think we have.");
    exit(EXIT_FAILURE);
  }

  /* TODO: could allocate this once up front during init */
  /* we have rcount requestors and ourself, allocate array to store
   * number of elements we'll send to each, storing the amount we
   * keep as the first entry */
  int num_ranks = rcount + 1;
  int *sizes = (int *)malloc((size_t)num_ranks * sizeof(int));

  if (sizes == NULL) {
    LOG(LogLevel::Fatal, "Failed to allocate memory for sizes.");
    MPI_Abort(comm, CIRCLE_MPI_ERROR);
  }

  if ((parent->runtimeFlags & RuntimeFlags::SplitEqual) !=
      RuntimeFlags::None) {
    /* split queue equally among ourself and all requestors */
    spread_counts(&sizes[0], num_ranks, qp->count);
  } else { /* SPLIT_RANDOM */
    /* randomly pick a total amount to send to requestors,
     * but keep at least one item */
    int send_count = (rand_r(&seed) % qp->count) + 1;

    if (send_count == qp->count) {
      send_count--;
    }

    /* we keep the first portion, and spread the rest */
    sizes[0] = qp->count - send_count;
    spread_counts(&sizes[1], rcount, send_count);
  }

  /* send elements to requestors, note the requestor array
   * starts at 0 and sizes start at 1 */
  for (i = 0; i < rcount; i++) {
    sendWork(qp, requestors[i], sizes[i + 1]);
  }

  ::free(sizes);

  LOG(LogLevel::Debug, "Done servicing requests.");
}

/**
 * Checks for outstanding work requests
 */
void State::workreceiptCheck(Queue *qp) {
  /* pick off any work request mesasges we have */
  while (work_outstanding > 0) {
    /* Test to see if we have any work receipt message to receive */
    int flag;
    MPI_Status status;
    MPI_Iprobe(MPI_ANY_SOURCE, CIRCLE_TAG_WORK_RECEIPT, comm, &flag,
               &status);

    /* if we don't have any, break out of the loop */
    if (!flag) {
      break;
    }

    /* we got a work receipt message, get the rank */
    int rank = status.MPI_SOURCE;

    /* receive the message */
    MPI_Recv(NULL, 0, MPI_BYTE, rank, CIRCLE_TAG_WORK_RECEIPT, comm,
             &status);

    /* decrement our count of outstanding work messages */
    work_outstanding--;

    /* force a fresh termination allreduce
     * when transferring work */
    term_flag = 0;
  }
}

/**
 * Checks for outstanding work requests
 */
void State::workreqCheck(Queue *qp, int cleanup) {
  /* record list of requesting ranks in requestors
   * and number in rcount */
  int *requestors = requestors;
  int rcount = 0;

  /* pick off any work request mesasges we have */
  while (1) {
    /* Test for any work request message */
    int flag;
    MPI_Status status;
    MPI_Iprobe(MPI_ANY_SOURCE, CIRCLE_TAG_WORK_REQUEST, comm, &flag,
               &status);

    /* if we don't have any, break out of the loop */
    if (!flag) {
      break;
    }

    /* we got a work request message, get the rank */
    int rank = status.MPI_SOURCE;

    /* receive the message */
    MPI_Recv(NULL, 0, MPI_BYTE, rank, CIRCLE_TAG_WORK_REQUEST, comm,
             &status);

    /* add rank to requestor list */
    LOG(LogLevel::Debug, "Received work request from %d", rank);
    requestors[rcount] = rank;
    rcount++;
  }

  /* If we didn't receive any work request, no need to continue */
  if (rcount == 0) {
    return;
  }

  /* send work to requestors */
  if (qp->count == 0 || cleanup || ABORT_FLAG) {
    /* we send "no work" messages back if we have no work,
     * we are in a cleanup phase, or we have received an
     * abort message */
    int i;
    for (i = 0; i < rcount; i++) {
      sendNoWork(requestors[i]);
    }
  } else {
    /* Otherwise, divy up the work items we have among the
     * requesting ranks */
    sendWorkToMany(qp, requestors, rcount);
  }

  return;
}

/**
 * Print the offsets of a copied queue.
 */
void State::printOffsets(uint32_t *offsets, int32_t count) {
  int32_t i = 0;

  for (i = 0; i < count; i++) {
    LOG(LogLevel::Debug, "\t[%d] %d", i, offsets[i]);
  }
}

