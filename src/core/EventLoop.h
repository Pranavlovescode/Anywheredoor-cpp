//
// Created by deilsy on 02-06-2026.
//

#ifndef ANYWHEREDOOR_EVENTLOOP_H
#define ANYWHEREDOOR_EVENTLOOP_H

#include <functional>
#include <queue>
#include <mutex>
#include <string>
#include <thread>


namespace event {
    struct Task {
        std::string name; // name of the task
        std::function<void()> callback_fun; // the function it will perform
    };

    class EventLoop {

    public:
        void schedule(const Task& task);
        void loop();
        void wait();
        void stop();
        void start();
    private:
        /**
         * A simple thread-safe queue to hold scheduled tasks. In a production implementation, you might want to use a more robust concurrent queue or add condition variables for better efficiency.
         */
        std::queue<Task> event_queue;
        std::thread loop_thread;
        std::mutex queue_mutex;
    };
} // namespace event

#endif //ANYWHEREDOOR_EVENTLOOP_H
