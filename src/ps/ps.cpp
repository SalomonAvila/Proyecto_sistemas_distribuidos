#include <zmq.hpp>
#include <iostream>
#include <string>
#include <cstdint>
#include "utils/structs.cpp"

void menu(){
    std::cout<<"\n\n\n";
    std::cout<<"----Bienvenido a la biblioteca Ada Lovelace----"<<"\n";
    std::cout<<"Que operacion desea hacer?\n";
    std::cout<<"1. Prestamo\n";
    std::cout<<"2. Renovacion\n";
    std::cout<<"3. Devolucion\n";
    std::cout<<"4. Salir";
}

void sendPsRequest(const Solicitud& solicitud, zmq::socket_t socket){
    zmq::message_t mensaje(sizeof(Solicitud));
    memcpy(mensaje.data(),&solicitud,sizeof(Solicitud));
    socket.send(mensaje,zmq::send_flags::none);
}

void receivePsRequest(Solicitud solicitud, zmq::socket_t socket){
    zmq::message_t mensaje;
    socket.recv(mensaje,zmq::recv_flags::none);
}

int main(int argc, char* argv[]){
    std::string ip;
    std::int8_t sede;
    if(argc == 1){
        std::cout<<"No se puede establecer conexion si no se sabe la IP\n";
        return 0;
    }else if(argc == 2){
         ip = argv[1];
    }else{
        std::cout<<"El formato de entrada es: ./[nombreDelArchivo] [IP] [#DeSede]";
        
    }

    //Creacion de la conexion
    zmq::context_t context(1);
    zmq::socket_t socket(context,zmq::socket_type::req);
    std::string completeSocketDir = "tcp://";
    completeSocketDir.append(ip);
    completeSocketDir.append(":5555");
    socket.connect(completeSocketDir);


    int opc;
    menu();
    std::cin>>opc;
    while(opc != 4){
        Solicitud prestamo;
        switch(opc){
            case 1:
                prestamo.tipo = TipoSolicitud::PRESTAMO;
                std::cout<<"Ingrese el codigo del libro que desea: ";
                std::cin>> prestamo.codigo;
                break;
            case 2:
                break;
            case 3:
                break;
            case 4:
                break;
        }
    }
    

}




/*
for(int i = 0; i < 10; i++){
        // Enviar mensaje
        zmq::message_t request(5);
        memcpy(request.data(), "Hello", 5);
        std::cout << "Enviando Hello " << i << "..." << std::endl;
        socket.send(request, zmq::send_flags::none);
        
        // IMPORTANTE: Debes recibir la respuesta antes de enviar de nuevo
        zmq::message_t reply;
        socket.recv(reply, zmq::recv_flags::none);
        
        std::string respuesta(static_cast<char*>(reply.data()), reply.size());
        std::cout << "Respuesta recibida: " << respuesta << std::endl;
    }
    return 0;


*/