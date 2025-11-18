#include <zmq.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <pqxx/pqxx>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include "../../utils/structs.cpp"

std::atomic<bool> isRunning(true);
std::atomic<bool> isPrimaryRole(false);
std::atomic<int> lastOperationId(0);
std::mutex databaseMutex;

void obtainEnvData(std::vector<std::string> &environmentVariables){
    std::fstream configFile("../.env");
    std::string key, value;
    while (std::getline(configFile, key, '=') && std::getline(configFile, value)) {
        environmentVariables.push_back(value);
    }
}

void heartbeatPublisher(zmq::context_t &context, const std::string &ipAddress){
    zmq::socket_t heartbeatSocket(context, zmq::socket_type::pub);
    std::string heartbeatEndpoint = "tcp://" + ipAddress + ":5562";
    heartbeatSocket.bind(heartbeatEndpoint);
    
    std::cout << "[GA-Heartbeat] Started on " << heartbeatEndpoint << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    while(isRunning){
        try {
            std::string heartbeatMessage = "ALIVE:" + std::to_string(lastOperationId.load());
            heartbeatSocket.send(zmq::buffer(heartbeatMessage), zmq::send_flags::none);
            std::this_thread::sleep_for(std::chrono::seconds(2));
        } catch (const std::exception &error) {
            std::cerr << "[GA-Heartbeat] Error: " << error.what() << std::endl;
            break;
        }
    }
}

void primaryMonitor(zmq::context_t &context, const std::string &primaryIpAddress){
    zmq::socket_t monitorSocket(context, zmq::socket_type::sub);
    std::string heartbeatEndpoint = "tcp://" + primaryIpAddress + ":5562";
    monitorSocket.connect(heartbeatEndpoint);
    monitorSocket.set(zmq::sockopt::subscribe, "");
    monitorSocket.set(zmq::sockopt::rcvtimeo, 5000);
    
    std::cout << "[GA-Monitor] Listening to primary at " << heartbeatEndpoint << "\n";
    
    int missedHeartbeatCount = 0;
    const int maxMissedHeartbeats = 3;
    bool primaryWasDown = false;
    
    while(isRunning){
        zmq::message_t heartbeatMessage;
        if(monitorSocket.recv(heartbeatMessage, zmq::recv_flags::none) && heartbeatMessage.size() > 0){
            std::string heartbeatData(static_cast<char*>(heartbeatMessage.data()), heartbeatMessage.size());
            
            if(primaryWasDown){
                std::cout << "\n[GA-Recovery] Primary is back, demoting to replica\n\n";
                isPrimaryRole = false;
                primaryWasDown = false;
            }
            missedHeartbeatCount = 0;
        } else {
            missedHeartbeatCount++;
            if(missedHeartbeatCount >= maxMissedHeartbeats && !isPrimaryRole.load()){
                std::cout << "\n[GA-Failover] Primary down, promoting to primary\n\n";
                isPrimaryRole = true;
                primaryWasDown = true;
            }
        }
    }
}

void sendReplicationRequest(const std::string &topic, const Request &request, zmq::socket_t &replicationSocket){
    replicationSocket.send(zmq::buffer(topic), zmq::send_flags::sndmore);
    zmq::message_t requestMessage(sizeof(Request));
    memcpy(requestMessage.data(), &request, sizeof(Request));
    replicationSocket.send(requestMessage, zmq::send_flags::none);
}

void logDatabaseOperation(int requestType, int bookCode, int locationId, pqxx::connection &dbConnection){
    try {
        pqxx::work transaction(dbConnection);
        transaction.exec(
            "INSERT INTO operation_log (request_type, code, location, timestamp) "
            "VALUES (" + 
                transaction.quote(requestType) + ", " +
                transaction.quote(bookCode) + ", " +
                transaction.quote(locationId) + ", " +
                "NOW()" +
            ")"
        );
        pqxx::result queryResult = transaction.exec("SELECT lastval()");
        lastOperationId = queryResult[0][0].as<int>();
        transaction.commit();
    } catch(const std::exception &error){
        std::cerr << "[GA-Log] Error: " << error.what() << "\n";
    }
}

