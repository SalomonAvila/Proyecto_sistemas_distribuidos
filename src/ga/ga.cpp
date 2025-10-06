#include <zmq.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <pqxx/pqxx>
#include "../../utils/structs.cpp"

void obtainEnvData(std::vector<std::string> &v){
    std::fstream f("../.env");
    std::string key,val;
    while (std::getline(f, key, '=') && std::getline(f, val)) {
        v.push_back(val);
    }
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

int main(int argc, char *argv[]){
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


    zmq::context_t context(1);
    zmq::socket_t socketOne(context,zmq::socket_type::rep);
    std::string completeSocketDir = "tcp://";
    completeSocketDir.append(directionsPool[loc]);
    completeSocketDir.append(":5560");
    socketOne.bind(completeSocketDir);

    while(true){

        zmq::message_t request;
        zmq::recv_result_t result = socketOne.recv(request, zmq::recv_flags::none);
        if(!result){
            std::cerr<<"ERROR\n";
            return 0;
        }
        Request req;
        memcpy(&req, request.data(), sizeof(Request));
        std::cout << "Solicitud recibida:\n";
        std::cout << " - Tipo: " << int(req.requestType) << "\n";
        std::cout << " - Código libro: " << req.code << "\n";
        std::cout << " - Sede: " << int(req.location) << "\n";

        std::string reply;
        socketOne.send(zmq::buffer(reply), zmq::send_flags::none);
        
        try{
            std::string conect = "dbname=root user=root password=root host=localhost port=5434";
            std::cout<<"Conexion es: "<<conect<<"\n";
            pqxx::connection C(conect);
            
            switch(int(req.requestType)){
                case 0:
                    std::cerr<<"TO BE IMPLEMENTED\n";
                    break;
                case 1:
                    //Funcion para renovacion
                    break;
                case 2:
                    //Funcion para devolucion
                    break;
            }
            
        } catch (const std::exception &e) {
        std::cerr << "CONECTION ERROR";
    }

    }


}