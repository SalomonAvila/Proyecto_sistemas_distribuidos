 all:
	mkdir build
	g++ src/ps/ps.cpp -o build/ps -lzmq
	g++ src/gc/gc.cpp -o build/gc -lzmq
	g++ src/actores/actorDevolucion.cpp -o build/ad -lzmq
	g++ src/actores/actorRenovacion.cpp -o build/ar -lzmq
	g++ src/ga/ga.cpp -o build/ga -lzmq -lpqxx -lpq
clean:
	rm -rf build