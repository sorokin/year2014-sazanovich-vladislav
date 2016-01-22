//
//  main.cpp
//  simple_proxy
//
//  Created by Vladislav Sazanovich on 28.11.15.
//  Copyright © 2015 ZeRoGerc. All rights reserved.
//

#include <iostream>
#include <thread>
#include <signal.h>

#include "socket.hpp"
#include "main_server.hpp"
#include "socket.hpp"
#include "tcp_connection.hpp"
#include "event_queue.hpp"
#include "proxy.hpp"
#include "listener.hpp"

int main(int argc, const char * argv[]) {
//    sigset_t mask;
//    sigemptyset(&mask);
//    sigaddset(&mask,SIGPIPE);
//    sigprocmask(SIG_BLOCK, &mask, NULL);
    
    main_server server(2538);
    event_queue kq(server.get_socket());
    proxy proxy_server{&kq, server.get_socket()};

    proxy_server.main_loop();
}
