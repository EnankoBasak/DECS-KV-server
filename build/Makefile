# --- Compiler Settings ---
CXX = g++
# -I flags set the include search paths relative to the Makefile (./build).
# This is where the compiler looks for header files (like httplib.h).
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -O3
# -pthread is essential for C++ multi-threading features used in the client [1]
LDFLAGS = -pthread

# --- Directory and File Definitions (Relative to./build) ---
INCLUDE = ../include
SRC_ROOT =../src
CLIENT_SRC = $(SRC_ROOT)/client/load_generator.cpp
SERVER_SRC = $(SRC_ROOT)/server/server.cpp

CLIENT_TARGET = load_generator
SERVER_TARGET = server
ALL_TARGETS = $(CLIENT_TARGET).o $(SERVER_TARGET).o

# --- Default Target ---
# Builds both the client and the server executables
.PHONY: all
all: $(ALL_TARGETS)

# --- Client Compilation Rule ---
# Compiles the load_generator client executable
$(CLIENT_TARGET).o: $(CLIENT_SRC)
	@echo "Compiling Client: $^"
	$(CXX) -I $(INCLUDE) $^ $(CXXFLAGS) $(LDFLAGS) -o $@

# --- Server Compilation Rule (Placeholder) ---
# Compiles the server executable. NOTE: This requires a server.cpp file to exist in src/server/
$(SERVER_TARGET).o: $(SERVER_SRC)
	@echo "Compiling Server: $^"
	$(CXX) -I $(INCLUDE) $^ $(CXXFLAGS) $(LDFLAGS) -o $@

# --- Clean Target ---
# Removes the compiled executables from the./build directory
.PHONY: clean
clean:
	@echo "Cleaning compiled targets..."
	rm -f $(ALL_TARGETS) *.o

# --- Run Targets ---
# Example command to run the client for 30 seconds with 16 threads
.PHONY: run-client
run-client: $(CLIENT_TARGET)
	@echo "--- Running Load Test Example (16 threads, 30s) ---"
	./$(CLIENT_TARGET) 16 30 http://localhost:8080

# Example command to run the server
.PHONY: run-server
run-server: $(SERVER_TARGET)
	@echo "--- Running Server Example ---"
	./$(SERVER_TARGET)
