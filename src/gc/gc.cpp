#include <zmq.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <cstring>
#include "../../utils/structs.cpp"

void obtainEnvData(std::vector<std::string> &environmentVariables){
    std::fstream configFile("../.env");
    std::string key, value;
    while (std::getline(configFile, key, '=') && std::getline(configFile, value)) {
        environmentVariables.push_back(value);
    }
}

std::string sendSynchronousRequest(Request &request, zmq::socket_t &actorSocket){
    zmq::message_t requestMessage(sizeof(Request));
    memcpy(requestMessage.data(), &request, sizeof(Request));
    actorSocket.send(requestMessage, zmq::send_flags::none);
    
    zmq::message_t responseMessage;
    actorSocket.recv(responseMessage, zmq::recv_flags::none);
    
    return std::string(static_cast<char*>(responseMessage.data()), responseMessage.size());
}

int main(int argc, char* argv[]){
    std::vector<std::string> ipAddressList;
    std::int8_t locationIndex;
    
    if (argc == 1){
        std::cout << "[GC-Error] Cannot establish connection without IP\n";
        return 0;
    } else if (argc == 2){
        obtainEnvData(ipAddressList);
        locationIndex = std::int8_t(std::stoi(argv[1])) - 1;
        
        if (locationIndex >= std::int8_t(ipAddressList.size())){
            std::cout << "[GC-Error] This location does not exist\n";
            return 0;
        }
    }
    
    std::cout << "========================================\n";
    std::cout << "  LOAD MANAGER (GC) - STARTING\n";
    std::cout << "========================================\n";

    zmq::context_t zmqContext(1);
    
    zmq::socket_t clientSocket(zmqContext, zmq::socket_type::rep);
    std::string clientEndpoint = "tcp://";
    clientEndpoint.append(ipAddressList[locationIndex]);
    clientEndpoint.append(":5555");
    clientSocket.bind(clientEndpoint);
    std::cout << "[GC] Listening for PS on " << clientEndpoint << "\n";

    zmq::socket_t loanActorSocket(zmqContext, zmq::socket_type::req);
    std::string loanActorEndpoint = "tcp://";
    loanActorEndpoint.append(ipAddressList[locationIndex]);
    loanActorEndpoint.append(":5556");
    loanActorSocket.connect(loanActorEndpoint);
    std::cout << "[GC] Connected to Loan Actor on port 5556\n";

    zmq::socket_t renewalActorSocket(zmqContext, zmq::socket_type::req);
    std::string renewalActorEndpoint = "tcp://";
    renewalActorEndpoint.append(ipAddressList[locationIndex]);
    renewalActorEndpoint.append(":5558");
    renewalActorSocket.connect(renewalActorEndpoint);
    std::cout << "[GC] Connected to Renewal Actor on port 5558\n";

    zmq::socket_t returnActorSocket(zmqContext, zmq::socket_type::req);
    std::string returnActorEndpoint = "tcp://";
    returnActorEndpoint.append(ipAddressList[locationIndex]);
    returnActorEndpoint.append(":5557");
    returnActorSocket.connect(returnActorEndpoint);
    std::cout << "[GC] Connected to Return Actor on port 5557\n";

    std::cout << "[GC] Ready to process requests\n\n";

    while(true){
        zmq::message_t clientRequest;
        zmq::recv_result_t receiveResult = clientSocket.recv(clientRequest, zmq::recv_flags::none);
        
        if(!receiveResult || clientRequest.size() == 0){
            continue;
        }
        
        Request parsedRequest;
        memcpy(&parsedRequest, clientRequest.data(), sizeof(Request));
        
        std::cout << "[GC] Request received from PS:\n";
        std::cout << "[GC] Type: " << int(parsedRequest.requestType) << "\n";
        std::cout << "[GC] Code: " << parsedRequest.code << "\n";
        std::cout << "[GC] Location: " << int(parsedRequest.location) << "\n\n";
        
        std::string actorResponse;
        
        switch(int(parsedRequest.requestType)){
            case 0:
                std::cout << "[GC] Routing LOAN request to actor\n";
                actorResponse = sendSynchronousRequest(parsedRequest, loanActorSocket);
                break;
                
            case 1:
                std::cout << "[GC] Routing RENEWAL request to actor\n";
                actorResponse = sendSynchronousRequest(parsedRequest, renewalActorSocket);
                break;
                
            case 2:
                std::cout << "[GC] Routing RETURN request to actor\n";
                actorResponse = sendSynchronousRequest(parsedRequest, returnActorSocket);
                break;
                
            default:
                actorResponse = "Error: Unknown request type";
                break;
        }
        
        std::cout << "[GC] Sending response to PS: " << actorResponse << "\n\n";
        clientSocket.send(zmq::buffer(actorResponse), zmq::send_flags::none);
    }
    
    return 0;
}