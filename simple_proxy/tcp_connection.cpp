//
//  tcp_connection.cpp
//  simple_proxy
//
//  Created by Vladislav Sazanovich on 06.12.15.
//  Copyright © 2015 ZeRoGerc. All rights reserved.
//

#include <stdio.h>
#include "ipv4_endpoint.hpp"
#include "tcp_connection.hpp"
#include "tcp_client.hpp"
#include "tcp_server.hpp"

const std::string buffer::chunked_end{"\r\n0\r\n\r\n"};

buffer::buffer(std::string chunk, int amount_of_data) {
    available_data = amount_of_data;
    tail = std::string();
    data = std::string();
    append(chunk);
}

void buffer::append(std::string chunk) {
    assert(available_data != 0);
    
    if (available_data != -1 && chunk.size() > available_data) {
        chunk = chunk.substr(0, available_data);
    }
    
    if (chunk.size() > chunked_end.size()) {
        tail = chunk.substr(chunk.size() - chunked_end.size());
    } else {
        tail += chunk;
        if (tail.size() > chunked_end.size()) {
            tail = tail.substr(tail.size() - chunked_end.size());
        }
    }
    
    if (available_data != -1) {
        available_data -= chunk.size();
    }
    data += chunk;
    
    if (tail == chunked_end && available_data == -1) {
        available_data = 0;
    }
}

std::string buffer::get(size_t amount) const {
    if (amount == -1) { //return all data by default
        return data;
    }
    
    if (amount >= data.size()) {
        std::string ret = data;
        return data;
    }
    
    std::string ret = data.substr(0, amount);
    return ret;
}

void buffer::pop_front(size_t amount) {
    assert(amount <= data.size());
    data = data.substr(amount);
}

void buffer::clear() {
    data.clear();
    available_data = 0;
    tail.clear();
}

size_t buffer::size() const {
    return data.size();
}

tcp_connection::tcp_connection(event_queue* q, int descriptor)
    : queue(q), client(new proxy_client(descriptor)), server() {}

void tcp_connection::start() {
    switch_state(State::RECEIVE_CLIENT);
}

bool tcp_connection::init_server(std::string const& ip, std::string const& host, size_t port) {
    try {
        //check if new server different to previous
        if (server && !(server->get_host() == host)) {
            safe_server_disconnect();
        }

        if (!server) {
            server.reset(new proxy_client(ip, host, port));
        }
        
        server->set_write_event(event_registration{
            queue,
            get_server_socket(),
            EVFILT_WRITE,
            handler{[this](struct kevent& event){
                this->handle_server_disconnect(event);
            }}
        });
        
    } catch (std::exception const& e) {
        std::cerr << std::strerror(errno) << '\n';
        //send BAD request
        safe_disconnect();
        return false;
    }
    return true;
}

tcp_connection::~tcp_connection() {}

void tcp_connection::safe_server_disconnect() {
    server.reset(nullptr);
}

void tcp_connection::safe_client_disconnect() {
    client.reset(nullptr);
}

void tcp_connection::safe_disconnect() {
    if (state == State::RESOLVE) {
        //while resolving in progress connection can't die
        safe_client_disconnect();
        safe_server_disconnect();
    } else {
        switch_state(State::DELETED);
    }
}

void tcp_connection::get_client_body(struct kevent &event) {
    if (handle_client_disconnect(event) || body_buffer.amount_of_available_data() == 0)
        return;

    std::string chunk = client->read(std::min(1024, static_cast<int>(body_buffer.amount_of_available_data())));
    body_buffer.append(chunk);
}

void tcp_connection::get_client_header(struct kevent &event) {
    if (handle_client_disconnect(event)) {
        std::cout << "read client disconnect";
        return;
    }

    std::string chunk = client->read(1024);

    std::cerr << "client " << chunk << "\n###################################\n";
    header.append(chunk);

    if (header.get_state() == http_header::State::COMPLETE) {
        switch_state(State::RESOLVE);
        
        task resolve {
                [this]() {
                    /*
                     during resolve connection couldn't die
                     but if we need to determine if connection is in valid state after resolve
                     we could check state of client
                     if client doesn't exist it means that connection die
                     */
                    std::string host = header.retrieve_host();
                    size_t port = header.retrieve_port();
                    // since we pass data as header + body
                    size_t content_len = header.get_content_length() + header.size();

                    try {
                        std::string ip = http_parse::get_ip_by_host(host);
                        queue->execute_in_main(task{[this, host, ip, port, content_len](){
                            if (!client) {
                                //if state is invalid just delete
                                switch_state(State::DELETED);
                                return;
                            }
                            if (!init_server(ip, host, port))
                                return;
                            
                            //here we have valid server and valid client
                            body_buffer = buffer(header.get_string_representation(), static_cast<int>(content_len));
                            
                            switch_state(State::SEND_SERVER);
                        }});
                    } catch (std::exception const& e) {
                        //send bad request in main thread
                        queue->execute_in_main(
                                               task{[this]()
                                                   {
                                                       switch_state(State::DELETED);
                                                       safe_disconnect();
                                                   }
                                               });
                    }
                }
        };

        //execute in background
        callback(resolve);
    }
}

void tcp_connection::handle_client_write(struct kevent& event) {
    if (handle_client_disconnect(event))
        return;

    
    size_t len = client->send(body_buffer.get());
    assert(len != -1);
    
    body_buffer.pop_front(len);
    
    //if server finish sending and client receive all available data
    if (body_buffer.size() == 0 && body_buffer.amount_of_available_data() == 0) {
        switch_state(State::RECEIVE_CLIENT); //start new request
    }
}

