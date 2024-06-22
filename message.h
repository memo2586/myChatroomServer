#ifndef MESSAGE_H
#define MESSAGE_H

#include <iostream>
#include <string.h>

struct message_t {
public:
    void set_username(char* username);
    void set_username(std::string username);
    const char* get_username();
    void set_message(char* msg);
    void set_message(std::string msg);
    const char* get_message();
    void set_room_number(int number);
    int get_room_number();
    void empty();
    bool is_empty();

public:
    message_t() {}
    message_t(char* username, char* msg) {
        
    }
    message_t(std::string username, std::string msg) {
        strncpy(m_username, username.c_str(), MAX_USERNAME_SIZE);
        strncpy(m_data, msg.c_str(), MAX_MESSAGE_SIZE);
    }
    ~message_t() {}

private:
    static const int MAX_USERNAME_SIZE = 32;
    static const int MAX_MESSAGE_SIZE = 988;
    int m_room_number;
    char m_username[MAX_USERNAME_SIZE];
    char m_data[MAX_MESSAGE_SIZE];
};

void message_t::set_username(char* username) {
    strncpy(m_username, username, MAX_USERNAME_SIZE);
}

void message_t::set_username(std::string username) {
    strncpy(m_username, username.c_str(), MAX_USERNAME_SIZE);
}

const char* message_t::get_username() {
    return m_username;
}

void message_t::set_message(char* msg) {
    strncpy(m_data, msg, MAX_MESSAGE_SIZE);
}

void message_t::set_message(std::string msg) {
    strncpy(m_data, msg.c_str(), MAX_MESSAGE_SIZE);
}

const char* message_t::get_message() {
    return m_data;
}

void message_t::set_room_number(int number) {
    m_room_number = number;
}

int message_t::get_room_number() {
    return m_room_number;
}

void message_t::empty() {
    memset(m_username, '\0', MAX_USERNAME_SIZE);
    memset(m_data, '\0', MAX_MESSAGE_SIZE);
}

bool message_t::is_empty() {
    return m_data[0] == '\0';
}

#endif
