//
// Created by Vladislav Sazanovich on 07.01.16.
//

#ifndef SIMPLE_PROXY_EVENT_REGISTRATION_H
#define SIMPLE_PROXY_EVENT_REGISTRATION_H

#include "event_queue.hpp"

struct event_registration
{
    event_registration();

    event_registration(event_queue* queue, size_t ident, int16_t filter, handler h, bool listen=false);

    event_registration(event_registration const&) = delete;
    event_registration& operator=(event_registration const&) = delete;

    event_registration(event_registration&& other);
    event_registration& operator=(event_registration&& other);

    ~event_registration() {
        stop_listen();
    }

    void stop_listen() {
        if (is_listened && ident != -1) {
            queue->delete_event(ident, filter);
            is_listened = false;
        }
    }

    void resume_listen() {
        if (!is_listened && ident != -1) {
            queue->add_event(ident, filter, &handler_);
            is_listened = true;
        }
    }

    void change_function(handler hand) {
        handler_ = std::move(hand);
        if (is_listened)
            queue->add_event(ident, filter, &handler_);
    }
    
    bool is_valid() const {
        return ident != -1;
    }

private:
    event_queue* queue;
    handler handler_;
    size_t ident = -1;
    int16_t filter;

    bool is_listened=false;
};



#endif //SIMPLE_PROXY_EVENT_REGISTRATION_H