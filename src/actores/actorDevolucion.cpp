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

/**
 * @brief Function for receiving the response
 * 
 * @param socket socket passed by reference
 */
std::string receiveAdResponse(zmq::socket_t& socket){
    zmq::message_t response;
    zmq::recv_result_t result =  socket.recv(response, zmq::recv_flags::none);
    if(!result){
        std::cerr<<"ERROR\n";
        return 0;
    }
    std::string content((char *)response.data(), response.size());
    return content;
}

/**
 * @brief Function for making the request to the respective GA [REQ-REP]
 * 
 * @param message 
 * @param socket 
 */
void sendAdRequest(zmq::message_t& message, zmq::socket_t& socket){
    socket.send(message, zmq::send_flags::none);
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
        std::cout<<"El formato de entrada es: ./[nombreDelArchivo] [IP] [#DeSede]";
    }

    zmq::context_t context(1);
    zmq::socket_t socketOne(context, zmq::socket_type::sub);
    std::string completeSocketDir = "tcp://";
    completeSocketDir.append(directionsPool[loc]);
    completeSocketDir.append(":5558"); 
    socketOne.connect(completeSocketDir);
    std::string topic = "return";
    socketOne.set(zmq::sockopt::subscribe, topic);

    zmq::socket_t socketTwo(context, zmq::socket_type::req);
    completeSocketDir = "tcp://";
    completeSocketDir.append(directionsPool[0]);
    completeSocketDir.append(":5560"); 
    socketTwo.connect(completeSocketDir);

    while(true){
        zmq::message_t topicMessage;
        zmq::message_t message;
        zmq::recv_result_t resultOne = socketOne.recv(topicMessage,zmq::recv_flags::none);
        zmq::recv_result_t resultTwo =  socketOne.recv(message,zmq::recv_flags::none);
        if(!resultOne || !resultTwo){
            std::cerr<<"NO SE PUDO OBTENER ALGUNO DE LOS 2 MENSAJES\n";
            return 0;
        }
        Request req;
        memcpy(&req, message.data(), sizeof(Request));
        
        std::cout << "Solicitud recibida:\n";
        std::cout << " - Tipo: " << int(req.requestType) << "\n";
        std::cout << " - Código libro: " << req.code << "\n";
        std::cout << " - Sede: " << int(req.location) << "\n";
        
        sendAdRequest(message,socketTwo);
        std::string response = receiveAdResponse(socketTwo);
    }
}