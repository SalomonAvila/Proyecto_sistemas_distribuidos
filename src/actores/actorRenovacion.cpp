#include <zmq.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include "../../utils/structs.cpp"

std::atomic<bool> primaryGaAlive(true);
std::atomic<bool> isRunning(true);
std::mutex gaAddressMutex;
std::string currentGaAddress;

void obtainEnvData(std::vector<std::string> &environmentVariables){
    std::fstream configFile("../.env");
    std::string key, value;
    while (std::getline(configFile, key, '=') && std::getline(configFile, value)) {
        environmentVariables.push_back(value);
    }
}

void monitorGaHeartbeat(zmq::context_t &context, const std::string &primaryIp, const std::string &secondaryIp){
    zmq::socket_t heartbeatSocket(context, zmq::socket_type::sub);
    std::string heartbeatEndpoint = "tcp://" + primaryIp + ":5562";
    heartbeatSocket.connect(heartbeatEndpoint);
    heartbeatSocket.set(zmq::sockopt::subscribe, "");
    heartbeatSocket.set(zmq::sockopt::rcvtimeo, 5000);
    
    std::cout << "[AR-Heartbeat] Monitoring primary GA at " << heartbeatEndpoint << "\n";
    
    int missedHeartbeatCount = 0;
    const int maxMissedHeartbeats = 3;
    
    while(isRunning){
        zmq::message_t heartbeatMessage;
        zmq::recv_result_t receiveResult = heartbeatSocket.recv(heartbeatMessage, zmq::recv_flags::none);
        
        if(receiveResult && heartbeatMessage.size() > 0){
            missedHeartbeatCount = 0;
            if(!primaryGaAlive){
                std::cout << "\n[AR-Recovery] Primary GA is back, switching\n\n";
                {
                    std::lock_guard<std::mutex> lock(gaAddressMutex);
                    currentGaAddress = "tcp://" + primaryIp + ":5560";
                }
                primaryGaAlive = true;
            }
        } else {
            missedHeartbeatCount++;
            if(missedHeartbeatCount >= maxMissedHeartbeats && primaryGaAlive){
                std::cout << "\n[AR-Failover] Primary GA down, switching to secondary\n\n";
                {
                    std::lock_guard<std::mutex> lock(gaAddressMutex);
                    currentGaAddress = "tcp://" + secondaryIp + ":5560";
                }
                primaryGaAlive = false;
            }
        }
    }
    std::cout << "[AR-Heartbeat] Monitor stopped\n";
}

std::string getCurrentGaAddress(){
    std::lock_guard<std::mutex> lock(gaAddressMutex);
    return currentGaAddress;
}

std::string sendRequestWithFailover(zmq::message_t& requestMessage, zmq::context_t& context){
    int maxRetryAttempts = 3;
    int baseDelayMs = 200;
    int maxDelayMs = 1000;
    int socketTimeoutMs = 2000;
    
    for(int attemptNumber = 0; attemptNumber < maxRetryAttempts; attemptNumber++){
        try {
            zmq::socket_t gaSocket(context, zmq::socket_type::req);
            gaSocket.set(zmq::sockopt::rcvtimeo, socketTimeoutMs);
            gaSocket.set(zmq::sockopt::sndtimeo, socketTimeoutMs);
            gaSocket.connect(getCurrentGaAddress());

            zmq::message_t requestCopy(requestMessage.size());
            memcpy(requestCopy.data(), requestMessage.data(), requestMessage.size());
            gaSocket.send(requestCopy, zmq::send_flags::none);

            zmq::message_t gaResponse;
            zmq::recv_result_t receiveResult = gaSocket.recv(gaResponse, zmq::recv_flags::none);
            gaSocket.close();
            
            if(receiveResult && gaResponse.size() > 0){
                return std::string(static_cast<char*>(gaResponse.data()), gaResponse.size());
            }
        } catch (const zmq::error_t& error) {
            std::cerr << "[AR-Error] ZMQ error on attempt " << (attemptNumber + 1) << ": " << error.what() << "\n";
        }

        if(attemptNumber < maxRetryAttempts - 1){
            int delayMs = std::min(baseDelayMs * (1 << attemptNumber), maxDelayMs);
            std::cout << "[AR-Retry] Request failed, retrying in " << delayMs << " ms\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
    }
    std::cerr << "[AR-Error] All " << maxRetryAttempts << " attempts failed\n";
    return "ERROR";
}

int main(int argc, char* argv[]){
    std::vector<std::string> ipAddressList;
    std::int8_t locationIndex;
    
    if(argc == 1){
        std::cout << "[AR-Error] Cannot establish connection without IP\n";
        return 0;
    } else if(argc == 2){
        obtainEnvData(ipAddressList);
        locationIndex = std::int8_t(std::stoi(argv[1])) - 1;
        
        if (locationIndex >= std::int8_t(ipAddressList.size())){
            std::cout << "[AR-Error] This location does not exist\n";
            return 0;
        }
    }

    std::cout << "========================================\n";
    std::cout << "    RENEWAL ACTOR (AR) - STARTING\n";
    std::cout << "========================================\n";
    
    zmq::context_t zmqContext(1);
    
    zmq::socket_t gcSocket(zmqContext, zmq::socket_type::rep);
    std::string gcEndpoint = "tcp://";
    gcEndpoint.append(ipAddressList[locationIndex]);
    gcEndpoint.append(":5558");
    gcSocket.bind(gcEndpoint);
    
    std::cout << "[AR] Listening on " << gcEndpoint << "\n";

    {
        std::lock_guard<std::mutex> lock(gaAddressMutex);
        currentGaAddress = "tcp://" + ipAddressList[0] + ":5560";
    }
    
    std::cout << "[AR] Primary GA: tcp://" << ipAddressList[0] << ":5560\n";
    std::cout << "[AR] Secondary GA: tcp://" << ipAddressList[1] << ":5560\n";
    
    std::thread heartbeatThread(monitorGaHeartbeat, std::ref(zmqContext), 
                         std::ref(ipAddressList[0]), std::ref(ipAddressList[1]));
    
    std::cout << "[AR] Ready to process RENEWAL requests\n\n";

    while(true){
        zmq::message_t gcRequest;
        zmq::recv_result_t receiveResult = gcSocket.recv(gcRequest, zmq::recv_flags::none);
        
        if(!receiveResult || gcRequest.size() == 0){
            continue;
        }
        
        Request parsedRequest;
        memcpy(&parsedRequest, gcRequest.data(), sizeof(Request));
        
        std::cout << "[AR] Request received from GC:\n";
        std::cout << "[AR] Type: RENEWAL\n";
        std::cout << "[AR] Book code: " << parsedRequest.code << "\n";
        std::cout << "[AR] Location: " << int(parsedRequest.location) << "\n";

        std::string gaResponse = sendRequestWithFailover(gcRequest, zmqContext);
        
        if(gaResponse == "ERROR"){
            gaResponse = "Error: Could not process renewal operation";
        }
        
        std::cout << "[AR] Sending response to GC: " << gaResponse << "\n\n";
        gcSocket.send(zmq::buffer(gaResponse), zmq::send_flags::none);
    }
    
    isRunning = false;
    heartbeatThread.join();
    return 0;
}