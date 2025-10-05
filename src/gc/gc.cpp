#include <zmq.hpp>
#include <iostream>
#include <string>

int main(int argc, char* argv[]){
    std::string ip;
    if(argc == 1){
        std::cout<<"No se puede establecer conexion si no se sabe la IP"<<std::endl;
        return 0;
    }else if(argc == 3){
         ip = argv[1];
    }else{
        std::cout
    }
    
    zmq::context_t context(1);
    zmq::socket_t socket(context,zmq::socket_type::rep);
    std::string completeSocketDir = "tcp://";
    completeSocketDir.append(ip);
    completeSocketDir.append(":5555");
    socket.bind(completeSocketDir);

    while(true){
        zmq::message_t request;

        auto result = socket.recv(request, zmq::recv_flags::none);
        
        std::string msg(static_cast<char*>(request.data()), request.size());
        std::cout << "Mensaje recibido: " << msg << std::endl;
        
        std::string reply = "Recibido";
        socket.send(zmq::buffer(reply), zmq::send_flags::none);
    }
}