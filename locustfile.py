from locust import User, task, between, events
import zmq
import struct
import time
import random

class LibraryUser(User):
    """
    Usuario virtual que simula un Proceso Solicitante (PS)
    Se comunica SOLO con el Gestor de Carga (GC) en puerto 5555
    """
    wait_time = between(0.1, 0.5)
    
    def on_start(self):
        """Conectar al GC"""
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.REQ)
        

        gc_address = "tcp://127.0.0.1:5555"
        print(f"[Locust] Connecting to GC at {gc_address}")
        
        self.socket.connect(gc_address)
        self.socket.setsockopt(zmq.RCVTIMEO, 10000)
        self.socket.setsockopt(zmq.SNDTIMEO, 10000)
  
        self.all_book_codes = list(range(100001, 101001))
 
        loaned_ids = [
            965, 560, 158, 357, 189, 839, 766, 713, 996, 609, 742, 675, 738, 173, 301, 844, 98, 994, 380, 518, 
            82, 56, 558, 505, 880, 711, 820, 135, 646, 677, 139, 103, 662, 776, 497, 755, 473, 393, 566, 470, 
            842, 802, 324, 236, 15, 858, 193, 212, 168, 979, 345, 116, 463, 405, 632, 487, 574, 998, 765, 878, 
            981, 886, 438, 370, 672, 643, 298, 924, 975, 170, 623, 200, 939, 933, 456, 361, 338, 999, 11, 841, 
            292, 241, 263, 534, 666, 445, 684, 2, 122, 66, 840, 671, 347, 394, 577, 12, 579, 685, 319, 751, 
            618, 238, 312, 797, 909, 720, 207, 903, 120, 141, 516, 506, 325, 450, 564, 747, 467, 413, 343, 656, 
            451, 626, 205, 49, 53, 821, 795, 262, 699, 415, 34, 853, 123, 342, 285, 307, 398, 232, 138, 352, 
            901, 90, 160, 440, 88, 41, 541, 287, 268, 781, 452, 493, 294, 448, 466, 727, 230, 693, 616, 810, 
            726, 775, 533, 132, 224, 990, 348, 993, 177, 344, 960, 787, 83, 952, 983, 935, 24, 959, 185, 866, 
            568, 874, 272, 832, 822, 420, 3, 896, 545, 315, 446, 654, 741, 815, 311, 593, 169, 914, 313, 882
        ]
        self.loaned_book_codes = [100000 + id_libro for id_libro in loaned_ids]
        
        self.location = 1
    
    def send_to_gc(self, request_type, code):
        """
        Envía request SOLO al GC (puerto 5555)
        El GC se encarga de routear a los actores correspondientes
        
        Args:
            request_type: 0=LOAN, 1=RENEWAL, 2=RETURN
            code: Código del libro (100001-101000)
        """
        
        try:
            request_data = struct.pack('=iib3x', request_type, code, self.location)
        except Exception as e:
            print(f"[Locust] Packing error: {e}")
            return 0, "Packing error"
        
        start_time = time.time()
        request_names = ["LOAN", "RENEWAL", "RETURN"]
        request_name = request_names[request_type]
        
        try:

            self.socket.send(request_data)
            
            response = self.socket.recv()
            
            response_time = (time.time() - start_time) * 1000
            response_text = response.decode('utf-8', errors='ignore')
            
    
            is_error = "Error" in response_text or "error" in response_text.lower()

            events.request.fire(
                request_type="ZMQ",
                name=request_name,
                response_time=response_time,
                response_length=len(response),
                exception=Exception(response_text) if is_error else None,
                context={}
            )
            
            if is_error:
                print(f"[Locust] {request_name} FAILED: {response_text[:50]}")
            
            return response_time, response_text
            
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
            return response_time, "Timeout"
            
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
            return response_time, str(e)
    
    @task(5)
    def loan_book(self):
        """Prestar un libro (SÍNCRONO vía GC → AP → GA)"""
        code = random.choice(self.all_book_codes)
        self.send_to_gc(0, code)
    
    @task(2)
    def renew_book(self):
        """Renovar un libro (ASÍNCRONO vía GC → AR)"""
        code = random.choice(self.loaned_book_codes)
        self.send_to_gc(1, code)
    
    @task(3)
    def return_book(self):
        """Devolver un libro (ASÍNCRONO vía GC → AD)"""
        code = random.choice(self.loaned_book_codes)
        self.send_to_gc(2, code) 
    
    def on_stop(self):
        """Cerrar conexión"""
        self.socket.close()
        self.context.term()