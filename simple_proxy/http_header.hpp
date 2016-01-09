//
//  http_header.hpp
//  simple_proxy
//
//  Created by Vladislav Sazanovich on 26.12.15.
//  Copyright © 2015 ZeRoGerc. All rights reserved.
//

#ifndef http_header_hpp
#define http_header_hpp

#include <stdio.h>
#include <string>

struct http_header {
    enum class State {READING, READED, COMPLETE};
    enum class Type {UNDEFINED, HEADER, CHUNKED, CONTENT};

    http_header (http_header const&) = delete;
    http_header& operator=(http_header const&) = delete;

    http_header() {}

    http_header(std::string initial) {
        append(initial);
    }
    
    void append(std::string const& chunk);

    void clear() {
        data.clear();
        type = Type::UNDEFINED;
        state = State::READING;
        content_length = 0;
        sz = 0;
    }

    inline size_t size() const {
        if (!(state == State::COMPLETE)) {
             return data.size();
        } else {
            return sz;
        }
    }

    inline Type get_type() const {
        return type;
    }
    
    inline State get_state() const {
        return state;
    }

    inline int get_content_length() const {
        return content_length;
    }

    inline std::string get_string_representation() const {
        return data;
    }
    
    bool is_valid() const;

    void init_properties();
    
    std::string retrieve_host() const;
    size_t retrieve_port() const;
    
private:
    std::string data;
    Type type = Type::UNDEFINED;
    State state = State::READING;
    
    size_t content_length = 0;
    size_t sz = 0;
    
    void transform_to_relative();
    void parse_content_length();
    void parse_is_chunked_encoding();
};

#endif /* http_header_hpp */