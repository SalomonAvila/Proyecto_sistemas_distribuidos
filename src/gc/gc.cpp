#include <zmq.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include "../../utils/structs.cpp"

void obtainEnvData(std::vector<std::string> &v){
    std::fstream f("../.env");
    std::string key,val;
    while (std::getline(f, key, '=') && std::getline(f, val)) {
        v.push_back(val);
    }
}

void sendGcRequest(const Request& request, zmq::socket_t& socket){
    zmq::message_t message(sizeof(Request));
    memcpy(message.data(),&request,sizeof(Request));
    socket.send(message,zmq::send_flags::none);
    std::cout<<"Enviado\n";
}

void sendAsyncGcRequest(const std::string& topic, const Request& request, zmq::socket_t& socket){
    socket.send(zmq::buffer(topic), zmq::send_flags::sndmore);
    zmq::message_t message(sizeof(Request));
    memcpy(message.data(),&request,sizeof(Request));
    socket.send(message,zmq::send_flags::none);
}

std::string receiveGcResponse(zmq::socket_t& socket){
    zmq::message_t response;
    zmq::recv_result_t result =  socket.recv(response, zmq::recv_flags::none);
    if(!result){
        std::cerr<<"ERROR\n";
        return 0;
    }
    std::string content((char *)response.data(), response.size());
    return content;
}

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
            std::cout<<"No existe esta sede\n";
            return 0;
         }
    }else{
        std::cerr<<"Run format is ./[fileName] [#Location]";
        return 0;
    }
    
    //Creacion de la conexion de respuesta
    zmq::context_t context(1);
    zmq::socket_t socketOne(context,zmq::socket_type::rep);
    std::string completeSocketDir = "tcp://";
    completeSocketDir.append(directionsPool[loc]);
    completeSocketDir.append(":5555");
    socketOne.bind(completeSocketDir);

    /* Conection to the Loan Actor  --Later--
    zmq::socket_t socketTwo(context, zmq::socket_type::req);
    completeSocketDir = "tcp://";
    completeSocketDir.append(ip);
    completeSocketDir.append(":5556");
    socketTwo.connect(completeSocketDir);
    */

    zmq::socket_t socketThree(context, zmq::socket_type::pub);
    completeSocketDir = "tcp://";
    completeSocketDir.append(directionsPool[loc]);
    completeSocketDir.append(":5557");
    socketThree.bind(completeSocketDir);

    zmq::socket_t socketFour(context, zmq::socket_type::pub);
    completeSocketDir = "tcp://";
    completeSocketDir.append(directionsPool[loc]);
    completeSocketDir.append(":5558");
    socketFour.bind(completeSocketDir);

    while (true) {
        zmq::message_t request;
        zmq::recv_result_t result = socketOne.recv(request, zmq::recv_flags::none);

        if (request.size() != sizeof(Request) || !result) {
            std::cerr<<"ERROR IN REQ SOCKET\n";
            return 0;
        }

        Request req;
        memcpy(&req, request.data(), sizeof(Request));
        
        std::cout << "Solicitud recibida:\n";
        std::cout << " - Tipo: " << int(req.requestType) << "\n";
        std::cout << " - Código libro: " << req.code << "\n";
        std::cout << " - Sede: " << int(req.location) << "\n";
        
        int tipo = int(req.requestType);
        std::string reply;
        std::string response;
        std::string topic = "return";
        std::string message;
        time_t ahora = time(nullptr);
        time_t futuro = ahora + 7 * 24 * 60 * 60;
        tm* fecha = localtime(&futuro);
        std::string fechaFinal = "dia " + std::to_string(fecha->tm_mday) + " del mes " + std::to_string(fecha->tm_mon + 1);
        switch(tipo){
            case 0:
                response = "To be implemented later\n";
                socketOne.send(zmq::buffer(response), zmq::send_flags::none);
                break;
            case 1:
                topic = "renewal";
                sendAsyncGcRequest(topic,req,socketThree);
                socketOne.send(zmq::buffer(fechaFinal), zmq::send_flags::none);
                break;
            case 2:
                topic = "return";
                reply = "Solicitud procesada correctamente";
                sendAsyncGcRequest(topic,req,socketFour);
                socketOne.send(zmq::buffer(reply), zmq::send_flags::none);
                break;
        }
    }
}