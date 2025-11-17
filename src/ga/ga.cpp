#include <zmq.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <pqxx/pqxx>
#include <stdexcept>
#include "../../utils/structs.cpp"

/**
 * @brief Function to read data from the file containing environment variables 
 * 
 * @param v for storing IP values
 */
void obtainEnvData(std::vector<std::string> &v){
    std::fstream f("../.env");
    std::string key, val;
    while (std::getline(f, key, '=') && std::getline(f, val)){
        v.push_back(val);
    }
}

/**
 * @brief Function for making request to the respective GA [PUB-SUB]
 * 
 * @param topic Topic for the publisher to publish
 * @param request Request structure that contains information
 * @param socket socket passed by reference
 */
void sendAsyncGaRequest(const std::string &topic, const Request &request, zmq::socket_t &socket){
    socket.send(zmq::buffer(topic), zmq::send_flags::sndmore);
    zmq::message_t message(sizeof(Request));
    memcpy(message.data(), &request, sizeof(Request));
    socket.send(message, zmq::send_flags::none);
}

/**
 * @brief Function to process a book loan request
 * 
 * @param code Book code
 * @param location Location (0 or 1)
 * @param C Database connection
 * @return std::string Result message
 */
std::string loanBook(int code, int location, pqxx::connection &C){
    int sede_real = location + 1;
    std::string columna_ejemplares = (sede_real == 1) ? "ejemplares_sede1" : "ejemplares_sede2";

    try{
        pqxx::work W(C);
        
        // Check if the book exists and has available copies at the specified location
        pqxx::result R = W.exec(
            "SELECT id_libro, " + columna_ejemplares + " "
            "FROM libros "
            "WHERE codigo = " + W.quote(code)
        );

        if (R.empty()) {
            W.abort(); // Importante: abortar antes de retornar
            return "Error: El libro no existe.";
        }
        
        int id_libro = R[0]["id_libro"].as<int>();
        int ejemplares_disponibles = R[0][columna_ejemplares].as<int>();

        if (ejemplares_disponibles <= 0) {
            W.abort(); // Importante: abortar antes de retornar
            return "Error: No hay ejemplares disponibles de este libro.";
        }

        // Decrement the count of available copies
        W.exec(
            "UPDATE libros "
            "SET " + columna_ejemplares + " = " + columna_ejemplares + " - 1 "
            "WHERE id_libro = " + W.quote(id_libro)
        );

        // Create a new record in the estados table
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

        // Get the return date BEFORE commit
        pqxx::result fecha_result = W.exec("SELECT (NOW() + interval '14 days')::date");
        std::string fecha_devolucion = fecha_result[0][0].as<std::string>();
        
        W.commit();
        
        return "Préstamo exitoso. Fecha de devolución: " + fecha_devolucion;
    }
    catch (const std::exception &e){
        return std::string("Error en BD: ") + e.what();
    }
}
/**
 * @brief Function to interact with PostgreSQL (versión modificada)
 * 
 * @param code 
 * @param location 
 * @param C 
 * @return std::string 
 */
std::string renewBook(int code, int location, pqxx::connection &C){
    int sede_real = location + 1;
    try{
        pqxx::work W(C);
        
        // Buscar el préstamo con menor número de renovaciones que no haya sido devuelto
        pqxx::result R = W.exec(
            "SELECT e.id_estado, e.renovaciones "
            "FROM estados e "
            "JOIN libros l ON l.id_libro = e.id_libro "
            "WHERE l.codigo = " + std::to_string(code) +
            " AND e.sede = " + std::to_string(sede_real) +
            " AND e.tipo_operacion = 'prestamo' "
            " AND NOT EXISTS ( "
            "     SELECT 1 FROM estados e3 "
            "     WHERE e3.id_libro = e.id_libro "
            "     AND e3.tipo_operacion = 'devuelto' "
            "     AND e3.fecha_operacion > e.fecha_operacion "
            " )"
            " ORDER BY e.renovaciones ASC, e.fecha_operacion ASC "
            " LIMIT 1");
            
        if (R.empty()) {
            return "Error: No se encontró un préstamo activo para este libro en esta sede.";
        }
        
        int id_estado = R[0]["id_estado"].as<int>();
        int renovaciones = R[0]["renovaciones"].as<int>();
        
        if (renovaciones >= 2){
            return "Error: Límite máximo de renovaciones alcanzado (2).";
        }
        
        // Actualizar el préstamo
        W.exec(
            "UPDATE estados "
            "SET renovaciones = renovaciones + 1, "
            "    fecha_devolucion_prevista = fecha_devolucion_prevista + INTERVAL '7 days' "
            "WHERE id_estado = " + std::to_string(id_estado));
            
        W.commit();
        return "Préstamo renovado exitosamente por 7 días adicionales.";
    }
    catch (const std::exception &e){
        return std::string("Error: ") + e.what();
    }
}

