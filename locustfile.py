import zmq
import struct
import time
import random
from locust import User, task, between, events

class LibraryUser(User):
    wait_time = between(1, 3)
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.context = None
        self.socket = None
        self.gcEndpoint = "tcp://192.168.1.10:5555"
        self.availableBookCodes = list(range(100001, 100021))
        self.locationId = 0
        
    def on_start(self):
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.REQ)
        self.socket.setsockopt(zmq.RCVTIMEO, 5000)
        self.socket.setsockopt(zmq.SNDTIMEO, 5000)
        self.socket.connect(self.gcEndpoint)
        print(f"[Locust] Connected to GC at {self.gcEndpoint}")
    
    def on_stop(self):
        if self.socket:
            self.socket.close()
        if self.context:
            self.context.term()
        print("[Locust] Disconnected from GC")
    
    def sendLibraryRequest(self, requestType, bookCode, locationId):
        requestName = f"{['LOAN', 'RENEWAL', 'RETURN'][requestType]}_{bookCode}_LOC{locationId + 1}"
        startTime = time.time()
        responseText = ""
        exceptionError = None
        
        try:
            requestData = struct.pack('iBb', bookCode, requestType, locationId)
            self.socket.send(requestData)
            
            responseMessage = self.socket.recv()
            responseText = responseMessage.decode('utf-8')
            
            totalTime = int((time.time() - startTime) * 1000)
            
            if self.isConnectionError(responseText):
                events.request.fire(
                    request_type="ZMQ",
                    name=requestName,
                    response_time=totalTime,
                    response_length=len(responseText),
                    exception=Exception(f"Connection error: {responseText}"),
                    context=self.context
                )
            else:
                events.request.fire(
                    request_type="ZMQ",
                    name=requestName,
                    response_time=totalTime,
                    response_length=len(responseText),
                    exception=None,
                    context=self.context
                )
                
        except zmq.Again:
            totalTime = int((time.time() - startTime) * 1000)
            exceptionError = Exception("ZMQ timeout: No response from server")
            events.request.fire(
                request_type="ZMQ",
                name=requestName,
                response_time=totalTime,
                response_length=0,
                exception=exceptionError,
                context=self.context
            )
            self.reconnect()
            
        except zmq.ZMQError as zmqError:
            totalTime = int((time.time() - startTime) * 1000)
            exceptionError = Exception(f"ZMQ error: {str(zmqError)}")
            events.request.fire(
                request_type="ZMQ",
                name=requestName,
                response_time=totalTime,
                response_length=0,
                exception=exceptionError,
                context=self.context
            )
            self.reconnect()
            
        except Exception as generalError:
            totalTime = int((time.time() - startTime) * 1000)
            exceptionError = Exception(f"Unexpected error: {str(generalError)}")
            events.request.fire(
                request_type="ZMQ",
                name=requestName,
                response_time=totalTime,
                response_length=0,
                exception=exceptionError,
                context=self.context
            )
            self.reconnect()
    
    def isConnectionError(self, responseText):
        connectionErrorKeywords = [
            "Could not process",
            "All",
            "attempts failed",
            "ZMQ error",
            "timeout",
            "connection refused",
            "Network is unreachable"
        ]
        
        responseLower = responseText.lower()
        for keyword in connectionErrorKeywords:
            if keyword.lower() in responseLower:
                return True
        return False
    
    def reconnect(self):
        try:
            if self.socket:
                self.socket.close()
            self.socket = self.context.socket(zmq.REQ)
            self.socket.setsockopt(zmq.RCVTIMEO, 5000)
            self.socket.setsockopt(zmq.SNDTIMEO, 5000)
            self.socket.connect(self.gcEndpoint)
            print("[Locust] Reconnected to GC")
        except Exception as reconnectError:
            print(f"[Locust-Error] Failed to reconnect: {str(reconnectError)}")
    
    @task(3)
    def performLoan(self):
        bookCode = random.choice(self.availableBookCodes)
        self.sendLibraryRequest(0, bookCode, self.locationId)
    
    @task(1)
    def performRenewal(self):
        bookCode = random.choice(self.availableBookCodes)
        self.sendLibraryRequest(1, bookCode, self.locationId)
    
    @task(2)
    def performReturn(self):
        bookCode = random.choice(self.availableBookCodes)
        self.sendLibraryRequest(2, bookCode, self.locationId)