void syncFromSecondaryGA(pqxx::connection &dbConnection, const std::string &secondaryIpAddress){
    std::cout << "[GA-Sync] Starting synchronization from secondary\n";
    
    try {
        pqxx::work transaction(dbConnection);
        pqxx::result queryResult = transaction.exec("SELECT COALESCE(MAX(id), 0) FROM operation_log");
        int myLastOperationId = queryResult[0][0].as<int>();
        transaction.commit();
        
        zmq::context_t syncContext(1);
        zmq::socket_t syncSocket(syncContext, zmq::socket_type::req);
        syncSocket.connect("tcp://" + secondaryIpAddress + ":5563");
        syncSocket.set(zmq::sockopt::rcvtimeo, 5000);
        
        std::string syncRequest = "SYNC:" + std::to_string(myLastOperationId);
        syncSocket.send(zmq::buffer(syncRequest), zmq::send_flags::none);
        
        zmq::message_t syncResponse;
        if(syncSocket.recv(syncResponse, zmq::recv_flags::none)){
            std::string responseData(static_cast<char*>(syncResponse.data()), syncResponse.size());
            if(responseData != "NO_SYNC_NEEDED"){
                std::cout << "[GA-Sync] Applying " << responseData << " missing operations\n";
            }
        }
        std::cout << "[GA-Sync] Synchronization complete\n";
    } catch(const std::exception &error){
        std::cerr << "[GA-Sync] Error: " << error.what() << "\n";
    }
}

std::string processLoanRequest(int bookCode, int locationId, pqxx::connection &dbConnection){
    std::lock_guard<std::mutex> lock(databaseMutex);
    int actualSede = locationId + 1;
    std::string examplesColumn = (actualSede == 1) ? "ejemplares_sede1" : "ejemplares_sede2";

    try{
        pqxx::work transaction(dbConnection);
        
        pqxx::result bookQuery = transaction.exec(
            "SELECT id_libro, " + examplesColumn + " "
            "FROM libros "
            "WHERE codigo = " + transaction.quote(bookCode)
        );

        if (bookQuery.empty()) {
            transaction.abort();
            return "Error: Book does not exist";
        }
        
        int bookId = bookQuery[0]["id_libro"].as<int>();
        int availableExamples = bookQuery[0][examplesColumn].as<int>();

        if (availableExamples <= 0) {
            transaction.abort();
            return "Error: No available copies of this book";
        }

        transaction.exec(
            "UPDATE libros "
            "SET " + examplesColumn + " = " + examplesColumn + " - 1 "
            "WHERE id_libro = " + transaction.quote(bookId)
        );

        transaction.exec(
            "INSERT INTO estados (id_libro, tipo_operacion, fecha_operacion, fecha_devolucion_prevista, sede, renovaciones) "
            "VALUES (" +
                transaction.quote(bookId) + ", " +
                transaction.quote("prestamo") + ", " +
                "NOW(), " +
                "(NOW() + interval '14 days')::date, " +
                transaction.quote(actualSede) + ", " +
                "0" +
            ")"
        );

        pqxx::result dateResult = transaction.exec("SELECT (NOW() + interval '14 days')::date");
        std::string returnDate = dateResult[0][0].as<std::string>();
        
        transaction.commit();
        logDatabaseOperation(0, bookCode, locationId, dbConnection);
        return "Loan successful. Return date: " + returnDate;
    }
    catch (const std::exception &error){
        return std::string("Database error: ") + error.what();
    }
}

