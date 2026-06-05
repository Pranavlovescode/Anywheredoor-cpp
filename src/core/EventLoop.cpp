//
// Created by deilsy on 02-06-2026.
//

#include "EventLoop.h"
#include "spdlog/spdlog.h"

namespace event {

    EventLoop::EventLoop() {
        running = true;
        loop_thread = std::thread(&EventLoop::loop, this);
    }

    EventLoop::~EventLoop() {
        stop();
        wait();
    }
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
            spdlog::info("[EventLoop] Running task: {}", task.name);
            // calling the callback function of the task
            try {
                if (task.callback_fun) task.callback_fun();
            } catch (const std::exception& e) {
                spdlog::error("[EventLoop] Task '{}' threw: {}", task.name, e.what());
            }
            spdlog::info("[EventLoop] Completed task: {}", task.name);
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

} // event