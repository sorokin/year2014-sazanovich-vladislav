//
// Created by Vladislav Sazanovich on 06.12.15.
//

#include <sys/socket.h>
#include <assert.h>
#include "event_queue.hpp"
#include "custom_exception.hpp"

background_tasks_handler::background_tasks_handler(): work(true) {
    for (int i = 0; i < THREADS_AMOUNT; i++) {
        // TODO: what will happen if constructor of std::thread fails
        // or push_back fails?
        threads.push_back(std::thread(
                                      [this](){
                                          execute();
                                      }
                          ));
    }
}


background_tasks_handler::~background_tasks_handler() {
    stop();
}


void background_tasks_handler::push(task t) {
    std::unique_lock<std::mutex> lock(mutex);
    poll.push(t);
    
    condition.notify_one();
}


void background_tasks_handler::execute() {
    while(work) {
        std::unique_lock<std::mutex> lock(mutex);
        
        while (poll.size() == 0) {
            condition.wait(lock);
            if (!work) break;
        }
        
        if (!work) return;
        
        //We've got one
        task current = poll.front();
        poll.pop();
        
        lock.unlock();
        //execute
        current();
    }
}

void background_tasks_handler::stop() {
    work = false;
    condition.notify_all();
    
    for (int i = 0; i < THREADS_AMOUNT; i++)
        if (threads[i].joinable())
            threads[i].join();
}


event_queue::event_queue() {
    kq = kqueue();
    int fds[2];
    if (pipe(fds) == -1) {
        throw custom_exception("fail to create pipe fd");
    }
    pipe_in = fds[1];
    pipe_out = fds[0];
    
    main_thread_events_handler = handler {
        [this](struct kevent& event) {
            assert(main_thread_tasks.size() != 0);
            char buffer;
            read(pipe_out, &buffer, sizeof(buffer));
            
            std::unique_lock<std::mutex> locker{mutex};
            if (main_thread_tasks.size() != 0) {
                auto f = main_thread_tasks.back();
                main_thread_tasks.pop_back();
                locker.unlock();
                f();
            }
        }
    };
    
    add_event(pipe_out, EVFILT_READ, &main_thread_events_handler);
}

event_queue::~event_queue() {
    close(pipe_in);
    close(pipe_out);
}


void event_queue::delete_event(size_t ident, int16_t filter) {
    event(ident, filter, EV_DELETE, 0, 0, nullptr);
}


void event_queue::add_event(size_t ident, int16_t filter, handler* hand) {
    event(ident, filter, EV_ADD, 0, 0, hand);
}


void event_queue::execute_in_main(task t) {
    std::lock_guard<std::mutex> locker{mutex};
    main_thread_tasks.push_back(t);
    write(pipe_in, "T", 1);
}


void event_queue::execute_in_background(task t) {
    background_tasks.push(t);
}


int event_queue::occurred() {
    return kevent(kq, NULL, 0, evlist, SOMAXCONN, NULL);
}


void event_queue::execute(int amount) {
    deleted_events.clear();
    
//    std::cerr << "AMOUNT " << amount << "\n";

    for (int i = 0; i < amount; i++) {
//        std::cerr << "EVENT " << evlist[i].filter << ' ' << evlist[i].ident << "\n";
        
        if (deleted_events.size() == 0 || deleted_events.find(std::make_pair(evlist[i].ident, evlist[i].filter)) == deleted_events.end()) {
            handler* hand = static_cast<handler*>(evlist[i].udata);
            (*hand)(evlist[i]);
        }
    }
}


void event_queue::stop_resolve() {
    background_tasks.stop();
}


void event_queue::event(size_t ident, int16_t filter, uint16_t flags, uint32_t fflags, int64_t data, handler* hand) {
    struct kevent temp_event;
    
    EV_SET(&temp_event, ident, filter, flags, fflags, data, static_cast<void*>(hand));

    if (kevent(kq, &temp_event, 1, NULL, 0, NULL) == -1) {
        std::string message{"kevent fails: "};
        message.append(std::strerror(errno));
        throw custom_exception(message);
    }
    
    if ((flags & EV_DELETE) && (deleted_events.find(std::make_pair(ident, filter)) == deleted_events.end())) {
            deleted_events.insert(std::make_pair(ident, filter));
    }
}
