#include <zmq.hpp>
#include <iostream>
#include <string>
#include <fstream>
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

}

void sendAdRequest(zmq::message_t& message, zmq::socket_t& socket){

}

int main(int argc, char* argv[]){

}