/**
 * @brief Function to process a book return request with specific priority logic.
 * 
 * @param codigo Book code
 * @param sede Location (0 or 1)
 * @param C Database connection
 * @return std::string Result message
 */
std::string returnBook(int codigo, int sede, pqxx::connection &C){
    int sede_real = sede + 1;

    try {
        pqxx::work W(C);
        
        // 1. Buscar todos los préstamos activos del libro en esta sede
        //    y seleccionar el que tiene el MAYOR número de renovaciones
        pqxx::result R = W.exec(
            "SELECT e.id_estado, e.id_libro "
            "FROM estados e "
            "JOIN libros l ON l.id_libro = e.id_libro "
            "WHERE l.codigo = " + std::to_string(codigo) + 
            " AND e.sede = " + std::to_string(sede_real) + 
            " AND e.tipo_operacion = 'prestamo' "
            "ORDER BY e.renovaciones DESC, e.fecha_operacion DESC "
            "LIMIT 1"
        );

        if (R.empty()) {
            return "Error: No se encontró un préstamo activo para este libro en esta sede.";
        }

        int id_estado = R[0]["id_estado"].as<int>();
        int id_libro = R[0]["id_libro"].as<int>();

        // 2. Marcar ese préstamo como 'devuelto'
        W.exec(
            "UPDATE estados "
            "SET tipo_operacion = 'devuelto' "
            "WHERE id_estado = " + std::to_string(id_estado)
        );

        // 3. Incrementar los ejemplares disponibles en la sede correcta
        std::string columna_ejemplares = (sede_real == 1) ? "ejemplares_sede1" : "ejemplares_sede2";
        W.exec(
            "UPDATE libros "
            "SET " + columna_ejemplares + " = " + columna_ejemplares + " + 1 "
            "WHERE id_libro = " + std::to_string(id_libro)
        );

        W.commit();
        return "Devolución exitosa. Ejemplar disponible nuevamente.";
    }
    catch (const std::exception &e) {
        return std::string("Error en BD: ") + e.what();
    }
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
int main(int argc, char *argv[]){
    std::vector<std::string> directionsPool;
    std::int8_t loc;
    if (argc == 1){
        std::cerr << "Cannot stablish conection without library location\n";
        return 0;
    }
    else if (argc == 2){
        obtainEnvData(directionsPool);
        loc = std::int8_t(std::stoi(argv[1]));
        loc--;
        if (loc >= std::int8_t(directionsPool.size())){
            std::cout << "This location does not exist.\n";
            return 0;
        }
    }
    else{
        std::cerr<<"Run format is ./fileName #Location";
        return 0;
    }
    if (int(loc) == 0){
        zmq::context_t context(1);
        zmq::socket_t socketOne(context, zmq::socket_type::rep);
        std::string completeSocketDir = "tcp://";
        completeSocketDir.append(directionsPool[loc]);
        completeSocketDir.append(":5560");
        socketOne.bind(completeSocketDir);

        zmq::socket_t socketTwo(context, zmq::socket_type::pub);
        completeSocketDir = "tcp://";
        completeSocketDir.append(directionsPool[loc]);
        completeSocketDir.append(":5561");
        socketTwo.bind(completeSocketDir);

        while (true){
            zmq::message_t request;
            zmq::recv_result_t result = socketOne.recv(request, zmq::recv_flags::none);
            if (!result){
                std::cerr << "ERROR\n";
                return 0;
            }
            Request req;
            memcpy(&req, request.data(), sizeof(Request));
            
            printRequestInformation(req);

            std::string reply;
            try{
                std::string conect = "dbname=root user=root password=root host=localhost port=5434";
                pqxx::connection C(conect);
                std::int8_t locat = req.location;
                locat++;
                switch (int(req.requestType)){
                    case 0:
                        std::cout << "Option to loan\n";
                        reply = loanBook(req.code, req.location, C);
                        std::cout << "\n\nResponse: " << reply << "\n\n";
                        
                        // Solo enviar réplica si la operación fue exitosa
                        if (reply.find("Error") == std::string::npos) {
                            sendAsyncGaRequest("replica", req, socketTwo);
                            std::cout << "Message sent to the replica\n";
                        }
                        
                        socketOne.send(zmq::buffer(reply), zmq::send_flags::none);
                        std::cout << "Returned to the request socket\n";
                        break;
                    case 1:
                        std::cout << "Option to renew\n";
                        reply = renewBook(req.code, req.location, C);
                        std::cout << "\n\nResponse: " << reply << "\n\n";
                        sendAsyncGaRequest("replica", req, socketTwo);
                        std::cout << "Message sent to the replica\n";
                        socketOne.send(zmq::buffer(reply), zmq::send_flags::none);
                        std::cout << "Returned to the request socket\n";
                        break;
                    case 2:
                        std::cout << "Option to return\n";
                        reply = returnBook(req.code, req.location, C);
                        std::cout << "\n\nAnswer: " << reply << "\n\n";
                        sendAsyncGaRequest("replica", req, socketTwo);
                        std::cout << "Message sent to the replica\n";
                        socketOne.send(zmq::buffer(reply), zmq::send_flags::none);
                        std::cout << "Returned to the request socket\n";
                        break;
                }
            }
            catch (const std::exception &e){
                std::cerr << "CONECTION ERROR";
            }
        }
    }
    else if (loc == 1){
        zmq::context_t context(1);
        zmq::socket_t socketOne(context, zmq::socket_type::sub);
        std::string completeSocketDir = "tcp://";
        completeSocketDir.append(directionsPool[0]);
        completeSocketDir.append(":5561");
        socketOne.connect(completeSocketDir);
        std::string topic = "replica";
        socketOne.set(zmq::sockopt::subscribe, topic);

        while (true){
            zmq::message_t topicMessage;
            zmq::message_t message;
            zmq::recv_result_t resultOne = socketOne.recv(topicMessage, zmq::recv_flags::none);
            zmq::recv_result_t resultTwo = socketOne.recv(message, zmq::recv_flags::none);
            if (!resultOne || !resultTwo){
                std::cerr << "ONE OF THE TWO MESSAGES COULD NOT BE OBTAINED\n";
                return 0;
            }
            Request req;
            memcpy(&req, message.data(), sizeof(Request));
            printRequestInformation(req);
            std::string reply;
            try{
                std::string conect = "dbname=root user=root password=root host=localhost port=5434";
                std::cout << "Connection is: " << conect << "\n";
                pqxx::connection C(conect);
                std::int8_t locat = req.location;
                locat++;
                std::cout << "The code is: " << int(req.requestType) << "\n";
                switch (int(req.requestType)){
                    case 0:
                        std::cout << "Option to loan\n";
                        reply = loanBook(req.code, req.location, C);
                        std::cout << "Loan processed in replica: " << reply << "\n";
                        break;
                    case 1:
                        std::cout << "Option to renew\n";
                        reply = renewBook(req.code, req.location, C);
                        break;
                    case 2:
                        std::cout << "Option to return\n";
                        reply = returnBook(req.code, req.location, C);
                        break;
                }
            }
            catch (const std::exception &e){
                std::cerr << "CONECTION ERROR";
            }
        }
    }
}