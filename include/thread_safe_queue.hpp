#include <condition_variable>
#include <mutex>
#include <queue>

template <typename T>
class ThreadSafeQueue {
 public:
  auto push(T item) -> void {
    std::lock_guard lock(mutex_);
    queue_.push(std::move(item));
    cv_.notify_one();
  }
  auto pop() -> T {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] {
      return !queue_.empty();
    });
    T item = std::move(queue_.front());
    queue_.pop();
    return item;
  }

 private:
  std::queue<T> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
};
