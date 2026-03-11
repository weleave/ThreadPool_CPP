#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

class ThreadPool {
public:
    // 构造函数：创建固定数量的工作线程
    explicit ThreadPool(std::size_t thread_count = std::thread::hardware_concurrency()) {
        if (thread_count == 0) {
            // 保险起见，至少开一个线程，避免硬件并发数返回 0
            thread_count = 1;
        }

        workers_.reserve(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this]() { worker_loop(); });
        }
    }

    // 禁止拷贝，线程池一般都不希望被拷来拷去
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 析构时做优雅关闭，防止线程泄漏
    ~ThreadPool() {
        shutdown();
    }

    // 提交任务：支持任意可调用对象，返回 future 取结果
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType = std::invoke_result_t<F, Args...>;

        // 把任务封装成 packaged_task，方便 future 获取返回值/异常
        auto task_ptr = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<ReturnType> result = task_ptr->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!accept_new_tasks_) {
                throw std::runtime_error("线程池已停止接收新任务");
            }

            task_queue_.emplace([task_ptr]() { (*task_ptr)(); });
            ++unfinished_tasks_;
        }

        // 通知一个空闲线程来取任务
        task_cv_.notify_one();
        return result;
    }

    // 阻塞等待：等到当前所有任务都执行完成
    void wait_for_tasks() {
        std::unique_lock<std::mutex> lock(mutex_);
        finished_cv_.wait(lock, [this]() { return unfinished_tasks_ == 0; });
    }

    // 优雅关闭：不再收新任务，先做完队列，再退出线程
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutdown_called_) {
                return;
            }
            shutdown_called_ = true;
            accept_new_tasks_ = false;
        }

        // 先等已有任务做完
        wait_for_tasks();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_workers_ = true;
        }

        // 唤醒所有线程，让它们看到 stop 标志并退出
        task_cv_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    // 方便外部查看线程数量（比如打印日志）
    std::size_t size() const {
        return workers_.size();
    }

private:
    // 工作线程主循环：不断从队列取任务并执行
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                task_cv_.wait(lock, [this]() { return stop_workers_ || !task_queue_.empty(); });

                // 只有在“停止 + 队列空”的情况下才真正退出线程
                if (stop_workers_ && task_queue_.empty()) {
                    return;
                }

                task = std::move(task_queue_.front());
                task_queue_.pop();
            }

            // 真正执行任务
            task();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                --unfinished_tasks_;
                if (unfinished_tasks_ == 0) {
                    finished_cv_.notify_all();
                }
            }
        }
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> task_queue_;

    mutable std::mutex mutex_;
    std::condition_variable task_cv_;
    std::condition_variable finished_cv_;

    bool accept_new_tasks_ = true;
    bool stop_workers_ = false;
    bool shutdown_called_ = false;
    std::size_t unfinished_tasks_ = 0;
};

