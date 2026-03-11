#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>

/// Simple thread-safe FIFO queue for communication between threads.
/// Uses blocking pop with shutdown signalling.
//We can use lock-free queue but its not required here. 
//multi‑producer, multi‑consumer, high‑throughput market data needs lock-free structures.
template <typename T>
class ThreadSafeQueue {
public:
    /// Push a new element into the queue.
    void push(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(std::move(value));
        }
        cv_.notify_one();
    }

    /// Pop an element from the queue, blocking until one is available or shutdown is signalled.
    /// Returns false only if the queue is being shut down and is empty.
    bool pop(T& out) {
        std::unique_lock<std::mutex> lock(mutex_);
        //Wake up on shutdown(true) or when an item is available.
        cv_.wait(lock, [this] { return !queue_.empty() || shutdown_; });

        //Even if shutdown, still pop remaining items
        if (shutdown_ && queue_.empty()) {
            return false;
        }

        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    /// Signal that no more items will be pushed; wakes up all waiting threads.
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
        }
        cv_.notify_all();
    }

private:
    std::deque<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool shutdown_{false};
};
