#include <pybind11/functional.h>
#include <pybind11/pybind11.h>

#include "lanl_circle.hpp"

namespace py = pybind11;

#if 0
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
#endif

PYBIND11_MODULE(hddm_solver, m) {
  m.doc() = "Python interface for LANL Circle";

  py::enum_<circle::RuntimeFlags>(
      m, "RuntimeFlags", py::arithmetic(),
      "Run time flags for the behavior of splitting work")
      .value("SplitRandom", circle::RuntimeFlags::SplitRandom,
             "Split work randomly")
      .value("SplitEqual", circle::RuntimeFlags::SplitEqual,
             "Split work evenly")
      .value("CreateGlobal", circle::RuntimeFlags::CreateGlobal,
             "Call create callback on all procs")
      .value("TermTree", circle::RuntimeFlags::TermTree,
             "Use tree-based termination")
      .value("DefaultFlags", circle::RuntimeFlags::SplitEqual,
             "Default behavior is random work stealing");

  py::enum_<circle::LogLevel>(
      m, "LogLevel", py::arithmetic(),
      "The various logging levels that Circle will output")
      .value("CircleNone", circle::LogLevel::None)
      .value("CircleFatal", circle::LogLevel::Fatal)
      .value("CircleError", circle::LogLevel::Error)
      .value("CircleWarning", circle::LogLevel::Warning)
      .value("CircleInfo", circle::LogLevel::Info)
      .value("CircleDebug", circle::LogLevel::Debug);

  m.def(
      "create",
      [](circle::CallbackFunc create_callback,
         circle::CallbackFunc process_callback,
         enum circle::RuntimeFlags flags) {
        return py::cast(std::shared_ptr<circle::Circle>(
            new circle::Circle(create_callback, process_callback, flags)));
      },
      "Initialize a Circle instance for parallel processing");

  m.def(
      "create",
      [](circle::CallbackFunc create_callback,
         circle::CallbackFunc process_callback,
         circle::CallbackFunc reduce_init_callback,
         circle::reduceOperationCallbackFunc reduce_operation_callback,
         circle::reduceFinalizeCallbackFunc reduce_finalize_callback,
         enum circle::RuntimeFlags flags) {
        return py::cast(std::shared_ptr<circle::Circle>(new circle::Circle(
            create_callback, process_callback, reduce_init_callback,
            reduce_operation_callback, reduce_finalize_callback, flags)));
      },
      "Initialize a Circle instance for parallel processing and reduction");

  m.def("get_log_level", [](py::object circle) {
    return circle.cast<std::shared_ptr<circle::Circle>>()->getLogLevel();
  });

  m.def(
      "set_log_level",
      [](py::object circle, enum circle::LogLevel level) {
        circle.cast<std::shared_ptr<circle::Circle>>()->setLogLevel(level);
      },
      "Define the detail of logging that Circle should output");

  m.def("get_runtime_flags", [](py::object circle) {
    return circle.cast<std::shared_ptr<circle::Circle>>()->getRuntimeFlags();
  });

  m.def(
      "get_runtime_flags",
      [](py::object circle, enum circle::RuntimeFlags flags) {
        circle.cast<std::shared_ptr<circle::Circle>>()->setRuntimeFlags(flags);
      },
      "Change runtime flags");

  m.def("get_tree_width", [](py::object circle) {
    return circle.cast<std::shared_ptr<circle::Circle>>()->getTreeWidth();
  });

  m.def(
      "set_tree_width",
      [](py::object circle, int width) {
        circle.cast<std::shared_ptr<circle::Circle>>()->setTreeWidth(width);
      },
      "Change the width of the k-ary communication tree");

  m.def("get_reduce_period", [](py::object circle) {
    return circle.cast<std::shared_ptr<circle::Circle>>()->getReducePeriod();
  });

  m.def(
      "set_reduce_period",
      [](py::object circle, int secs) {
        circle.cast<std::shared_ptr<circle::Circle>>()->setReducePeriod(secs);
      },
      "Change the number of seconds between consecutive reductions");

  m.def("get_rank", [](py::object circle) {
    return circle.cast<std::shared_ptr<circle::Circle>>()->getRank();
  });

#if 0
void circle_reduce(Circle circle, const void *buf, size_t size);
#endif

  m.def(
      "execute",
      [](py::object circle) {
        circle.cast<std::shared_ptr<circle::Circle>>()->execute();
      },
      "Once you've defined and told Circle about your callbacks, use this to "
      "execute your program");

  m.def(
      "abort",
      [](py::object circle) {
        circle.cast<std::shared_ptr<circle::Circle>>()->abort();
      },
      "Call this function to have all ranks dump a checkpoint file and exit");

  m.def(
      "read_restarts",
      [](py::object circle) {
        return circle.cast<std::shared_ptr<circle::Circle>>()->readRestarts();
      },
      "Call this function to read in Circle restart files");

  m.def(
      "checkpoint",
      [](py::object circle) {
        return circle.cast<std::shared_ptr<circle::Circle>>()->checkpoint();
      },
      "Call this function to read in Circle restart files. Each rank"
      " writes a file called circle<rank>.txt");

  m.def(
      "enqueue",
      [](py::object circle, const std::vector<uint8_t> &element) {
        return circle.cast<std::shared_ptr<circle::Circle>>()->enqueue(element);
      },
      "The interface to the work queue. This can be accessed from within the"
      " process and create work callbacks");

  m.def("dequeue", [](py::object circle) {
    std::vector<uint8_t> element;
    circle.cast<std::shared_ptr<circle::Circle>>()->dequeue(element);
    return element;
  });

  m.def("get_local_queue_size", [](py::object circle) {
    return circle.cast<std::shared_ptr<circle::Circle>>()->getLocalQueueSize();
  });

  m.def(
      "init",
      []() {
        int argc = 0;
        char **argv = nullptr;
        return circle::init(&argc, &argv);
      },
      "Initialize internal state needed by Circle. This should be called before"
      " any other Circle API call. This returns the MPI rank value.");

  m.def("wtime", &circle::wtime,
        "Returns an elapsed time on the calling processor for benchmarking "
        "purposes");
}
