#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool {
public:
    explicit ThreadPool(int n_threads) {
        for (int i = 0; i < n_threads; ++i) {
            m_workers.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock lock(m_mutex);
                        m_cv.wait(lock, [this] {
                            return m_stop || !m_queue.empty();
                        });
                        if (m_stop && m_queue.empty()) return;
                        task = std::move(m_queue.front());
                        m_queue.pop();
                    }
                    task();
                    --m_pending;
                }
            });
        }
    }

    ~ThreadPool() {
        { std::unique_lock lock(m_mutex); m_stop = true; }
        m_cv.notify_all();
        for (auto& t : m_workers) t.join();
    }

    void enqueue(std::function<void()> task) {
        ++m_pending;
        { std::unique_lock lock(m_mutex); m_queue.push(std::move(task)); }
        m_cv.notify_one();
    }

    int pending() const { return m_pending.load(); }

private:
    std::vector<std::thread>          m_workers;
    std::queue<std::function<void()>> m_queue;
    std::mutex                        m_mutex;
    std::condition_variable           m_cv;
    std::atomic<int>                  m_pending{0};
    bool                              m_stop{false};
};