void tcp_connection::get_server_body(struct kevent &event) {
    if (handle_server_disconnect(event) || body_buffer.amount_of_available_data() == 0)
        return;

    std::string chunk = server->read(1024);
    body_buffer.append(chunk);
}

void tcp_connection::get_server_header(struct kevent &event) {
    if (handle_server_disconnect(event))
        return;

    std::string chunk = server->read(1024);

    std::cerr << "server " << chunk << "\n###################################\n";
    header.append(chunk);

    if (header.get_state() == http_header::State::COMPLETE) {
        
        if (header.get_type() == http_header::Type::CHUNKED) {
            body_buffer = buffer(header.get_string_representation(), -1);
            
        } else {
            body_buffer = buffer(header.get_string_representation(), static_cast<int>(header.get_content_length() + header.size()));
        }

        switch_state(State::SEND_CLIENT);
    }
}

void tcp_connection::handle_server_write(struct kevent& event) {
    if (handle_server_disconnect(event))
        return;
    
    
    size_t len = server->send(body_buffer.get());
    assert(len != -1);
    body_buffer.pop_front(len);
    
    //If we have already received all data from client and send it
    if (body_buffer.size() == 0 && body_buffer.amount_of_available_data() == 0) {
        switch_state(State::RECEIVE_SERVER);
    }
}

bool tcp_connection::handle_server_disconnect(struct kevent& event) {
    if (state == State::DELETED) {
        return true;
    }
    if ((event.flags & EV_EOF) && (event.data == 0)) {
        std::cerr << "server handle disconnected" << std::endl;
        safe_server_disconnect();
        return true;
    }
    return false;
}

bool tcp_connection::handle_client_disconnect(struct kevent& event) {
    if (state == State::DELETED) {
        return true;
    }
    if ((event.flags & EV_EOF) && (event.data == 0)) {
        std::cerr << "client handle disconnected" << std::endl;
        safe_disconnect();
        return true;
    }
    return false;
}

void tcp_connection::switch_state(State new_state) {
    static auto client_disconnect = [this](struct kevent &event) {
        this->handle_client_disconnect(event);
    };
    
    static auto server_disconnect = [this](struct kevent &event) {
        this->handle_server_disconnect(event);
    };
    
    state = new_state;
    std::cout << "switch_state" << static_cast<int>(new_state) << std::endl;
    switch (new_state) {
        case State::RECEIVE_CLIENT:
            header.clear();
            body_buffer.clear();
            
            if (server) {
                //listen only for disconnect
                set_write_function(server, server_disconnect);
            }
            
            client->stop_write();
            set_read_function(
                              client,
                              handler{
                                  [this](struct kevent &event) {
                                      get_client_header(event);
                                  }
                              }
                              );
            
            client->resume_read();
            break;
        case State::RESOLVE:
            if (client) {
                //listen for disconnect
                client->stop_listen();
                set_write_function(client, client_disconnect);
                client->resume_write();
            }
            if (server) {
                //ditto
                server->stop_listen();
                set_write_function(server, server_disconnect);
                server->resume_write();
            }
            break;
        case State::SEND_SERVER:
            set_write_function(
                               server,
                               handler{
                                   [this](struct kevent &event) {
                                       handle_server_write(event);
                                   }
                               }
                               );
            
            set_read_function(
                              client,
                              handler{
                                      [this](struct kevent &event) {
                                          get_client_body(event);
                                      }
                              }
            );
            
            //start transfer data
            client->resume_read();
            server->resume_write();
            break;
        case State::RECEIVE_SERVER:
            header.clear();
            body_buffer.clear();
            
            set_write_function(client, client_disconnect);
            server->stop_listen();
            set_read_function(
                              server,
                              handler{
                                  [this](struct kevent &event) {
                                      get_server_header(event);
                                  }
                              }
                              );
            
            server->resume_read();
            break;
        case State::SEND_CLIENT:
            set_write_function(
                               client,
                               [this](struct kevent &event) {
                                   handle_client_write(event);
                               }
                               );
            
            set_read_function(
                              server,
                              [this](struct kevent &event) {
                                  get_server_body(event);
                              }
                              );
            
            //start transfer data
            server->resume_read();
            client->resume_write();
            break;
        default:
            safe_client_disconnect();
            safe_server_disconnect();
    }
}

void tcp_connection::set_read_function(std::unique_ptr<proxy_client>& object, handler hand) {
    assert(object);
    if (object->get_event_read().is_valid()) {
        object->get_event_read().stop_listen();
        object->get_event_read().change_function(std::move(hand));
    } else {
        object->set_read_event(
                           event_registration{
                               queue,
                               object->get_socket(),
                               EVFILT_READ,
                               std::move(hand),
                           }
                           );
    }
}

void tcp_connection::set_write_function(std::unique_ptr<proxy_client>& object, handler hand) {
    assert(object);
    if (object->get_event_write().is_valid()) {
        object->get_event_write().stop_listen();
        object->get_event_write().change_function(std::move(hand));
    } else {
        object->set_write_event(
                               event_registration{
                                   queue,
                                   object->get_socket(),
                                   EVFILT_WRITE,
                                   std::move(hand),
                                   true
                               }
                               );
    }
}

void tcp_connection::set_callback(resolver _callback) {
    this->callback = _callback;
}

void tcp_connection::set_deleter(std::function<void()> del) {
    this->deleter = del;
}