CPP=g++
CFLAGS=-g -std=c++0x -Wall -Wextra -pedantic
LIBS=
SRC=../src
all: qparser
qparser: $(SRC)/*.cpp
	$(CPP) $(CFLAGS) -o qparser $(SRC)/*.cpp

clean:
	rm -f *.o qparser 
