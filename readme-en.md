
# Delivery 1

  

## Technologies used

For this project, the C++ language was used due to its speed of execution. Additionally, the ØMQ library (in C++ taken from zmq.hpp) was used.

Finally, PostgreSQL and its pgAdmin visualization were used to provide a persistence mechanism, using a docker-compose file.

![C++](https://img.shields.io/badge/C++-00599C?style=flat-square&logo=C%2B%2B&logoColor=white) 
![Docker Compose](https://img.shields.io/badge/Docker%20Compose-061D2F?style=flat-square&logo=docker&logoColor=white)
![PostgreSQL](https://img.shields.io/badge/postgresql-4169e1?style=flat-square&logo=postgresql&logoColor=white)



## Configure environment

To configure this environment, simply:

 1. `git clone https://github.com/SalomonAvila/Proyecto_sistemas_distribuidos.git`
 2. Create a .env file in the root directory following the structure of .env.example, where the IP address of the first machine is where the primary database is hosted.


## Compilation

> Note: Administrator permissions may be required to use `sudo`

For practical reasons, a Makefile has been included in which a folder named *build* is created to store the executables. It is recommended to always clean up any remnants first with `make clean` and then compile the files with `make all`.

To start the database and the display panel, you must use docker-compose with `docker-compose up -d`.

If you want to restart the service from scratch, recreating the tables and performing the base insertions (contemplated in the ***init.sql*** file), simply use `docker-compose down -v` and then `docker-compose up -d` again.



## Execution

As stated in the description, three machines are expected to be used. Only requesting processes will be executed on the first machine. The second machine (whose IP corresponds to the value of *IP_SEDE_1* in the environment variables) will execute the headquarters load manager, the actors corresponding to the operations to be tested at headquarters, and finally the headquarters file manager. On the last machine (whose IP corresponds to the value of *IP_SEDE_2* in the environment variables), the headquarters load manager will be executed, followed by the actors corresponding to the headquarters operations to be tested and, finally, the headquarters file manager.