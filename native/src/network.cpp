#include "network.h"
#include <ws2tcpip.h>
#include <fstream>
#include <filesystem>
#include <regex>
#include <vector>
#include <array>
#include <stdexcept>
#include <cstring>
namespace {
void Append(std::vector<uint8_t>& p,int32_t value){uint32_t x=htonl(uint32_t(value));auto b=reinterpret_cast<uint8_t*>(&x);p.insert(p.end(),b,b+4);}
void Append(std::vector<uint8_t>& p,double value){uint64_t x=0;memcpy(&x,&value,8);for(int i=7;i>=0;i--)p.push_back(uint8_t(x>>(i*8)));}
bool Write(SOCKET socket,const void* data,int size){auto p=static_cast<const char*>(data);while(size){int n=send(socket,p,size,0);if(n<=0)return false;p+=n;size-=n;}return true;}
bool Read(SOCKET socket,void* data,int size){auto p=static_cast<char*>(data);while(size){int n=recv(socket,p,size,0);if(n<=0)return false;p+=n;size-=n;}return true;}
int32_t Int(const uint8_t* p){uint32_t x;memcpy(&x,p,4);return int32_t(ntohl(x));}
bool Packet(SOCKET socket,const std::vector<uint8_t>& p){uint32_t n=htonl(uint32_t(p.size()));return Write(socket,&n,4)&&Write(socket,p.data(),int(p.size()));}
}
Discovery ReadDiscovery(){
    wchar_t local[32768]{};DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
    if(!n||n>=32768)throw std::runtime_error("LOCALAPPDATA unavailable");
    std::ifstream stream(std::filesystem::path(local)/L"MCNativeTaskmgr/session.json",std::ios::binary);
    std::string json((std::istreambuf_iterator<char>(stream)),{});std::smatch port,pid,token;
    if(json.size()>2048||!std::regex_search(json,port,std::regex("\"port\":([0-9]+)"))||
       !std::regex_search(json,pid,std::regex("\"pid\":([0-9]+)"))||
       !std::regex_search(json,token,std::regex("\"token\":\"([0-9a-f]{64})\"")))
        throw std::runtime_error("Game mod session not found");
    auto p=std::stoul(port[1]),id=std::stoul(pid[1]);
    if(p==0||p>65535||id==0)throw std::runtime_error("Invalid game session");
    return{uint16_t(p),DWORD(id),token[1]};
}
Network::Network(SharedState* state,const Discovery& discovery):state_(state){
    frame_=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,state->frameMutex);
    if(!frame_)throw std::runtime_error("Frame mutex unavailable");
    socket_=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(socket_==INVALID_SOCKET){CloseHandle(frame_);frame_=nullptr;throw std::runtime_error("Socket unavailable");}
    DWORD timeout=5000;setsockopt(socket_,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
    setsockopt(socket_,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
    int noDelay=1;setsockopt(socket_,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char*>(&noDelay),sizeof(noDelay));
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(discovery.port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    std::string hello="MCNTM1 "+discovery.token+"\n";
    if(connect(socket_,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0||!Write(socket_,hello.data(),int(hello.size()))){
        closesocket(socket_);socket_=INVALID_SOCKET;CloseHandle(frame_);frame_=nullptr;throw std::runtime_error("Cannot connect to game bridge");
    }
    live_=true;receive_=std::thread(&Network::Receive,this);send_=std::thread(&Network::Send,this);
}
Network::~Network(){
    live_=false;if(socket_!=INVALID_SOCKET)shutdown(socket_,SD_BOTH);
    if(send_.joinable())send_.join();if(receive_.joinable())receive_.join();
    if(socket_!=INVALID_SOCKET)closesocket(socket_);if(frame_)CloseHandle(frame_);
}
void Network::Receive(){
    std::array<uint8_t,28+kPixels*3> payload{};
    while(live_){
        uint32_t networkLength=0;if(!Read(socket_,&networkLength,4))break;
        int length=int(ntohl(networkLength));if(length<4||length>int(payload.size())||!Read(socket_,payload.data(),length))break;
        if(Int(payload.data())==2){if(length>4096)break;continue;}
        if(length!=int(payload.size()))break;
        if(Int(payload.data())!=1||Int(payload.data()+8)!=kWidth||Int(payload.data()+12)!=kHeight)break;
        if(WaitForSingleObject(frame_,100)!=WAIT_OBJECT_0)break;
        state_->sourceWidth=Int(payload.data()+16);state_->sourceHeight=Int(payload.data()+20);state_->frameFlags=Int(payload.data()+24);
        memcpy(state_->rgb,payload.data()+28,kPixels*3);state_->frameSequence=Int(payload.data()+4);ReleaseMutex(frame_);++received;
    }
    live_=false;shutdown(socket_,SD_BOTH);
}
void Network::Send(){
    uint64_t heartbeat=0;
    while(live_){
        if(state_->inputOverflow)break;
        LONG read=InterlockedCompareExchange(&state_->inputRead,0,0),write=InterlockedCompareExchange(&state_->inputWrite,0,0);
        if(read!=write){
            MemoryBarrier();InputEvent e=state_->input[uint32_t(read)%kInputCapacity];
            std::vector<uint8_t> p;Append(p,e.type);
            switch(e.type){
            case 10:Append(p,e.a);Append(p,e.b);Append(p,e.c);Append(p,e.d);break;
            case 11:Append(p,e.a);Append(p,e.b);Append(p,e.c);break;
            case 12:Append(p,e.x);Append(p,e.y);Append(p,e.z);Append(p,e.w);break;
            case 13:Append(p,e.x);Append(p,e.y);break;
            case 14:Append(p,e.a);Append(p,e.b);break;
            case 15:Append(p,e.a);break;
            default:break;
            }
            if(!Packet(socket_,p))break;InterlockedExchange(&state_->inputRead,read+1);
        }else if(GetTickCount64()-heartbeat>500){
            std::vector<uint8_t> p;Append(p,0);if(!Packet(socket_,p))break;heartbeat=GetTickCount64();
        }else Sleep(2);
    }
    live_=false;shutdown(socket_,SD_BOTH);
}
