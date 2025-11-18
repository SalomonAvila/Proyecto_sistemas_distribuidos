#include <zmq.hpp>
#include <iostream>
#include <unistd.h>
#include <string>
#include <cstdint>
#include <fstream>
#include <vector>
#include "../../utils/structs.cpp"

void obtainEnvData(std::vector<std::string> &environmentVariables){
    std::fstream configFile("../.env");
    std::string key, value;
    while (std::getline(configFile, key, '=') && std::getline(configFile, value)) {
        environmentVariables.push_back(value);
    }
    configFile.close();
}

void displayMenu(){
    std::cout << "\n\n\n";
    std::cout << "========================================\n";
    std::cout << "  ADA LOVELACE LIBRARY SYSTEM\n";
    std::cout << "========================================\n";
    std::cout << "Select an operation:\n";
    std::cout << "1. Loan a book\n";
    std::cout << "2. Renew a loan\n";
    std::cout << "3. Return a book\n";
    std::cout << "4. Exit\n";
    std::cout << "========================================\n";
    std::cout << "Option: ";
}

void sendRequestToGc(const Request& clientRequest, zmq::socket_t& gcSocket){
    zmq::message_t requestMessage(sizeof(Request));
    memcpy(requestMessage.data(), &clientRequest, sizeof(Request));
    gcSocket.send(requestMessage, zmq::send_flags::none);
    std::cout << "[PS] Request sent to GC\n";
}

void receiveResponseFromGc(zmq::socket_t& gcSocket){
    zmq::message_t gcResponse;
    zmq::recv_result_t receiveResult = gcSocket.recv(gcResponse, zmq::recv_flags::none);
    
    if(!receiveResult){
        std::cerr << "[PS-Error] No successful response from GC\n";
        return;
    }
    
    std::string responseContent(static_cast<char*>(gcResponse.data()), gcResponse.size());
    std::cout << "[PS-Response] " << responseContent << "\n";
}

void processFileRequests(const std::string &filePath, std::int8_t currentLocation, zmq::socket_t& gcSocket){
    std::string requestTypeStr, bookCodeStr, locationStr;
    std::fstream requestFile(filePath);
    
    if(!requestFile.is_open()){
        std::cerr << "[PS-Error] Cannot open file: " << filePath << "\n";
        return;
    }
    
    Request clientRequest;
    int requestCounter = 1;
    
    std::cout << "[PS] Processing requests from file...\n\n";
    
    while(requestFile >> requestTypeStr >> bookCodeStr >> locationStr){
        std::cout << "[PS] Processing request #" << requestCounter << "\n";
        
        clientRequest.code = std::int32_t(std::stoi(bookCodeStr));
        std::int8_t requestedLocation = std::int8_t(std::stoi(locationStr)) - 1;
        
        if(requestedLocation != currentLocation){
            std::cout << "[PS-Warning] Request location mismatch. You must be at location " 
                      << (requestedLocation + 1) << "\n\n";
            requestCounter++;
            continue;
        }
        
        clientRequest.location = requestedLocation;
        
        if(requestTypeStr == "LOAN"){
            clientRequest.requestType = RequestType::LOAN;
            std::cout << "[PS] Type: LOAN\n";
        } else if(requestTypeStr == "RENEWAL"){
            clientRequest.requestType = RequestType::RENEWAL;
            std::cout << "[PS] Type: RENEWAL\n";
        } else if(requestTypeStr == "RETURN"){
            clientRequest.requestType = RequestType::RETURN;
            std::cout << "[PS] Type: RETURN\n";
        } else {
            std::cout << "[PS-Error] Unknown request type: " << requestTypeStr << "\n\n";
            requestCounter++;
            continue;
        }
        
        std::cout << "[PS] Book code: " << clientRequest.code << "\n";
        std::cout << "[PS] Location: " << (int(clientRequest.location) + 1) << "\n";
        
        sendRequestToGc(clientRequest, gcSocket);
        receiveResponseFromGc(gcSocket);
        
        std::cout << "\n";
        requestCounter++;
    }
    
    requestFile.close();
    std::cout << "[PS] Finished processing " << (requestCounter - 1) << " requests from file\n";
}

