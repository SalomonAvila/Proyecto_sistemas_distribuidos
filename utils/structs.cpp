#include <iostream>
using namespace std;



struct Solicitud{
    TipoSolicitud tipo;
    int32_t codigo;
    
};


//ENUMERACIONES
enum struct TipoSolicitud{
    PRESTAMO,
    RENOVACION,
    DEVOLUCION
};

enum struct EstadoPrestamo{
    ACTIVO,
    VENCIDO,
    CERRADO
};

enum struct Rol{
    ESTUDIANTE,
    PROFESOR
};

enum struct Sede{
    UNO,
    DOS
};