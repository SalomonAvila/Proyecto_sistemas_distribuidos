#include <zmq.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include "../../utils/structs.cpp"

std::atomic<bool> primaryAlive(true);
std::atomic<bool> running(true);
std::mutex gaAddressMutex;
std::string currentGaAddress;

void obtainEnvData(std::vector<std::string> &v){
    std::fstream f("../.env");
    std::string key,val;
    while (std::getline(f, key, '=') && std::getline(f, val)) {
        v.push_back(val);
    }
}

void heartbeatMonitor(zmq::context_t &context, const std::string &primaryAddr, const std::string &secondaryAddr){
    zmq::socket_t heartbeatSocket(context, zmq::socket_type::sub);
    std::string heartbeatEndpoint = "tcp://" + primaryAddr + ":5562";
    heartbeatSocket.connect(heartbeatEndpoint);
    heartbeatSocket.set(zmq::sockopt::subscribe, "");
    heartbeatSocket.set(zmq::sockopt::rcvtimeo, 5000);
    
    std::cout << "[HB Monitor] Monitoring " << heartbeatEndpoint << std::endl;
    
    int missedHeartbeats = 0;
    const int MAX_MISSED = 3;
    
    while(running){
        zmq::message_t msg;
        zmq::recv_result_t result = heartbeatSocket.recv(msg, zmq::recv_flags::none);
        
        if(result && msg.size() > 0){
            missedHeartbeats = 0;
            if(!primaryAlive){
                std::cout << "\n✓ PRIMARY RECOVERED!\n\n";
                std::lock_guard<std::mutex> lock(gaAddressMutex);
                currentGaAddress = "tcp://" + primaryAddr + ":5560";
                primaryAlive = true;
            }
        } else {
            missedHeartbeats++;
            if(missedHeartbeats >= MAX_MISSED && primaryAlive){
                std::cout << "\n⚠️  PRIMARY FAILED! FAILOVER...\n\n";
                std::lock_guard<std::mutex> lock(gaAddressMutex);
                currentGaAddress = "tcp://" + secondaryAddr + ":5560";
                primaryAlive = false;
            }
        }
    }
}

std::string getCurrentGaAddress(){
    std::lock_guard<std::mutex> lock(gaAddressMutex);
    return currentGaAddress;
}

std::string sendRequestWithFailover(zmq::message_t& message, zmq::context_t& context){
    for(int attempt = 0; attempt < 3; attempt++){
        try {
            zmq::socket_t socket(context, zmq::socket_type::req);
            socket.set(zmq::sockopt::rcvtimeo, 3000);
            socket.set(zmq::sockopt::sndtimeo, 3000);
            socket.connect(getCurrentGaAddress());

            zmq::message_t msgCopy(message.size());
            memcpy(msgCopy.data(), message.data(), message.size());
            socket.send(msgCopy, zmq::send_flags::none);

            zmq::message_t response;
            if(socket.recv(response, zmq::recv_flags::none) && response.size() > 0){
                socket.close();
                return std::string(static_cast<char*>(response.data()), response.size());
            }
            socket.close();
        } catch (const zmq::error_t& e) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return "ERROR: No GA available";
}

int main(int argc, char* argv[]){
    std::vector<std::string> directionsPool;
    if(argc != 2) return 0;
    
    obtainEnvData(directionsPool);
    std::int8_t loc = std::int8_t(std::stoi(argv[1])) - 1;
    
    std::cout << "======== ACTOR PRÉSTAMO ========\n";
    
    zmq::context_t context(1);
    zmq::socket_t socketOne(context, zmq::socket_type::rep);
    std::string addr = "tcp://" + directionsPool[loc] + ":5556";
    socketOne.bind(addr);

    {
        std::lock_guard<std::mutex> lock(gaAddressMutex);
        currentGaAddress = "tcp://" + directionsPool[0] + ":5560";
    }
    
    std::thread hbThread(heartbeatMonitor, std::ref(context), 
                         std::ref(directionsPool[0]), std::ref(directionsPool[1]));
    
    std::cout << "[Actor] Ready\n\n";

    while(true){
        zmq::message_t request;
        socketOne.recv(request, zmq::recv_flags::none);
        Request req;
        memcpy(&req, request.data(), sizeof(Request));
        std::string response = sendRequestWithFailover(request, context);
        std::cout << "Solicitud recibida:\n";
        std::cout << " - Tipo: " << int(req.requestType) << "\n";
        std::cout << " - Código libro: " << req.code << "\n";
        std::cout << " - Sede: " << int(req.location) << "\n";
        socketOne.send(zmq::buffer(response), zmq::send_flags::none);
    }
    
    running = false;
    hbThread.join();
    return 0;
}