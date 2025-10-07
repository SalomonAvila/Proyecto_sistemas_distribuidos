# Entrega 1

  

## Tecnologías usadas

Para este proyecto, se usó el lenguaje C++ gracias a la rapidez que este ofrece al momento de ejecutarse. Adicionalmente, se usó la librería de ØMQ (en C++ tomada de zmq.hpp).

Por último, para poder tener un mecanismo de persistencia se usó PostgreSQL y su visualización pgAdmin usando un archivo docker-compose.

![C++](https://img.shields.io/badge/C++-00599C?style=flat-square&logo=C%2B%2B&logoColor=white) 
![Docker Compose](https://img.shields.io/badge/Docker%20Compose-061D2F?style=flat-square&logo=docker&logoColor=white)
![PostgreSQL](https://img.shields.io/badge/postgresql-4169e1?style=flat-square&logo=postgresql&logoColor=white)



## Configurar entorno

Para configurar este entorno, basta con:

 1. `git clone https://github.com/SalomonAvila/Proyecto_sistemas_distribuidos.git`
 2. Crear un archivo .env en la raíz de este siguiendo la estructura del .env.example donde la IP de la primera máquina es donde está alojada la BD primaria.


## Compilación

> Nota: Es posible que se requieran permisos de administrador usar `sudo`

Se ha incluido, por practicidad, un archivo Makefile en el cual se crea una carpeta de nombre *build* donde se almacenan los ejecutables. Se sugiere siempre primero limpiar cualquier resto con `make clean` y luego compilar los archivos con `make all`.

Para poder levantar la BD y el panel de visualización, se debe utilizar docker-compose usando `docker-compose up -d`. 

Si desea reiniciar el servicio desde 0, creando nuevamente las tablas y realizando las inserciones base (contempladas en el archivo ***init.sql***) basta con usar `docker-compose down -v` y luego nuevamente `docker-compose up -d`.



## Ejecución

Como se ha planteado en el enunciado, se prevé el uso de 3 máquinas. En la primera de estas solo se ejecutarán procesos solicitantes. En la segunda (cuya IP corresponde con el valor de *IP_SEDE_1* de las variables de entorno) se ejecutará el gestor de carga de la sede, los actores correspondientes a las operaciones que se deseen probar de la sede y por último el gestor de archivos de la sede. En la última máquina (cuya IP corresponde con el valor de *IP_SEDE_2* de las variables de entorno) se ejecutará el gestor de carga de la sede, los actores correspondientes a las operaciones que se deseen probar de la sede y por último el gestor de archivos de la sede.