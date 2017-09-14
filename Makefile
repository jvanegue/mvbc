SRC = main.cpp bootstrap.cpp worker.cpp build.cpp hash.cpp
OBJ = $(SRC:.cpp=.o)
EXE = node
CC  = g++
CFLAGS = -Wall -g3
LDFLAGS = -lcrypto

all: $(OBJ)
	$(CC) $(OBJ) -o $(EXE) $(LDFLAGS)

clean:
	rm -f $(OBJ) *~ node
