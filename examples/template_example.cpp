// Conceptual example for an Emitter node's svc method
#include <ff/ff.hpp>
#include <memory>
// #include <print>

struct MyTask {
  int data; /* other members */

  ~MyTask() {
    // std::println("Destryoing object {}", data); // To test std::unique_ptr automatic deallocation
  }
};

/*
 * Using IN_T = void does not work because ff_node_t<IN_T, OUT_T> defines a function
 * void* svc(void*) { return svc<IN_T, OUT_T>(reinterpret_cast<IN_T*>(task)); } which would clash
 * with the overloaded function defined in the Emitter struct below. In C++ there cannot be two
 * overloaded functions differing only by their return type.
 * */

struct Emitter : ff::ff_node_t<MyTask> { // equivalent to ff_node_t<MyTask, MyTask> since OUT_T defaults to IN_T
  auto svc(MyTask*) -> MyTask* override { // Unused input
    for (int i = 0; i < 100; ++i) {
      auto task = std::make_unique<MyTask>();
      task->data = i;
      // ff_send_out expects a raw pointer; release ownership from unique_ptr
      ff_send_out(task.release());
    }
    return EOS;  // Signal End-Of-Stream
  }
};

struct Collector : ff::ff_node_t<MyTask, void> { 
  auto svc(MyTask* task) -> void* override {
    if (task != nullptr) {
      auto ptr = std::unique_ptr<MyTask>(task);
      // std::println("Processing object {}", ptr->data);
    }
    return GO_ON;
  }
};

auto main() -> int {
  auto pipe = ff::ff_pipeline();

  pipe.add_stage(new Emitter());
  pipe.add_stage(new Collector());
  // std::println("Pipe started");
  pipe.run_and_wait_end();
  // std::println("Pipe ended");
}
