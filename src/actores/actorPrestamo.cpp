#include <zmq.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <thread>
#include <chrono>
#include "../../utils/structs.cpp"

/**
 * @brief Function to read data from the file containing the environment variables 
 * 
 * @param v for storing IP values
 */
void obtainEnvData(std::vector<std::string> &v){
    std::fstream f("../.env");
    std::string key,val;
    while (std::getline(f, key, '=') && std::getline(f, val)) {
        v.push_back(val);
    }
}

std::string receiveAdResponse(zmq::socket_t& socket){
    zmq::message_t response;
    zmq::recv_result_t result =  socket.recv(response, zmq::recv_flags::none);
    if(!result){
        std::cerr<<"ERROR: Failed to receive response from GA\n";
        return "ERROR";
    }

    if(response.size() == 0){
        std::cerr<<"ERROR: Empty response from GA\n";
        return "ERROR";
    }

    std::string content(static_cast<char*>(response.data()), response.size());
    return content;
}

/**
 * @brief Implementation of sending request to GA, includes a retry with exponential backoff
 */
std::string sendAdRequest(zmq::message_t& message, zmq::context_t& context, std::string gaAddress){
    int maxRetries = 3;
    int baseDelay = 100;
    int maxDelay = 2000;

    for(int att = 0; att < maxRetries; att++){
        try {
            zmq::socket_t socket(context, zmq::socket_type::req);
            socket.set(zmq::sockopt::rcvtimeo, 1000);
            socket.set(zmq::sockopt::sndtimeo, 1000);
            socket.connect(gaAddress);

            zmq::message_t meg(message.size());
            memcpy(meg.data(), message.data(), message.size());
            socket.send(meg, zmq::send_flags::none);

            zmq::message_t response;
            zmq::recv_result_t result = socket.recv(response, zmq::recv_flags::none);
            
            socket.close();

            if(result && response.size() > 0){
                if(att > 0){
                    std::cout << "Request succeeded after " << att << " retries.\n";
                }
                return std::string(static_cast<char*>(response.data()), response.size());
            }
        } catch (const zmq::error_t& e) {
            std::cerr << "ZMQ Error on attempt " << att + 1 << "\n";
        }

        if(att < maxRetries - 1){
            int delay = std::min(baseDelay * (1 << att), maxDelay);
            std::cout << "Request failed, retrying in " << delay << " ms...\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }
    }
    std::cerr << "All " << maxRetries << " attempts failed\n";
    return "ERROR";
}



int main(int argc, char* argv[]){
    std::vector<std::string> directionsPool;
    std::int8_t loc;
    if(argc == 1){
        std::cout<<"No se puede establecer conexion si no se sabe la IP\n";
        return 0;
    }else if(argc == 2){
        obtainEnvData(directionsPool);
         loc = std::int8_t(std::stoi(argv[1]));
         loc--;
         if(loc >= std::int8_t(directionsPool.size())){
            std::cout<<"No existe esta sede\n";
            return 0;
         }
    }else{
        std::cout<<"El formato de entrada es: ./[nombreDelArchivo] [#DeSede]";
        return 0;
    }
    zmq::context_t context(1);
    zmq::socket_t socketOne(context, zmq::socket_type::rep);
    std::string completeSocketDir = "tcp://";
    completeSocketDir.append(directionsPool[loc]);
    completeSocketDir.append(":5556");
    socketOne.bind(completeSocketDir);

    completeSocketDir = "tcp://";
    completeSocketDir.append(directionsPool[0]); 
    completeSocketDir.append(":5560");

    std::string response;
    while(true){
        zmq::message_t request;
        zmq::recv_result_t result = socketOne.recv(request, zmq::recv_flags::none);
        if(!result){
            std::cerr << "ERROR receiving from GC\n";
            continue;
        }

        response = sendAdRequest(request, context, completeSocketDir);

        if(response == "ERROR"){
            response = "Error: GA timeout";
            std::cerr << "TIMEOUT detected when connecting with GA\n";
        }else{
            std::cout << "Request processed successfully.\n";
        }

        socketOne.send(zmq::buffer(response), zmq::send_flags::none);
    }
}