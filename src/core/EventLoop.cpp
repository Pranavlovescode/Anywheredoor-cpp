//
// Created by deilsy on 02-06-2026.
//

#include "EventLoop.h"

#include <algorithm>
#include <condition_variable>

namespace event {
    std::condition_variable cv;
    std::atomic<bool> running{true};
    void EventLoop::loop() {
        while (running) {
            Task task;
            {
                std::unique_lock lock(queue_mutex);

                cv.wait(lock, [&]{ return !event_queue.empty() || !running; });

                if (!running && event_queue.empty()) {
                    break; // Exit if stopped and no tasks remain
                }
                task = event_queue.front();
                event_queue.pop();
            }
            // calling the callback function of the task
            if (task.callback_fun) {
                task.callback_fun();
            } else {
                // No tasks, sleep briefly to avoid busy-waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    void EventLoop::schedule(const Task& task) {

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            event_queue.push(task);
        }
        cv.notify_one();
    }

    void EventLoop::wait() {
        if (loop_thread.joinable()) {
            loop_thread.join();
        }
    }

    void EventLoop::stop(){
        running = false;
        cv.notify_all();
    }

    void EventLoop::start() {
        loop_thread = std::thread(&EventLoop::loop, this);
    }
} // event