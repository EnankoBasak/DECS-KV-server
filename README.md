# DECS-KV-server
Github repo for the implementation of HTTP Based KEY-VALUE server for CS744

## Directory Structure 

.
├── build/
│   ├── Makefile                  # Build instructions for client and server
│   ├── server                    # Compiled Server Executable
│   └── load_generator            # Compiled Unified Client Executable
└── src/
    ├── client/
    │   └── unified_load_generator.cpp  # Unified Load Test Client (PUT/GET/DELETE/MIX)
    └── server/
        ├── server.cpp              # Implementation of KVServer methods (Handlers, Run)
        └── KVServer.h              # KVServer class definition, KVCache implementation