//
//  tcp_pair.hpp
//  simple_proxy
//
//  Created by Vladislav Sazanovich on 28.11.15.
//  Copyright © 2015 ZeRoGerc. All rights reserved.
//

#ifndef tcp_connection_hpp
#define tcp_connection_hpp

#include <stdio.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "event_queue.hpp"
#include "http_parse.hpp"
#include "http_data.hpp"
#include "tasks_pull.hpp"

struct tcp_client;
struct tcp_server;
struct tcp_connection {
private:
    static const size_t BUFFER_SIZE = 1024;
    
    tcp_client* client;
    tcp_server* server;
    event_queue* queue;
    
    handler server_read;
    handler server_write;
    handler client_read;
    handler client_write;
    
    resolver callback;
    
    http_data buffer;

public:
    friend struct tcp_server;
    friend struct tcp_client;
    
    //Don't forget to set callback and client_handler after constructor
    tcp_connection(event_queue* queue, int descriptor);

    void init_server(event_queue* q);

    void finish_server_initialization(event_queue* queue);
    
    ~tcp_connection();

    void set_client_handler(handler handler);
    
    int get_client_socket();

    int get_server_socket();
    
    handler* get_client_handler();
    
    void set_callback(resolver callback);
    
    void safe_server_disconnect();
    
    void safe_client_disconnect();
    
    void safe_disconnect();
    
    void get_http_request(struct kevent& event);

    void handle_client_read(struct kevent& event);
    
    void handle_client_write(struct kevent& event, std::string response);

    void handle_server_read(struct kevent& event);

    void handle_server_write(struct kevent& event);
};

#endif /* tcp_pair_hpp */