int main(int argc, char* argv[]){
    std::vector<std::string> ipAddressList;
    std::int8_t locationIndex;
    bool useFileMode = false;
    std::string inputFilePath;
    
    if(argc == 1){
        std::cerr << "[PS-Error] Cannot establish connection without library location\n";
        std::cerr << "[PS-Error] Usage: ./ps <location> [-f <file>]\n";
        return 0;
    } else if(argc == 2){
        obtainEnvData(ipAddressList);
        locationIndex = std::int8_t(std::stoi(argv[1])) - 1;
        
        if(locationIndex >= std::int8_t(ipAddressList.size())){
            std::cerr << "[PS-Error] Location does not exist\n";
            return 0;
        }
    } else if((argc == 4) && (std::string(argv[2]) == "-f")){
        obtainEnvData(ipAddressList);
        locationIndex = std::int8_t(std::stoi(argv[1])) - 1;
        
        if(locationIndex >= std::int8_t(ipAddressList.size())){
            std::cerr << "[PS-Error] Location does not exist\n";
            return 0;
        }
        
        inputFilePath = "../";
        inputFilePath.append(argv[3]);
        std::cout << "[PS] File mode enabled: " << argv[3] << "\n";
        useFileMode = true;
    } else {
        std::cerr << "[PS-Error] Invalid arguments\n";
        std::cerr << "[PS-Error] Usage: ./ps <location> [-f <file>]\n";
        return 0;
    }

    std::cout << "========================================\n";
    std::cout << "  REQUESTING PROCESS (PS) - STARTING\n";
    std::cout << "========================================\n";

    zmq::context_t zmqContext(1);
    zmq::socket_t gcSocket(zmqContext, zmq::socket_type::req);
    std::string gcEndpoint = "tcp://";
    gcEndpoint.append(ipAddressList[locationIndex]);
    gcEndpoint.append(":5555");
    gcSocket.connect(gcEndpoint);
    
    std::cout << "[PS] Connected to GC at: " << gcEndpoint << "\n";
    std::cout << "[PS] Current location: " << (int(locationIndex) + 1) << "\n\n";

    if(useFileMode){
        processFileRequests(inputFilePath, locationIndex, gcSocket);
        gcSocket.disconnect(gcEndpoint);
        gcSocket.close();
        return 0;
    }

    int selectedOption;
    Request clientRequest;
    clientRequest.location = locationIndex;
    
    while(true){
        displayMenu();
        std::cin >> selectedOption;
        
        switch(selectedOption){
            case 1:
                clientRequest.requestType = RequestType::LOAN;
                std::cout << "\n[PS] Enter the book code you wish to loan: ";
                std::cin >> clientRequest.code;
                std::cout << "\n[PS] Sending LOAN request...\n";
                sendRequestToGc(clientRequest, gcSocket);
                receiveResponseFromGc(gcSocket);
                break;
                
            case 2:
                clientRequest.requestType = RequestType::RENEWAL;
                std::cout << "\n[PS] Enter the book code you wish to renew: ";
                std::cin >> clientRequest.code;
                std::cout << "\n[PS] Sending RENEWAL request...\n";
                sendRequestToGc(clientRequest, gcSocket);
                receiveResponseFromGc(gcSocket);
                break;
                
            case 3:
                clientRequest.requestType = RequestType::RETURN;
                std::cout << "\n[PS] Enter the book code you wish to return: ";
                std::cin >> clientRequest.code;
                std::cout << "\n[PS] Sending RETURN request...\n";
                sendRequestToGc(clientRequest, gcSocket);
                receiveResponseFromGc(gcSocket);
                break;
                
            case 4:
                std::cout << "\n[PS] Disconnecting from system...\n";
                gcSocket.disconnect(gcEndpoint);
                gcSocket.close();
                std::cout << "[PS] Goodbye!\n";
                return 0;
                
            default:
                std::cout << "\n[PS-Error] Invalid option. Please select 1-4.\n";
                break;
        }
    }
    
    return 0;
}