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

std::string renewBook(int code, int location, pqxx::connection &C){
    int sede_real = location + 1;
    
    try {
        pqxx::work W(C);

        pqxx::result R = W.exec(
            "SELECT id_estado, renovaciones "
            "FROM estados e "
            "JOIN libros l ON l.id_libro = e.id_libro "
            "WHERE l.codigo = " + std::to_string(code) + 
            " AND e.sede = " + std::to_string(sede_real) +
            " AND e.tipo_operacion = 'prestamo' "
            "ORDER BY e.fecha_operacion DESC "
            "LIMIT 1"
        );
        
        if (R.empty()) {
            return "No se puede ejecutar: préstamo no encontrado.";
        }
        
        int id_estado = R[0][0].as<int>();
        int renovaciones = R[0][1].as<int>();
        
        if (renovaciones >= 2) {
            return "No se puede renovar: se alcanzó el máximo de renovaciones.";
        }
  
        W.exec(
            "UPDATE estados "
            "SET renovaciones = renovaciones + 1, "
            "    fecha_devolucion_prevista = fecha_devolucion_prevista + INTERVAL '7 days' "
            "WHERE id_estado = " + std::to_string(id_estado)
        );
        
        W.commit();
        return "Préstamo renovado exitosamente.";
        
    } catch (const std::exception &e) {
        return std::string("Error: ") + e.what();
    }

}

std::string returnBook(int codigo, int sede, pqxx::connection &C) {
    int sede_real = sede + 1;  
    pqxx::work W(C);
    
    pqxx::result R = W.exec(
        "SELECT e.id_estado, e.id_libro "
        "FROM estados e "
        "JOIN libros l ON l.id_libro = e.id_libro "
        "WHERE l.codigo = " + std::to_string(codigo) + 
        " AND e.sede = " + std::to_string(sede_real) + 
        " AND e.tipo_operacion != 'devuelto' "
        "ORDER BY e.fecha_operacion DESC "
        "LIMIT 1;"
    );
    std::cout<<"Punto antes\n";
    if (R.empty()) return "No se puede ejecutar: préstamo no encontrado.";
    std::cout<<"Punto 1\n";
    int id_estado = R[0]["id_estado"].as<int>();
    int id_libro  = R[0]["id_libro"].as<int>();

    W.exec(
        "UPDATE estados SET tipo_operacion = 'devuelto' WHERE id_estado = " + std::to_string(id_estado) + ";"
    );

    std::string columna = (sede == 1) ? "ejemplares_sede1" : "ejemplares_sede2";
    W.exec(
        "UPDATE libros SET " + columna + " = " + columna + " + 1 WHERE id_libro = " + std::to_string(id_libro) + ";"
    );

    W.commit();
    std::cout<<"Punto 2\n";
    return "Préstamo devuelto exitosamente y ejemplares actualizados.";
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
        
        try{
            std::string conect = "dbname=root user=root password=root host=localhost port=5434";
            std::cout<<"Conexion es: "<<conect<<"\n";
            pqxx::connection C(conect);
            std::int8_t locat = req.location;
            locat++;
            std::cout<<"El codigo es: "<<int(req.requestType)<<"\n";
            switch(int(req.requestType)){
                case 0:
                    std::cerr<<"TO BE IMPLEMENTED\n";
                    break;
                case 1:
                    std::cout<<"Opcion de renovar\n";
                    reply = renewBook(req.code,req.location,C);
                    socketOne.send(zmq::buffer(reply), zmq::send_flags::none);
                    break;
                case 2:
                    std::cout<<"Opcion de devolver\n";
                    reply = returnBook(req.code,req.location,C);
                    socketOne.send(zmq::buffer(reply), zmq::send_flags::none);
                    break;
            }
            
        } catch (const std::exception &e) {
        std::cerr << "CONECTION ERROR";
    }

    }


}