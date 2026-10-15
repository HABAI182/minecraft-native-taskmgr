#pragma once
#include "protocol.h"
#include <winsock2.h>
#include <string>
#include <atomic>
#include <thread>
struct Discovery{uint16_t port;DWORD pid;std::string token;};
Discovery ReadDiscovery();
class Network {
    SOCKET socket_=INVALID_SOCKET;HANDLE frame_=nullptr;SharedState* state_=nullptr;
    std::thread receive_,send_;std::atomic<bool> live_{false};
    void Receive();void Send();
public:
    std::atomic<int> received{0};
    Network(SharedState* state,const Discovery& discovery);
    ~Network();
    bool Live()const{return live_.load();}
};
