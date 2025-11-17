from locust import User, task, between, events
import zmq
import struct
import time
import random
from threading import Lock

class LibraryUser(User):
    """
    Usuario virtual que simula un Proceso Solicitante (PS)
    Mantiene estado consistente con contadores de renovaciones
    """
    wait_time = between(0.1, 0.5)
    
    # Estado compartido entre todos los usuarios (thread-safe)
    available_books = None      # dict: {codigo: renovaciones_restantes}
    loaned_books = None         # list: códigos de libros prestados
    returnable_books = None     # list: códigos de libros que se pueden devolver
    state_lock = None
    
    @classmethod
    def initialize_shared_state(cls):
        """Inicializa el estado compartido una sola vez"""
        if cls.available_books is None:
            cls.state_lock = Lock()
            
            # Todos los libros disponibles con 2 renovaciones permitidas
            cls.available_books = {code: 2 for code in range(100001, 101001)}
            
            # Listas vacías inicialmente
            cls.loaned_books = []        # Libros prestados (pueden renovarse)
            cls.returnable_books = []    # Libros que se pueden devolver
            
            print(f"[State] Initialized: {len(cls.available_books)} available books")
            print(f"[State] Each book can be renewed 2 times")
    
    def on_start(self):
        """Conectar al GC e inicializar estado"""
        # Inicializar estado compartido
        self.initialize_shared_state()
        
        # Conectar ZMQ
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.REQ)
        
        gc_address = "tcp://127.0.0.1:5555"
        print(f"[Locust] Connecting to GC at {gc_address}")
        
        self.socket.connect(gc_address)
        self.socket.setsockopt(zmq.RCVTIMEO, 10000)
        self.socket.setsockopt(zmq.SNDTIMEO, 10000)
        
        self.location = 1
    
    def send_to_gc(self, request_type, code):
        """
        Envía request al GC
        
        Args:
            request_type: 0=LOAN, 1=RENEWAL, 2=RETURN
            code: Código del libro
        """
        try:
            request_data = struct.pack('=iib3x', request_type, code, self.location)
        except Exception as e:
            print(f"[Locust] Packing error: {e}")
            return 0, "Packing error", False
        
        start_time = time.time()
        request_names = ["LOAN", "RENEWAL", "RETURN"]
        request_name = request_names[request_type]
        
        try:
            self.socket.send(request_data)
            response = self.socket.recv()
            
            response_time = (time.time() - start_time) * 1000
            response_text = response.decode('utf-8', errors='ignore')
            
            # Errores de NEGOCIO que NO son fallas técnicas
            business_errors = [
                "No hay ejemplares disponibles",
                "No se encontró un préstamo activo",
                "Límite máximo de renovaciones alcanzado",
                "El libro no existe"
            ]
            
            # Solo es error técnico si NO es un error de negocio esperado
            is_business_error = any(err in response_text for err in business_errors)
            is_technical_error = "Error" in response_text and not is_business_error
            
            success = not is_technical_error and not ("Error en BD" in response_text)
            
            # Reportar a Locust (solo errores técnicos)
            events.request.fire(
                request_type="ZMQ",
                name=request_name,
                response_time=response_time,
                response_length=len(response),
                exception=Exception(response_text) if is_technical_error else None,
                context={}
            )
            
            if is_technical_error:
                print(f"[Locust] {request_name} TECHNICAL ERROR: {response_text[:80]}")
            elif is_business_error:
                print(f"[Locust] {request_name} Business response: {response_text[:60]}")
            
            return response_time, response_text, success
            
        except zmq.error.Again:
            response_time = (time.time() - start_time) * 1000
            events.request.fire(
                request_type="ZMQ",
                name=request_name,
                response_time=response_time,
                response_length=0,
                exception=Exception("Timeout"),
                context={}
            )
            print(f"[Locust] Timeout en {request_name}")
            return response_time, "Timeout", False
            
        except Exception as e:
            response_time = (time.time() - start_time) * 1000
            events.request.fire(
                request_type="ZMQ",
                name=request_name,
                response_time=response_time,
                response_length=0,
                exception=e,
                context={}
            )
            print(f"[Locust] Exception: {e}")
            return response_time, str(e), False
    
    @task(5)
    def loan_book(self):
        """Prestar un libro disponible"""
        with self.state_lock:
            if not self.available_books:
                print("[Locust] No hay libros disponibles para prestar")
                return
            code = random.choice(list(self.available_books.keys()))
        
        _, response, success = self.send_to_gc(0, code)
        
        if success:
            with self.state_lock:
                # Mover de available_books a loaned_books y returnable_books
                if code in self.available_books:
                    renovaciones_restantes = self.available_books.pop(code)
                    self.loaned_books.append(code)
                    self.returnable_books.append(code)
                    print(f"[State] Book {code} loaned. Can be renewed {renovaciones_restantes} more times")
    
    @task(2)
    def renew_book(self):
        """Renovar un libro prestado (solo si tiene renovaciones disponibles)"""
        with self.state_lock:
            if not self.loaned_books:
                print("[Locust] No hay libros prestados para renovar")
                return
            code = random.choice(self.loaned_books)
        
        _, response, success = self.send_to_gc(1, code)
        
        if success:
            with self.state_lock:
                # Verificar si el libro está en loaned_books
                if code in self.loaned_books:
                    # Encontrar el índice del libro
                    idx = self.loaned_books.index(code)
                    
                    # Si el libro existe en la lista, disminuir renovaciones
                    # Nota: Como usamos una lista simple, asumimos que cada código
                    # en loaned_books tiene renovaciones asociadas implícitamente
                    # Por simplicidad, movemos el libro a returnable si no estaba
                    if code not in self.returnable_books:
                        self.returnable_books.append(code)
                    
                    print(f"[State] Book {code} renewed")
        elif "Límite máximo" in response or "renovaciones alcanzado" in response:
            # Si alcanzó el límite, removerlo de loaned_books
            with self.state_lock:
                if code in self.loaned_books:
                    self.loaned_books.remove(code)
                    print(f"[State] Book {code} reached max renewals, removed from renewable list")
    
    @task(3)
    def return_book(self):
        """Devolver un libro prestado"""
        with self.state_lock:
            if not self.returnable_books:
                print("[Locust] No hay libros prestados para devolver")
                return
            code = random.choice(self.returnable_books)
        
        _, response, success = self.send_to_gc(2, code)
        
        if success:
            with self.state_lock:
                # Remover de returnable_books y loaned_books
                if code in self.returnable_books:
                    self.returnable_books.remove(code)
                if code in self.loaned_books:
                    self.loaned_books.remove(code)
                
                # Devolver a available_books con 2 renovaciones
                self.available_books[code] = 2
                print(f"[State] Book {code} returned. Back to available pool")
    
    def on_stop(self):
        """Cerrar conexión"""
        self.socket.close()
        self.context.term()
        
        # Imprimir estado final
        with self.state_lock:
            print(f"\n[State] Final Statistics:")
            print(f"  - Available: {len(self.available_books)}")
            print(f"  - Loaned (renewable): {len(self.loaned_books)}")
            print(f"  - Returnable: {len(self.returnable_books)}")