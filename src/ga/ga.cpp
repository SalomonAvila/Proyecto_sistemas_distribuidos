#include <zmq.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <pqxx/pqxx>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <atomic>
#include "../../utils/structs.cpp"

std::atomic<bool> running(true);

void obtainEnvData(std::vector<std::string> &v){
    std::fstream f("../.env");
    std::string key, val;
    while (std::getline(f, key, '=') && std::getline(f, val)){
        v.push_back(val);
    }
}

void heartbeatThread(zmq::context_t &context, const std::string &address){
    zmq::socket_t heartbeatSocket(context, zmq::socket_type::pub);
    std::string heartbeatAddr = "tcp://" + address + ":5562";
    heartbeatSocket.bind(heartbeatAddr);
    
    std::cout << "[Heartbeat] Started on " << heartbeatAddr << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    while(running){
        try {
            std::string heartbeat = "ALIVE";
            heartbeatSocket.send(zmq::buffer(heartbeat), zmq::send_flags::none);
            std::this_thread::sleep_for(std::chrono::seconds(2));
        } catch (const std::exception &e) {
            std::cerr << "[Heartbeat] Error: " << e.what() << std::endl;
            break;
        }
    }
}

void sendAsyncGaRequest(const std::string &topic, const Request &request, zmq::socket_t &socket){
    socket.send(zmq::buffer(topic), zmq::send_flags::sndmore);
    zmq::message_t message(sizeof(Request));
    memcpy(message.data(), &request, sizeof(Request));
    socket.send(message, zmq::send_flags::none);
}

std::string loanBook(int code, int location, pqxx::connection &C){
    int sede_real = location + 1;
    std::string columna_ejemplares = (sede_real == 1) ? "ejemplares_sede1" : "ejemplares_sede2";

    try{
        pqxx::work W(C);
        
        pqxx::result R = W.exec(
            "SELECT id_libro, " + columna_ejemplares + " "
            "FROM libros "
            "WHERE codigo = " + W.quote(code)
        );

        if (R.empty()) {
            W.abort();
            return "Error: El libro no existe.";
        }
        
        int id_libro = R[0]["id_libro"].as<int>();
        int ejemplares_disponibles = R[0][columna_ejemplares].as<int>();

        if (ejemplares_disponibles <= 0) {
            W.abort();
            return "Error: No hay ejemplares disponibles de este libro.";
        }

        W.exec(
            "UPDATE libros "
            "SET " + columna_ejemplares + " = " + columna_ejemplares + " - 1 "
            "WHERE id_libro = " + W.quote(id_libro)
        );

        W.exec(
            "INSERT INTO estados (id_libro, tipo_operacion, fecha_operacion, fecha_devolucion_prevista, sede, renovaciones) "
            "VALUES (" +
                W.quote(id_libro) + ", " +
                W.quote("prestamo") + ", " +
                "NOW(), " +
                "(NOW() + interval '14 days')::date, " +
                W.quote(sede_real) + ", " +
                "0" +
            ")"
        );

        pqxx::result fecha_result = W.exec("SELECT (NOW() + interval '14 days')::date");
        std::string fecha_devolucion = fecha_result[0][0].as<std::string>();
        
        W.commit();
        return "Préstamo exitoso. Fecha de devolución: " + fecha_devolucion;
    }
    catch (const std::exception &e){
        return std::string("Error en BD: ") + e.what();
    }
}

