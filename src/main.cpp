#include "thread_pool.hpp"

#include <chrono>
#include <iostream>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

int main() {
    // 这里取一个比较稳妥的线程数：至少 2 个，避免 demo 太“单线程”
    const std::size_t worker_count =
        std::max<std::size_t>(2, std::thread::hardware_concurrency());

    ThreadPool pool(worker_count);
    std::cout << "线程池启动完成，工作线程数量: " << pool.size() << '\n';

    std::mutex cout_mutex;
    std::vector<std::future<int>> futures;
    futures.reserve(12);

    // 模拟提交一批“有点耗时”的任务
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 1; i <= 12; ++i) {
        futures.push_back(pool.submit([i, &cout_mutex]() -> int {
            // 不同任务睡眠时间不一样，模拟真实业务的耗时差异
            std::this_thread::sleep_for(std::chrono::milliseconds(120 + (i % 4) * 60));
            const int value = i * i;

            {
                // 上锁是为了让日志更整齐，不然多线程同时输出会串行
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "[任务 " << i << "] 在线程 " << std::this_thread::get_id()
                          << " 上执行完成，结果 = " << value << '\n';
            }

            return value;
        }));
    }

    // 这里演示“阻塞等待”，等队列中任务全部执行完
    pool.wait_for_tasks();
    const auto end = std::chrono::steady_clock::now();

    std::vector<int> results;
    results.reserve(futures.size());
    for (auto& f : futures) {
        results.push_back(f.get());
    }

    const int sum = std::accumulate(results.begin(), results.end(), 0);
    const auto cost_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    std::cout << "全部任务执行完成，结果总和: " << sum << '\n';
    std::cout << "本次 demo 耗时: " << cost_ms << " ms\n";

    // 可以主动调用 shutdown，也可以不写让析构自动收尾
    pool.shutdown();
    std::cout << "线程池已优雅关闭\n";
    return 0;
}

