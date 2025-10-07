#include <zmq.hpp>
#include <iostream>
#include <unistd.h>
#include <string>
#include <cstdint>
#include <fstream>
#include <vector>
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
    f.close();
}

/**
 * @brief Menu for the user to be guided
 * 
 */
void menu(){
    std::cout<<"\n\n\n";
    std::cout<<"----Welcome to the Ada Lovelace Library----"<<"\n";
    std::cout<<"What operation would you like to perform?\n";
    std::cout<<"1. Loan\n";
    std::cout<<"2. Renewal\n";
    std::cout<<"3. Return\n";
    std::cout<<"4. Exit\n";
}

/**
 * @brief Function for making the request to the Gc [REQ-REP]
 * 
 * 
 * @param request Request structure that contains information
 * @param socket socket passed by reference
 */
void sendPsRequest(const Request& request, zmq::socket_t& socket){
    zmq::message_t message(sizeof(Request));
    memcpy(message.data(),&request,sizeof(Request));
    socket.send(message,zmq::send_flags::none);
    std::cout<<"Sent\n";
}

/**
 * @brief Function for receiving the response of the Gc
 * 
 * @param socket socket passed by reference
 */
void receivePsResponse(zmq::socket_t& socket){
    zmq::message_t response;
    zmq::recv_result_t result = socket.recv(response,zmq::recv_flags::none);
    if(!result){
        std::cerr<<"No succesful response from Gc\n";
    }
    std::string contenido((char *)(response.data()), response.size());
    std::cout<<contenido<<"\n";
}

/**
 * @brief main function
 * 
 * @param argc 
 * @param argv 
 * @return int 
 */
int main(int argc, char* argv[]){
    std::vector<std::string> directionsPool;
    std::int8_t loc;
    bool file = false;
    if(argc == 1){
        std::cerr<<"Cannot stablish conection without library location\n";
        return 0;
    }else if(argc == 2){
        obtainEnvData(directionsPool);
        loc = std::int8_t(std::stoi(argv[1]));
        loc--;
        if(loc >= std::int8_t(directionsPool.size())){
        std::cerr<<"Location doesn't exist\n";
        return 0;
        }
    }else if((argc == 4) && (std::string(argv[2]) == "-f")){
        obtainEnvData(directionsPool);
        loc = std::int8_t(std::stoi(argv[1]));
        loc--;
        if(loc >= std::int8_t(directionsPool.size())){
            std::cout<<"Location doesn't exist\n";
            return 0;
        }
        std::cout<<"The data from the file "<<argv[3]<<" will be loaded\n";
        file = true;

    }else{
        std::cerr<<"Run format is ./fileName #Location [-f] [bulkInsertion.txt]";
        return 0;
    }

    zmq::context_t context(1);
    zmq::socket_t socket(context,zmq::socket_type::req);
    std::string completeSocketDir = "tcp://";
    completeSocketDir.append(directionsPool[loc]);
    completeSocketDir.append(":5555");
    socket.connect(completeSocketDir);

    if(file){
        std::string reqType, code, location;
        std::string route = "../";
        route.append(argv[3]);
        std::fstream f(route);
        Request request;
        int c = 1;
        while(f>>reqType>>code>>location){
            std::cout<<c<<"\n";
            request.code = std::int32_t(std::stoi(code));
            std::int8_t formattedLocation = std::int8_t(std::stoi(location));
            formattedLocation--;
            if(formattedLocation != loc){
                std::cout<<"You need to go to the location the loan was registered\n";
                continue;
            }
            std::cout<<"Formatted location = "<<formattedLocation<<"\n";
            request.location = formattedLocation;
            if(reqType == "LOAN"){
                request.requestType = RequestType::LOAN;
            }else if(reqType == "RENEWAL"){
                request.requestType = RequestType::RENEWAL;
            }else if(reqType == "RETURN"){
                request.requestType = RequestType::RETURN;
            };
            sendPsRequest(request,socket);
            receivePsResponse(socket);
            c++;
        }
        return 0;
    }

    int opc;
    std::cout<<"Connect succesfully at: "<<completeSocketDir<<"\n";
    while(true){
        menu();
        std::cin>>opc;
        Request request;
        request.location = loc;
        switch(opc){
            case 1:
                request.requestType = RequestType::LOAN;
                std::cout<<"Enter the code of the book you wish to borrow: ";
                std::cin>> request.code;
                sendPsRequest(request,socket);
                receivePsResponse(socket);
                break;
            case 2:
                request.requestType = RequestType::RENEWAL;
                std::cout<<"Enter the code for the book you wish to renew: ";
                std::cin>> request.code;
                sendPsRequest(request,socket);
                receivePsResponse(socket);
                break;
            case 3:
                request.requestType = RequestType::RETURN;
                std::cout<<"Enter the code of the book you wish to return: ";
                std::cin>> request.code;
                sendPsRequest(request,socket);
                receivePsResponse(socket);
                break;
            case 4:
                socket.disconnect(completeSocketDir);
                socket.close();
                return 0;
                break;
        }
    }
}