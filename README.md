# Circle

This is a deeply refactored fork of [LibCircle](https://github.com/hpc/libcircle) from Los Alamos folks.

Circle: deploy efficient cluster map-reduce tasks without explicit use of MPI. Circle offers a simplified API for distributing embarrassingly parallel workloads among distributed-memory compute nodes. My means of Cirlce, a client application avoids comprehensive MPI deployment, yet Circle uses MPI runtime internally. Distributed processing and reduction is organized with callback functions. Circle manages a queue of work items that are dynamically assigned to work processes. Details on the algorithms used may be found at <http://dl.acm.org/citation.cfm?id=2389114>.

## Prerequisites

* MPI development and runtime, e.g. OpenMPI: <http://www.open-mpi.org/>

## Building

```
mkdir build
cd build
cmake ..
make -j4
```

## Build options

To configure logging verbosity, call `circle::enable_logging(enum circle::LogLevel level)` with one of the following values:

* `LogLevel::Fatal`: fatal errors only.
* `LogLevel::Errors`: errors and lower log levels.
* `LogLevel::Warnings`: warnings and lower log levels.
* `LogLevel::Info`: info messages on internal operations and lower log levels.
* `LogLevel::Debug`: fine grained debug messages and lower log levels.

## Testing

```
mpirun -np 4 ./example
mpirun -np 4 ./example_reduction
```

## Examples

The basic program flow when using Circle is the following:

1. Define callbacks which enqueue or dequeue strings from the queue.
2. Execute the program.

The basic example `src/examples/example.cpp` demonstrates the use of callbacks.

When reductions are enabled, Circle periodically executes a global,
user-defined reduction operation based on a time specified by the user.
A final reduction executes after the work loop terminates.
To use the optional reduction:

1. Define and register three callback functions with Circle:
 * `circle::reduceInitCallback` - This function is called once on each process for each reduction invocation to capture the initial contribution from that process to the reduction.
 * `circle::reduceOperationCallback` - This function is called each time Circle needs to combine two reduction values.  It defines the reduction operation.
 * `circle::reduceFinalizeCallback` - This function is called once on the root process to output the final reduction result.
2. Update the value of reduction variable(s) within the `circle::processCallback` callback as work items are dequeued and processed by Circle.
3. Specify the time period between consecutive reductions with a call to `Circle::setReducePeriod` to enable them.

The example `src/examples/example_reduction.cpp` shows how to use reductions to periodically print
the number of items processed. Each process counts the number of items it has processed locally.
The reducton computes the global sum across processes, and it prints the global sum along with the average rate.

## Runtime options

The following bit flags can be OR'ed together and passed to `Circle::Circle` constructor or at anytime through `Circle::setRuntimeFlags` before
calling `Circle::execute`:

* `RuntimeFlags::SplitRandom` - randomly divide items among processes requesting work
* `RuntimeFlags::SplitEqual` - equally divide items among processes requesting work
* `RuntimeFlags::CreateGlobal` - invoke create callback on all processes, instead of just the rank 0 process
* `RuntimeFlags::TermTree` - use tree-based termination detection, instead of ring-based token passing

