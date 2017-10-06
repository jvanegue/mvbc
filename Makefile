SRC = src/main.cpp src/bootstrap.cpp src/worker.cpp src/build.cpp src/hash.cpp src/utils.cpp src/chain.cpp
OBJ = $(SRC:.cpp=.o)
EXE = node
CC  = g++
CFLAGS = -Wall -g3 -Isrc
CPPFLAGS = -Wall -g3 -Isrc
LDFLAGS = -lcrypto

all: $(OBJ)
	$(CC) $(OBJ) -o $(EXE) $(LDFLAGS)

clean:
	rm -f $(OBJ) src/*~ node
