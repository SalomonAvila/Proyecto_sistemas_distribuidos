#include <zmq.hpp>
#include <iostream>
#include <string>
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
}

/**
 * @brief Function for making the request to the respective actor process [PUB-SUB]
 * 
 * @param topic Topic for the publisher to publish
 * @param request Request structure that contains information
 * @param socket socket passed by reference
 */
void sendAsyncGcRequest(const std::string& topic, const Request& request, zmq::socket_t& socket){
    socket.send(zmq::buffer(topic), zmq::send_flags::sndmore);
    zmq::message_t message(sizeof(Request));
    memcpy(message.data(),&request,sizeof(Request));
    socket.send(message,zmq::send_flags::none);
}

std::string sendSyncGcRequest(const Request& request, zmq::socket_t& socket){
    zmq::message_t message(sizeof(Request));
    memcpy(message.data(), &request, sizeof(Request));
    socket.send(message, zmq::send_flags::none);
    zmq::message_t response;
    zmq::recv_result_t result = socket.recv(response, zmq::recv_flags::none);
    if(!result){
        std::cerr<<"No response from syncronous actor\n";
        return "No response from syncronous actor\n";
    }
    std::string content((char *)response.data(),response.size());
    return content;
}

/**
 * @brief Function for receiving the response of the respective actor process
 * 
 * @param socket socket passed by reference
 */
std::string receiveGcResponse(zmq::socket_t& socket){
    zmq::message_t response;
    zmq::recv_result_t result =  socket.recv(response, zmq::recv_flags::none);
    if(!result){
        std::cerr<<"ERROR\n";
        return "ERROR\n";
    }
    std::string content((char *)response.data(), response.size());
    return content;
}



/**
 * @brief Print request information
 * 
 * @param request 
 */
void printRequestInformation(Request &request){
    int typeRequest = int(request.requestType);
    std::cout<<"\n\n";
    std::cout<<"--------------------------";
    std::cout << " - Type: ";
    if(typeRequest == 0){
        std::cout<<"LOAN\n";
    }else if(typeRequest == 1){
        std::cout<<"RENEWAL\n";
    }else if(typeRequest == 2){
        std::cout<<"RETURN\n";
    }
    std::cout << " - Book code: " << request.code << "\n";
    std::cout << " - Location: " << int(request.location) << "\n";
    std::cout<<"--------------------------";
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
        std::cerr<<"Cannot stablish conection without library location\n";
        return 0;
    }else if(argc == 2){
        obtainEnvData(directionsPool);
         loc = std::int8_t(std::stoi(argv[1]));
         loc--;
         if(loc >= std::int8_t(directionsPool.size())){
            std::cout<<"This location does not exist\n";
            return 0;
         }
    }else{
        std::cerr<<"Run format is ./fileName #Location";
        return 0;
    }
    
    //Creating connections
    zmq::context_t context(1);
    
    // Socket REP para recibir de PS/Locust
    zmq::socket_t socketOne(context,zmq::socket_type::rep);
    std::string completeSocketDir = "tcp://";
    completeSocketDir.append(directionsPool[loc]);
    completeSocketDir.append(":5555");
    socketOne.bind(completeSocketDir);

    // Socket REQ para comunicarse con Actor Préstamo
    zmq::socket_t socketTwo(context, zmq::socket_type::req);
    
    // ⭐ CORRECCIÓN: Timeout en socketTwo (REQ), no en socketOne (REP)
    socketTwo.set(zmq::sockopt::rcvtimeo, 15000); 
    socketTwo.set(zmq::sockopt::sndtimeo, 15000);
    
    completeSocketDir = "tcp://";
    completeSocketDir.append(directionsPool[loc]);
    completeSocketDir.append(":5556");
    socketTwo.connect(completeSocketDir);
    
    // Socket PUB para renovaciones
    zmq::socket_t socketThree(context, zmq::socket_type::pub);
    completeSocketDir = "tcp://";
    completeSocketDir.append(directionsPool[loc]);
    completeSocketDir.append(":5557");
    socketThree.bind(completeSocketDir);

    // Socket PUB para devoluciones
    zmq::socket_t socketFour(context, zmq::socket_type::pub);
    completeSocketDir = "tcp://";
    completeSocketDir.append(directionsPool[loc]);
    completeSocketDir.append(":5558");
    socketFour.bind(completeSocketDir);

    std::cout << "GC initialized on port 5555\n";
    std::cout << "Waiting for requests...\n\n";

    //Business logic
    while (true) {
        zmq::message_t request;
        zmq::recv_result_t result = socketOne.recv(request, zmq::recv_flags::none);

        // ⭐ CORRECCIÓN: No terminar el programa, solo continuar al siguiente request
        if (!result) {
            std::cerr << "ERROR: Failed to receive request\n";
            continue;  // ✅ Continuar en vez de return 0
        }
        
        if (request.size() != sizeof(Request)) {
            std::cerr << "ERROR: Invalid request size (expected " 
                      << sizeof(Request) << ", got " << request.size() << ")\n";
            
            // Enviar respuesta de error al cliente
            std::string error_msg = "Error: Invalid request format";
            socketOne.send(zmq::buffer(error_msg), zmq::send_flags::none);
            continue;  // ✅ Continuar procesando otros requests
        }

        Request req;
        memcpy(&req, request.data(), sizeof(Request));
        
        printRequestInformation(req);
        
        int tipo = int(req.requestType);
        std::string reply;
        std::string response;
        std::string topic;
        
        // Calcular fecha de renovación
        time_t ahora = time(nullptr);
        time_t futuro = ahora + 7 * 24 * 60 * 60;
        tm* fecha = localtime(&futuro);
        std::string fechaFinal = "dia " + std::to_string(fecha->tm_mday) 
                                + " del mes " + std::to_string(fecha->tm_mon + 1);
        
        try {
            switch(tipo){
                case 0:  // LOAN (Síncrono)
                    std::cout << "Processing LOAN request...\n";
                    response = sendSyncGcRequest(req, socketTwo);
                    socketOne.send(zmq::buffer(response), zmq::send_flags::none);
                    std::cout << "LOAN response sent\n";
                    break;
                    
                case 1:  // RENEWAL (Asíncrono)
                    std::cout << "Processing RENEWAL request...\n";
                    reply = "Renovacion procesada. Nueva fecha: " + fechaFinal;
                    socketOne.send(zmq::buffer(reply), zmq::send_flags::none);
                    topic = "renewal";
                    sendAsyncGcRequest(topic, req, socketThree);
                    std::cout << "RENEWAL published\n";
                    break;
                    
                case 2:  // RETURN (Asíncrono)
                    std::cout << "Processing RETURN request...\n";
                    reply = "Solicitud procesada correctamente";
                    socketOne.send(zmq::buffer(reply), zmq::send_flags::none);
                    topic = "return";
                    sendAsyncGcRequest(topic, req, socketFour);
                    std::cout << "RETURN published\n";
                    break;
                    
                default:
                    std::cerr << "ERROR: Unknown request type " << tipo << "\n";
                    reply = "Error: Invalid request type";
                    socketOne.send(zmq::buffer(reply), zmq::send_flags::none);
                    break;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Exception processing request: " << e.what() << "\n";
            std::string error_msg = "Error: " + std::string(e.what());
            socketOne.send(zmq::buffer(error_msg), zmq::send_flags::none);
        }
    }
    
    return 0;
}