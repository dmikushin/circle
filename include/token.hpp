#ifndef TOKEN_H
#define TOKEN_H

#include <getopt.h>
#include <mpi.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "queue.hpp"

namespace circle {

namespace internal {

enum tags {
  WHITE,
  BLACK,
  TERMINATE = -1,
  CIRCLE_TAG_WORK_REQUEST,
  CIRCLE_TAG_WORK_REPLY,
  CIRCLE_TAG_WORK_RECEIPT,
  CIRCLE_TAG_TOKEN,
  CIRCLE_TAG_REDUCE,
  CIRCLE_TAG_BARRIER,
  CIRCLE_TAG_TERM,
  CIRCLE_TAG_ABORT_REQUEST,
  CIRCLE_TAG_ABORT_REPLY,
  CIRCLE_TAG_ABORT_REDUCE,
  MSG_VALID,
  MSG_INVALID,
  PAYLOAD_ABORT = -32
};

typedef struct options {
  char *beginning_path;
  int verbose;
} options;

/* records info about the tree of spawn processes */
class TreeState {

public : // TODO remove
  int rank;         /* our global rank (0 to ranks-1) */
  int ranks;        /* number of nodes in tree */
  int parent_rank;  /* rank of parent */
  int children;     /* number of children we have */
  int *child_ranks; /* global ranks of our children */

  Circle* parent;

public :

  TreeState(Circle* parent_, int rank, int ranks, int k, MPI_Comm comm);

  ~TreeState();

  template<typename ... Args>
  void log(LogLevel logLevel_, const char* filename, int lineno, Args&& ... args)
  {
    parent->log(logLevel_, filename, lineno, std::forward<Args>(args) ...);
  }
};

class State {

public : // TODO remove
  /* communicator and our rank and its size */
  MPI_Comm comm;
  int rank;
  int size;

  /* tracks state of token */
  int token_is_local; /* flag indicating whether we have the token */
  int token_proc;     /* current color of process: WHITE, BLACK, TERMINATE */
  int token_buf;      /* buffer holding current token color */
  int token_src;      /* rank of process who will send token to us */
  int token_dest;     /* rank of process to which we send token */
  MPI_Request token_send_req; /* request associated with pending receive */

  /* offset arrays are used to transfer length of items while sending work */
  int offsets_count; /* number of offsets in work and request offset arrays */
  int *offsets_recv_buf; /* buffer in which to receive an array of offsets when
                            receiving work */
  int *offsets_send_buf; /* buffer to specify offsets while sending work */

  /* these are used for persistent receives of work request messages
   * from other tasks */
  int *requestors; /* list of ranks requesting work from us */

  /* used to randomly pick next process to requeset work from */
  unsigned seed;      /* seed for random number generator */
  int next_processor; /* rank of next process to request work from */

  /* manage state for requesting work from other procs */
  int work_requested;      /* flag indicating we have requested work */
  int work_requested_rank; /* rank of process we requested work from */

  /* tree used for collective operations */
  TreeState* tree; /* parent and children of tree */

  /* manage state for reduction operations */
  int reduce_enabled;      /* flag indicating whether reductions are enabled */
  double reduce_time_last; /* time at which last reduce ran */
  double reduce_time_interval; /* seconds between reductions */
  int reduce_outstanding; /* flag indicating whether a reduce is outstanding */
  int reduce_replies; /* keeps count of number of children who have replied */
  long long int reduce_buf[3]; /* local reduction buffer */

  /* manage state for barrier operations */
  int barrier_started; /* flag indicating whether local process has initiated
                          barrier */
  int barrier_up; /* flag indicating whether we have sent message to parent */
  int barrier_replies; /* keeps count of number of chidren who have replied */

  /* manage state for termination allreduce operations */
  int term_tree_enabled; /* flag indicating whether to use tree-based
                            termination */
  int work_outstanding;  /* counter to track number of outstanding work transfer
                            messages */
  int term_flag; /* whether we have sent work to anyone since last allreduce */
  int term_up;   /* flag indicating whether we have sent message to parent */
  int term_replies; /* keeps count of number of chidren who have replied */

  /* manage state for abort broadcast tree */
  int abort_state; /* flag tracking whether process is in abort state or not */
  int abort_outstanding; /* flag indicating whether we are waiting on abort
                            reply messages */
  int abort_num_req;     /* number of abort requests */
  MPI_Request
      *abort_req; /* pointer to array of MPI_Requests for abort messages */

  /* profiling counters */
  int32_t local_objects_processed; /* number of locally completed work items */
  uint32_t
      local_work_requested; /* number of times a process asked us for work */
  uint32_t
      local_no_work_received; /* number of times a process asked us for work */

  Circle* parent;

public :

  State(Circle* parent);

  ~State();

  template<typename ... Args>
  void log(LogLevel logLevel_, const char* filename, int lineno, Args&& ... args)
  {
    parent->log(logLevel_, filename, lineno, std::forward<Args>(args) ...);
  }

  /* initiate and execute reduction in background */
  void reduceCheck(int count, int cleanup);

  /* execute synchronous reduction */
  void reduceSync(int count);

  /* start non-blocking barrier */
  void barrierStart();

  /* test for completion of non-blocking barrier,
   * returns 1 when all procs have called barrier_start (and resets),
   * returns 0 otherwise */
  int barrierTest();

  /* test for abort, forward abort messages on tree if needed,
   * draining incoming abort messages */
  void abortCheck(int cleanup);

  /* execute an allreduce to determine whether any rank has entered
   * the abort state, and if so, set all ranks to be in abort state */
  void abortReduce();

  void getNextProc();

  int checkForTermAllReduce();

  void workreceiptCheck(Queue *queue);

  void workreqCheck(Queue *queue, int cleanup);

  int32_t requestWork(Queue *queue, int cleanup);

  void sendNoWork(int32_t dest);

  int8_t extendOffsets(int32_t size);

  void printOffsets(uint32_t *offsets, int32_t count);

  void abortStart(int cleanup);

  void bcast_abort();

  /* send token using MPI_Issend and update state */
  void tokenIsSend();

  /* given that we've received a token message,
   * receive it and update our state */
  void tokenRecv();

  /* checks for and receives an incoming token message,
   * then updates state */
  void tokenCheck();

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
  int checkForTerm();

  /* we execute this function when we have detected incoming work messages */
  int32_t workReceive(Queue *qp, int source, int size);

  /**
   * Sends work to a requestor
   */
  int sendWork(Queue *qp, int dest, int32_t count);

  /**
   * Distributes a random amount of the local work queue to the n requestors.
   */
  void sendWorkToMany(Queue *qp, int *requestors, int rcount);
};

} // namespace internal

} // namespace circle

#endif /* TOKEN_H */