std::string renewBook(int code, int location, pqxx::connection &C){
    int sede_real = location + 1;
    try{
        pqxx::work W(C);
        
        pqxx::result R = W.exec(
            "SELECT e.id_estado, e.renovaciones "
            "FROM estados e "
            "JOIN libros l ON l.id_libro = e.id_libro "
            "WHERE l.codigo = " + W.quote(code) +
            " AND e.sede = " + W.quote(sede_real) +
            " AND e.tipo_operacion = 'prestamo' "
            " AND NOT EXISTS ( "
            "     SELECT 1 FROM estados e3 "
            "     WHERE e3.id_libro = e.id_libro "
            "     AND e3.tipo_operacion = 'devuelto' "
            "     AND e3.fecha_operacion > e.fecha_operacion "
            " )"
            " ORDER BY e.renovaciones ASC, e.fecha_operacion ASC "
            " LIMIT 1"
        );
            
        if (R.empty()) {
            W.abort();
            return "Error: No se encontró un préstamo activo para este libro en esta sede.";
        }
        
        int id_estado = R[0]["id_estado"].as<int>();
        int renovaciones = R[0]["renovaciones"].as<int>();
        
        if (renovaciones >= 2){
            W.abort();
            return "Error: Límite máximo de renovaciones alcanzado (2).";
        }
        
        W.exec(
            "UPDATE estados "
            "SET renovaciones = renovaciones + 1, "
            "    fecha_devolucion_prevista = fecha_devolucion_prevista + INTERVAL '7 days' "
            "WHERE id_estado = " + W.quote(id_estado)
        );
            
        W.commit();
        return "Préstamo renovado exitosamente por 7 días adicionales.";
    }
    catch (const std::exception &e){
        return std::string("Error en BD: ") + e.what();
    }
}

std::string returnBook(int codigo, int sede, pqxx::connection &C){
    int sede_real = sede + 1;

    try {
        pqxx::work W(C);
        
        pqxx::result R = W.exec(
            "SELECT e.id_estado, e.id_libro "
            "FROM estados e "
            "JOIN libros l ON l.id_libro = e.id_libro "
            "WHERE l.codigo = " + W.quote(codigo) + 
            " AND e.sede = " + W.quote(sede_real) + 
            " AND e.tipo_operacion = 'prestamo' "
            "ORDER BY e.renovaciones DESC, e.fecha_operacion DESC "
            "LIMIT 1"
        );

        if (R.empty()) {
            W.abort();
            return "Error: No se encontró un préstamo activo para este libro en esta sede.";
        }

        int id_estado = R[0]["id_estado"].as<int>();
        int id_libro = R[0]["id_libro"].as<int>();

        W.exec(
            "UPDATE estados "
            "SET tipo_operacion = 'devuelto' "
            "WHERE id_estado = " + W.quote(id_estado)
        );

        std::string columna_ejemplares = (sede_real == 1) ? "ejemplares_sede1" : "ejemplares_sede2";
        W.exec(
            "UPDATE libros "
            "SET " + columna_ejemplares + " = " + columna_ejemplares + " + 1 "
            "WHERE id_libro = " + W.quote(id_libro)
        );

        W.commit();
        return "Devolución exitosa. Ejemplar disponible nuevamente.";
    }
    catch (const std::exception &e) {
        return std::string("Error en BD: ") + e.what();
    }
}

void printRequestInformation(Request &request){
    int typeRequest = int(request.requestType);
    std::cout<<"\n--------------------------";
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
    std::cout<<"--------------------------\n";
}

