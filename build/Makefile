CXX = g++
CXXFLAGS = -g -std=c++17 -Wall -Wextra -O3 -I $(ROOT_DIR)/include/
LDFLAGS  = -L./lib64 -lmysqlcppconn8 -lpthread

ROOT_DIR = ../

# Source files
SERVER_SRC = $(ROOT_DIR)/src/server/KVServer.cpp
CLIENT_SRC = $(ROOT_DIR)/src/client/load_generator.cpp

# Object files
SERVER_OBJ = server.o
CLIENT_OBJ = load_generator.o

# Executables
SERVER_EXE = server
CLIENT_EXE = load_generator

# Default target
all: $(SERVER_EXE) $(CLIENT_EXE)

# Link server executable
$(SERVER_EXE): $(SERVER_OBJ)
	$(CXX) $(SERVER_OBJ) -o $(SERVER_EXE) $(LDFLAGS)

# Link client executable
$(CLIENT_EXE): $(CLIENT_OBJ)
	$(CXX) $(CLIENT_OBJ) -o $(CLIENT_EXE) $(LDFLAGS)

# Compile server.o (depends on LRUCache.h)
$(SERVER_OBJ): $(SERVER_SRC) $(ROOT_DIR)/include/LRUCache.h
	$(CXX) $(CXXFLAGS) -c $(SERVER_SRC) -o $(SERVER_OBJ)

# Compile client.o
$(CLIENT_OBJ): $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) -c $(CLIENT_SRC) -o $(CLIENT_OBJ)

# Clean
clean:
	rm -f $(SERVER_OBJ) $(CLIENT_OBJ) $(SERVER_EXE) $(CLIENT_EXE)