std::string processRenewalRequest(int bookCode, int locationId, pqxx::connection &dbConnection){
    std::lock_guard<std::mutex> lock(databaseMutex);
    int actualSede = locationId + 1;
    try{
        pqxx::work transaction(dbConnection);
        
        pqxx::result loanQuery = transaction.exec(
            "SELECT e.id_estado, e.renovaciones "
            "FROM estados e "
            "JOIN libros l ON l.id_libro = e.id_libro "
            "WHERE l.codigo = " + transaction.quote(bookCode) +
            " AND e.sede = " + transaction.quote(actualSede) +
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
            
        if (loanQuery.empty()) {
            transaction.abort();
            return "Error: No active loan found for this book at this location";
        }
        
        int stateId = loanQuery[0]["id_estado"].as<int>();
        int renewalCount = loanQuery[0]["renovaciones"].as<int>();
        
        if (renewalCount >= 2){
            transaction.abort();
            return "Error: Maximum renewal limit reached (2)";
        }
        
        transaction.exec(
            "UPDATE estados "
            "SET renovaciones = renovaciones + 1, "
            "    fecha_devolucion_prevista = fecha_devolucion_prevista + INTERVAL '7 days' "
            "WHERE id_estado = " + transaction.quote(stateId)
        );
            
        transaction.commit();
        logDatabaseOperation(1, bookCode, locationId, dbConnection);
        return "Loan renewed successfully for 7 additional days";
    }
    catch (const std::exception &error){
        return std::string("Database error: ") + error.what();
    }
}

std::string processReturnRequest(int bookCode, int locationId, pqxx::connection &dbConnection){
    std::lock_guard<std::mutex> lock(databaseMutex);
    int actualSede = locationId + 1;

    try {
        pqxx::work transaction(dbConnection);
        
        pqxx::result loanQuery = transaction.exec(
            "SELECT e.id_estado, e.id_libro "
            "FROM estados e "
            "JOIN libros l ON l.id_libro = e.id_libro "
            "WHERE l.codigo = " + transaction.quote(bookCode) + 
            " AND e.sede = " + transaction.quote(actualSede) + 
            " AND e.tipo_operacion = 'prestamo' "
            "ORDER BY e.renovaciones DESC, e.fecha_operacion DESC "
            "LIMIT 1"
        );

        if (loanQuery.empty()) {
            transaction.abort();
            return "Error: No active loan found for this book at this location";
        }

        int stateId = loanQuery[0]["id_estado"].as<int>();
        int bookId = loanQuery[0]["id_libro"].as<int>();

        transaction.exec(
            "UPDATE estados "
            "SET tipo_operacion = 'devuelto' "
            "WHERE id_estado = " + transaction.quote(stateId)
        );

        std::string examplesColumn = (actualSede == 1) ? "ejemplares_sede1" : "ejemplares_sede2";
        transaction.exec(
            "UPDATE libros "
            "SET " + examplesColumn + " = " + examplesColumn + " + 1 "
            "WHERE id_libro = " + transaction.quote(bookId)
        );

        transaction.commit();
        logDatabaseOperation(2, bookCode, locationId, dbConnection);
        return "Return successful. Copy available again";
    }
    catch (const std::exception &error) {
        return std::string("Database error: ") + error.what();
    }
}

void printRequestDetails(Request &request){
    int requestType = int(request.requestType);
    std::cout << "\n--------------------------";
    std::cout << "\n[GA-Request] Type: ";
    if(requestType == 0){
        std::cout << "LOAN";
    } else if(requestType == 1){
        std::cout << "RENEWAL";
    } else if(requestType == 2){
        std::cout << "RETURN";
    }
    std::cout << "\n[GA-Request] Book code: " << request.code;
    std::cout << "\n[GA-Request] Location: " << int(request.location);
    std::cout << "\n--------------------------\n";
}

int main(int argc, char *argv[]){
    std::vector<std::string> ipAddressList;
    std::int8_t locationIndex;
    
    if (argc != 2){
        std::cerr << "[GA-Error] Run format: ./ga #Location\n";
        return 0;
    }
    
    obtainEnvData(ipAddressList);
    locationIndex = std::int8_t(std::stoi(argv[1])) - 1;
    
    if (locationIndex >= std::int8_t(ipAddressList.size())){
        std::cout << "[GA-Error] This location does not exist\n";
        return 0;
    }
    
    std::string dbConnectionString = "dbname=root user=root password=root host=localhost port=5434";
    
    if (int(locationIndex) == 0){
        std::cout << "========================================\n";
        std::cout << "  PRIMARY GA - LOCATION 1\n";
        std::cout << "========================================\n";
        isPrimaryRole = true;
        
        try {
            pqxx::connection dbConnection(dbConnectionString);
            syncFromSecondaryGA(dbConnection, ipAddressList[1]);
        } catch(const std::exception &error){
            std::cerr << "[GA-Init] Could not sync: " << error.what() << "\n";
        }
        
        zmq::context_t zmqContext(1);
        
        zmq::socket_t requestSocket(zmqContext, zmq::socket_type::rep);
        std::string requestEndpoint = "tcp://" + ipAddressList[locationIndex] + ":5560";
        requestSocket.bind(requestEndpoint);
        std::cout << "[GA] REP socket on port 5560\n";

        zmq::socket_t replicationSocket(zmqContext, zmq::socket_type::pub);
        std::string replicationEndpoint = "tcp://" + ipAddressList[locationIndex] + ":5561";
        replicationSocket.bind(replicationEndpoint);
        std::cout << "[GA] PUB socket on port 5561 (replication)\n";
        
        std::thread heartbeatThread(heartbeatPublisher, std::ref(zmqContext), std::ref(ipAddressList[locationIndex]));
        std::cout << "[GA] Ready\n\n";

        while (isRunning){
            zmq::message_t incomingRequest;
            requestSocket.recv(incomingRequest, zmq::recv_flags::none);
            
            Request parsedRequest;
            memcpy(&parsedRequest, incomingRequest.data(), sizeof(Request));
            printRequestDetails(parsedRequest);

            std::string operationResult;
            try{
                pqxx::connection dbConnection(dbConnectionString);
                
                switch (int(parsedRequest.requestType)){
                    case 0:
                        operationResult = processLoanRequest(parsedRequest.code, parsedRequest.location, dbConnection);
                        if (operationResult.find("Error") == std::string::npos) {
                            sendReplicationRequest("replica", parsedRequest, replicationSocket);
                        }
                        break;
                    case 1:
                        operationResult = processRenewalRequest(parsedRequest.code, parsedRequest.location, dbConnection);
                        if (operationResult.find("Error") == std::string::npos) {
                            sendReplicationRequest("replica", parsedRequest, replicationSocket);
                        }
                        break;
                    case 2:
                        operationResult = processReturnRequest(parsedRequest.code, parsedRequest.location, dbConnection);
                        if (operationResult.find("Error") == std::string::npos) {
                            sendReplicationRequest("replica", parsedRequest, replicationSocket);
                        }
                        break;
                }
            }
            catch (const std::exception &error){
                operationResult = "Database error: " + std::string(error.what());
            }
            requestSocket.send(zmq::buffer(operationResult), zmq::send_flags::none);
        }
        
        isRunning = false;
        heartbeatThread.join();
    }
    else {
        std::cout << "========================================\n";
        std::cout << "  SECONDARY GA - LOCATION 2\n";
        std::cout << "========================================\n";
        
        zmq::context_t zmqContext(1);
        
        zmq::socket_t replicationSocket(zmqContext, zmq::socket_type::sub);
        std::string replicationEndpoint = "tcp://" + ipAddressList[0] + ":5561";
        replicationSocket.connect(replicationEndpoint);
        replicationSocket.set(zmq::sockopt::subscribe, "replica");
        replicationSocket.set(zmq::sockopt::rcvtimeo, 100);
        
        zmq::socket_t failoverSocket(zmqContext, zmq::socket_type::rep);
        std::string failoverEndpoint = "tcp://" + ipAddressList[locationIndex] + ":5560";
        failoverSocket.bind(failoverEndpoint);
        
        zmq::socket_t syncSocket(zmqContext, zmq::socket_type::rep);
        syncSocket.bind("tcp://" + ipAddressList[locationIndex] + ":5563");
        syncSocket.set(zmq::sockopt::rcvtimeo, 100);
        
        std::thread monitorThread(primaryMonitor, std::ref(zmqContext), std::ref(ipAddressList[0]));
        
        std::cout << "[GA-Replica] Ready (Standby mode)\n\n";

        while (isRunning){
            zmq::message_t topicMessage, dataMessage;
            if (replicationSocket.recv(topicMessage, zmq::recv_flags::none)) {
                replicationSocket.recv(dataMessage, zmq::recv_flags::none);
                
                Request replicatedRequest;
                memcpy(&replicatedRequest, dataMessage.data(), sizeof(Request));
                
                try{
                    pqxx::connection dbConnection(dbConnectionString);
                    std::string operationResult;
                    
                    switch (int(replicatedRequest.requestType)){
                        case 0: operationResult = processLoanRequest(replicatedRequest.code, replicatedRequest.location, dbConnection); break;
                        case 1: operationResult = processRenewalRequest(replicatedRequest.code, replicatedRequest.location, dbConnection); break;
                        case 2: operationResult = processReturnRequest(replicatedRequest.code, replicatedRequest.location, dbConnection); break;
                    }
                    std::cout << "[GA-Replica] Synced operation #" << lastOperationId.load() << "\n";
                }
                catch (const std::exception &error){
                    std::cerr << "[GA-Replica] Error: " << error.what() << "\n";
                }
            }
            
            zmq::message_t syncRequest;
            if(syncSocket.recv(syncRequest, zmq::recv_flags::none)){
                std::string requestData(static_cast<char*>(syncRequest.data()), syncRequest.size());
                std::cout << "[GA-Sync] Sync request: " << requestData << "\n";
                syncSocket.send(zmq::buffer("NO_SYNC_NEEDED"), zmq::send_flags::none);
            }
            
            if(isPrimaryRole.load()){
                zmq::message_t failoverRequest;
                if (failoverSocket.recv(failoverRequest, zmq::recv_flags::dontwait)) {
                    std::cout << "\n[GA-Primary] Processing request\n";
                    
                    Request parsedRequest;
                    memcpy(&parsedRequest, failoverRequest.data(), sizeof(Request));
                    
                    std::string operationResult;
                    try{
                        pqxx::connection dbConnection(dbConnectionString);
                        switch (int(parsedRequest.requestType)){
                            case 0: operationResult = processLoanRequest(parsedRequest.code, parsedRequest.location, dbConnection); break;
                            case 1: operationResult = processRenewalRequest(parsedRequest.code, parsedRequest.location, dbConnection); break;
                            case 2: operationResult = processReturnRequest(parsedRequest.code, parsedRequest.location, dbConnection); break;
                        }
                    }
                    catch (const std::exception &error){
                        operationResult = "Database error: " + std::string(error.what());
                    }
                    failoverSocket.send(zmq::buffer(operationResult), zmq::send_flags::none);
                }
            }
        }
        
        isRunning = false;
        monitorThread.join();
    }
    return 0;
}