int main(int argc, char *argv[]){
    std::vector<std::string> directionsPool;
    std::int8_t loc;
    
    if (argc != 2){
        std::cerr<<"Run format: ./ga #Location\n";
        return 0;
    }
    
    obtainEnvData(directionsPool);
    loc = std::int8_t(std::stoi(argv[1])) - 1;
    
    if (loc >= std::int8_t(directionsPool.size())){
        std::cout << "This location does not exist.\n";
        return 0;
    }
    
    std::string dbConnection = "dbname=root user=root password=root host=localhost port=5434";
    
    if (int(loc) == 0){
        std::cout << "======== GA PRIMARIO - SEDE 1 ========\n";
        
        zmq::context_t context(1);
        
        zmq::socket_t socketOne(context, zmq::socket_type::rep);
        std::string addr = "tcp://" + directionsPool[loc] + ":5560";
        socketOne.bind(addr);
        std::cout << "[GA] REP on port 5560\n";

        zmq::socket_t socketTwo(context, zmq::socket_type::pub);
        addr = "tcp://" + directionsPool[loc] + ":5561";
        socketTwo.bind(addr);
        std::cout << "[GA] PUB on port 5561 (replica)\n";
        
        std::thread hbThread(heartbeatThread, std::ref(context), std::ref(directionsPool[loc]));
        std::cout << "[GA] Ready\n\n";

        while (true){
            zmq::message_t request;
            socketOne.recv(request, zmq::recv_flags::none);
            
            Request req;
            memcpy(&req, request.data(), sizeof(Request));
            printRequestInformation(req);

            std::string reply;
            try{
                pqxx::connection C(dbConnection);
                
                switch (int(req.requestType)){
                    case 0:
                        reply = loanBook(req.code, req.location, C);
                        if (reply.find("Error") == std::string::npos) {
                            sendAsyncGaRequest("replica", req, socketTwo);
                        }
                        break;
                    case 1:
                        reply = renewBook(req.code, req.location, C);
                        if (reply.find("Error") == std::string::npos) {
                            sendAsyncGaRequest("replica", req, socketTwo);
                        }
                        break;
                    case 2:
                        reply = returnBook(req.code, req.location, C);
                        if (reply.find("Error") == std::string::npos) {
                            sendAsyncGaRequest("replica", req, socketTwo);
                        }
                        break;
                }
            }
            catch (const std::exception &e){
                reply = "Error en BD: " + std::string(e.what());
            }
            socketOne.send(zmq::buffer(reply), zmq::send_flags::none);
        }
        
        running = false;
        hbThread.join();
    }
    else {
        std::cout << "======== GA RÉPLICA - SEDE 2 ========\n";
        
        zmq::context_t context(1);
        
        zmq::socket_t socketOne(context, zmq::socket_type::sub);
        std::string addr = "tcp://" + directionsPool[0] + ":5561";
        socketOne.connect(addr);
        socketOne.set(zmq::sockopt::subscribe, "replica");
        socketOne.set(zmq::sockopt::rcvtimeo, 100);
        
        zmq::socket_t socketFailover(context, zmq::socket_type::rep);
        addr = "tcp://" + directionsPool[loc] + ":5560";
        socketFailover.bind(addr);
        std::cout << "[REPLICA] Ready\n\n";

        while (true){
            zmq::message_t topicMessage, message;
            
            if (socketOne.recv(topicMessage, zmq::recv_flags::none)) {
                socketOne.recv(message, zmq::recv_flags::none);
                
                Request req;
                memcpy(&req, message.data(), sizeof(Request));
                
                try{
                    pqxx::connection C(dbConnection);
                    std::string reply;
                    
                    switch (int(req.requestType)){
                        case 0: reply = loanBook(req.code, req.location, C); break;
                        case 1: reply = renewBook(req.code, req.location, C); break;
                        case 2: reply = returnBook(req.code, req.location, C); break;
                    }
                }
                catch (const std::exception &e){}
            }
            
            zmq::message_t failoverRequest;
            if (socketFailover.recv(failoverRequest, zmq::recv_flags::dontwait)) {
                std::cout << "\n[FAILOVER] Processing request\n";
                
                Request req;
                memcpy(&req, failoverRequest.data(), sizeof(Request));
                
                std::string reply;
                try{
                    pqxx::connection C(dbConnection);
                    switch (int(req.requestType)){
                        case 0: reply = loanBook(req.code, req.location, C); break;
                        case 1: reply = renewBook(req.code, req.location, C); break;
                        case 2: reply = returnBook(req.code, req.location, C); break;
                    }
                }
                catch (const std::exception &e){
                    reply = "Error en BD: " + std::string(e.what());
                }
                socketFailover.send(zmq::buffer(reply), zmq::send_flags::none);
            }
        }
    }
    return 0;
}