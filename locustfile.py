import zmq
import struct
import time
import random
from locust import User, task, between, events
from threading import Lock

class LibraryUserBase(User):
    abstract = True
    wait_time = between(0.1, 0.5)
    
    available_books = None
    loaned_books = None
    returnable_books = None
    state_lock = None
    
    @classmethod
    def initialize_shared_state(cls):
        if cls.available_books is None:
            cls.state_lock = Lock()
            cls.available_books = {code: 2 for code in range(100001, 100021)}
            cls.loaned_books = []
            cls.returnable_books = []
            print(f"[State] Initialized: {len(cls.available_books)} available books")
            print(f"[State] Each book can be renewed 2 times")
    
    def on_start(self):
        self.initialize_shared_state()
        
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.REQ)
        self.socket.setsockopt(zmq.RCVTIMEO, 10000)
        self.socket.setsockopt(zmq.SNDTIMEO, 10000)
        self.socket.connect(self.gcEndpoint)
        
        print(f"[Locust-{self.sedeName}] Connected to {self.gcEndpoint}")
    
    def on_stop(self):
        if self.socket:
            self.socket.close()
        if self.context:
            self.context.term()
        
        with self.state_lock:
            print(f"\n[State-{self.sedeName}] Final Statistics:")
            print(f"  - Available: {len(self.available_books)}")
            print(f"  - Loaned (renewable): {len(self.loaned_books)}")
            print(f"  - Returnable: {len(self.returnable_books)}")
    
    def sendToGc(self, requestType, bookCode):
        requestNames = ["LOAN", "RENEWAL", "RETURN"]
        requestName = f"{requestNames[requestType]}_{bookCode}_{self.sedeName}"
        
        try:
            requestData = struct.pack('=iib3x', requestType, bookCode, self.locationId)
        except Exception as packError:
            print(f"[Locust-{self.sedeName}] Packing error: {packError}")
            events.request.fire(
                request_type="ZMQ",
                name=requestName,
                response_time=0,
                response_length=0,
                exception=packError,
                context={}
            )
            return 0, "Packing error", False
        
        startTime = time.time()
        
        try:
            self.socket.send(requestData)
            responseMessage = self.socket.recv()
            
            responseTime = (time.time() - startTime) * 1000
            responseText = responseMessage.decode('utf-8', errors='ignore')
            
            businessErrors = [
                "No hay ejemplares disponibles",
                "No se encontró un préstamo activo",
                "Límite máximo de renovaciones alcanzado",
                "El libro no existe",
                "No available copies",
                "No active loan found",
                "Maximum renewal limit reached"
            ]
            
            isBusinessError = any(err in responseText for err in businessErrors)
            isTechnicalError = "Error" in responseText and not isBusinessError
            isSuccess = not isTechnicalError and not ("Error en BD" in responseText)
            
            events.request.fire(
                request_type="ZMQ",
                name=requestName,
                response_time=responseTime,
                response_length=len(responseMessage),
                exception=Exception(responseText) if isTechnicalError else None,
                context={}
            )
            
            if isTechnicalError:
                print(f"[Locust-{self.sedeName}] {requestNames[requestType]} TECHNICAL ERROR: {responseText[:80]}")
            elif isBusinessError:
                print(f"[Locust-{self.sedeName}] {requestNames[requestType]} Business response: {responseText[:60]}")
            
            return responseTime, responseText, isSuccess
            
        except zmq.error.Again:
            responseTime = (time.time() - startTime) * 1000
            events.request.fire(
                request_type="ZMQ",
                name=requestName,
                response_time=responseTime,
                response_length=0,
                exception=Exception("ZMQ timeout"),
                context={}
            )
            print(f"[Locust-{self.sedeName}] Timeout in {requestNames[requestType]}")
            return responseTime, "Timeout", False
            
        except Exception as generalError:
            responseTime = (time.time() - startTime) * 1000
            events.request.fire(
                request_type="ZMQ",
                name=requestName,
                response_time=responseTime,
                response_length=0,
                exception=generalError,
                context={}
            )
            print(f"[Locust-{self.sedeName}] Exception: {generalError}")
            return responseTime, str(generalError), False
    
    @task(5)
    def loanBook(self):
        with self.state_lock:
            if not self.available_books:
                return
            bookCode = random.choice(list(self.available_books.keys()))
        
        _, response, success = self.sendToGc(0, bookCode)
        
        if success:
            with self.state_lock:
                if bookCode in self.available_books:
                    renovationsRemaining = self.available_books.pop(bookCode)
                    self.loaned_books.append(bookCode)
                    self.returnable_books.append(bookCode)
                    print(f"[State-{self.sedeName}] Book {bookCode} loaned. Can be renewed {renovationsRemaining} more times")
    
    @task(2)
    def renewBook(self):
        with self.state_lock:
            if not self.loaned_books:
                return
            bookCode = random.choice(self.loaned_books)
        
        _, response, success = self.sendToGc(1, bookCode)
        
        if success:
            with self.state_lock:
                if bookCode in self.loaned_books:
                    if bookCode not in self.returnable_books:
                        self.returnable_books.append(bookCode)
                    print(f"[State-{self.sedeName}] Book {bookCode} renewed")
        elif "Límite máximo" in response or "renovaciones alcanzado" in response or "Maximum renewal" in response:
            with self.state_lock:
                if bookCode in self.loaned_books:
                    self.loaned_books.remove(bookCode)
                    print(f"[State-{self.sedeName}] Book {bookCode} reached max renewals")
    
    @task(3)
    def returnBook(self):
        with self.state_lock:
            if not self.returnable_books:
                return
            bookCode = random.choice(self.returnable_books)
        
        _, response, success = self.sendToGc(2, bookCode)
        
        if success:
            with self.state_lock:
                if bookCode in self.returnable_books:
                    self.returnable_books.remove(bookCode)
                if bookCode in self.loaned_books:
                    self.loaned_books.remove(bookCode)
                
                self.available_books[bookCode] = 2
                print(f"[State-{self.sedeName}] Book {bookCode} returned")


class LibraryUserSede1(LibraryUserBase):
    weight = 1
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.gcEndpoint = "tcp://10.43.101.228:5555"
        self.locationId = 0
        self.sedeName = "Sede1"


class LibraryUserSede2(LibraryUserBase):
    weight = 1
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.gcEndpoint = "tcp://10.43.103.31:5555"
        self.locationId = 1
        self.sedeName = "Sede2"