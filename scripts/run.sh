#!/bin/bash

# --- Configuration ---
SERVER_HOST="localhost"
SERVER_PORT="8080"
LOAD_GENERATOR_BIN="../build/load_generator" # Path to your compiled load generator
SERVER_BIN="../build/server"                 # Path to your compiled server

# Database credentials (ensure these are set in your shell environment before running the script)
# export DB_USER="kvuser"
# export DB_PASSWORD="#YourPassword"
# export DB_HOST="localhost"
# DB_PORT=33060 # Default MySQL X Protocol port
# export DB_NAME="kvstore"

EXPERIMENT_DURATION_SEC=300 # 5 minutes per run
WARMUP_TIME_SEC=10          # Time to let server stabilize before collecting client metrics
SLEEP_BETWEEN_RUNS=10       # Seconds to wait between starting server and load generator

# --- CPU Affinity Settings ---
SERVER_CPU_CORE=0 # Server process will be pinned to CPU core 0
CLIENT_CPU_CORE=4 # Load generator process will be pinned to CPU core 4
# Make sure these cores exist on your system! Use `lscpu` to check.

# Load levels (number of concurrent threads for the load generator)
# Must have at least 5 different load levels.
LOAD_LEVELS=(1 2 4 8 16 32 64 128) # Example: Adjust based on your server's capacity

# Workloads to test (must have at least two for different bottlenecks)
WORKLOADS=("get_popular" "put_all" "get_all")

# Output directories
LOG_DIR="./logs"
CLIENT_LOG_DIR="${LOG_DIR}/client"
SERVER_LOG_DIR="${LOG_DIR}/server"

# --- Setup Functions ---

setup_environment() {
    mkdir -p "$CLIENT_LOG_DIR" "$SERVER_LOG_DIR"
    echo "Logs will be stored in $LOG_DIR"
    
    # Check if DB environment variables are set
    if [[ -z "$DB_USER" || -z "$DB_PASS" || -z "$DB_HOST" || -z "$DB_NAME" ]]; then
        echo "ERROR: Database environment variables (DB_USER, DB_PASS, DB_HOST, DB_PORT, DB_NAME) must be set."
        echo "Example: export DB_USER=\"kvuser\""
        exit 1
    fi

    # Check if load generator and server binaries exist
    if [[ ! -f "$LOAD_GENERATOR_BIN" ]]; then
        echo "ERROR: Load generator binary not found at $LOAD_GENERATOR_BIN. Please build the project."
        exit 1
    fi
    if [[ ! -f "$SERVER_BIN" ]]; then
        echo "ERROR: Server binary not found at $SERVER_BIN. Please build the project."
        exit 1
    fi

    # Check for taskset availability
    if ! command -v taskset &> /dev/null; then
        echo "WARNING: 'taskset' command not found. CPU affinity will not be applied."
        echo "Please install 'util-linux' package (e.g., 'sudo apt-get install util-linux' on Debian/Ubuntu)."
        SERVER_TASKSET_CMD=""
        CLIENT_TASKSET_CMD=""
    else
        SERVER_TASKSET_CMD="taskset -c $SERVER_CPU_CORE"
        CLIENT_TASKSET_CMD="taskset -c $CLIENT_CPU_CORE"
        echo "CPU affinity enabled: Server on Core $SERVER_CPU_CORE, Client on Core $CLIENT_CPU_CORE."
    fi
}

start_server() {
    echo "Starting server on CPU Core $SERVER_CPU_CORE..."
    # Launch server in background with DB credentials and CPU affinity, redirecting its output to a log file
    # Ensure server.cpp's main() calls _http_server.listen() in a blocking manner for taskset to work correctly
    DB_USER=$DB_USER DB_PASS=$DB_PASS DB_HOST=$DB_HOST DB_NAME=$DB_NAME \
    $SERVER_TASKSET_CMD "$SERVER_BIN" > "${SERVER_LOG_DIR}/server_output_${WORKLOAD_TYPE}_${THREADS}threads.log" 2>&1 &
    SERVER_PID=$!
    echo "Server started with PID: $SERVER_PID"
    sleep $SLEEP_BETWEEN_RUNS # Give server some time to initialize
}

stop_server() {
    if [[ -n "$SERVER_PID" ]]; then
        echo "Stopping server (PID: $SERVER_PID)..."
        kill "$SERVER_PID"
        wait "$SERVER_PID" 2>/dev/null # Wait for server to terminate, suppress "No such process" error
        echo "Server stopped."
    fi
}

# --- Resource Monitoring Function (Background) ---
# NOTE: This is for basic monitoring on the SAME host. For robust analysis, 
# consider tools like `atop` or `perf` on the server host directly.
# This version explicitly targets the pinned server process.

monitor_resources() {
    local workload_type=$1
    local threads=$2
    local duration=$3
    local start_time=$(date +%s)
    local log_file="${SERVER_LOG_DIR}/resource_utilization_${workload_type}_${threads}threads.log"

    echo "Monitoring resources for server PID $SERVER_PID on Core $SERVER_CPU_CORE. Logging to $log_file"
    echo "Timestamp,CPU_Percent,Memory_KB,CPU_Core" > "$log_file"

    while true; do
        current_time=$(date +%s)
        elapsed=$((current_time - start_time))

        if [[ "$elapsed" -ge "$duration" ]]; then
            echo "Resource monitoring completed."
            break
        fi

        # Capture CPU usage and Memory (RSS in KB) for the specific PID
        # Also capture the CPU core it's currently running on (should be $SERVER_CPU_CORE)
        CPU_USAGE=$(ps -p "$SERVER_PID" -o %cpu --no-headers 2>/dev/null | xargs)
        MEMORY_USAGE_KB=$(ps -p "$SERVER_PID" -o rss --no-headers 2>/dev/null | xargs)
        CURRENT_CPU_CORE=$(ps -p "$SERVER_PID" -o psr --no-headers 2>/dev/null | xargs) # Processor (CPU) that the process last executed on

        if [[ -z "$CPU_USAGE" || -z "$MEMORY_USAGE_KB" ]]; then
            echo "WARN: Server PID $SERVER_PID not found during monitoring. Terminating monitoring."
            break
        fi

        echo "$(date +%Y-%m-%d_%H-%M-%S),${CPU_USAGE},${MEMORY_USAGE_KB},${CURRENT_CPU_CORE}" >> "$log_file"
        sleep 5 # Log every 5 seconds
    done
}


# --- Main Experiment Loop ---

run_experiment() {
    local workload_type=$1
    local threads=$2

    echo -e "\n--- Running Experiment: Workload=$workload_type, Threads=$threads ---"

    # 1. Start the server on its assigned core
    start_server
    
    # 2. Start resource monitoring in background
    monitor_resources "$workload_type" "$threads" "$EXPERIMENT_DURATION_SEC" &
    MONITOR_PID=$!
    echo "Resource monitor started with PID: $MONITOR_PID"

    # 3. Run the load generator on its assigned core
    echo "Starting load generator for $EXPERIMENT_DURATION_SEC seconds on CPU Core $CLIENT_CPU_CORE..."
    CLIENT_OUTPUT_FILE="${CLIENT_LOG_DIR}/client_metrics_${workload_type}_${threads}threads.log"
    
    # Apply taskset to the load generator binary
    $CLIENT_TASKSET_CMD "$LOAD_GENERATOR_BIN" "$threads" "$EXPERIMENT_DURATION_SEC" "$workload_type" "http://${SERVER_HOST}:${SERVER_PORT}" > "$CLIENT_OUTPUT_FILE" 2>&1
    
    # 4. Extract key metrics from client output (adjust regex as per your load generator's output format)
    echo "Parsing client metrics..."
    THROUGHPUT=$(grep "Total Throughput" "$CLIENT_OUTPUT_FILE" | awk '{print $NF}')
    RESPONSE_TIME=$(grep "Average Response Time" "$CLIENT_OUTPUT_FILE" | awk '{print $NF}')
    
    echo "  Throughput: $THROUGHPUT req/s"
    echo "  Response Time: $RESPONSE_TIME ms"

    # 5. Stop resource monitoring
    if [[ -n "$MONITOR_PID" ]]; then
        echo "Stopping resource monitor (PID: $MONITOR_PID)..."
        kill "$MONITOR_PID" 2>/dev/null
        wait "$MONITOR_PID" 2>/dev/null
    fi

    # 6. Stop the server
    stop_server

    # 7. Add a small delay before the next experiment
    echo "Waiting for 10 seconds before next experiment to cool down..."
    sleep 10
}

# --- Main Script Execution ---
setup_environment

echo "Starting all load tests. Total duration per run: ${EXPERIMENT_DURATION_SEC} seconds."
echo "Approximately $(( ${#WORKLOADS[@]} * ${#LOAD_LEVELS[@]} * (${EXPERIMENT_DURATION_SEC} + $SLEEP_BETWEEN_RUNS + 10) / 60 )) minutes total runtime."


for workload in "${WORKLOADS[@]}"; do
    # Pre-population strategy:
    # For 'get_popular' and 'get_all' to be effective, data MUST exist.
    # A simple way for 'get_popular' is to put a small set of keys that will fit the cache.
    # For 'get_all' it's usually a larger set that won't fit the cache.
    # This example assumes you run a separate PUT load_generator run *before* these tests,
    # or your load generator handles pre-population for specific modes.
    
    # EXAMPLE: You might add a dedicated pre-population step here if needed
    # if [[ "$workload" == "get_popular" ]]; then
    #     echo "Pre-populating cache for get_popular..."
    #     # Example: Run a PUT only workload with low threads and limited keys
    #     $CLIENT_TASKSET_CMD "$LOAD_GENERATOR_BIN" 1 60 "put_single_key_range" "http://${SERVER_HOST}:${SERVER_PORT}" 
    #     sleep 5
    # fi

    for threads in "${LOAD_LEVELS[@]}"; do
        run_experiment "$workload" "$threads"
    done
done

echo -e "\n--- All experiments completed ---"
echo "Client logs in: $CLIENT_LOG_DIR"
echo "Server logs in: $SERVER_LOG_DIR"
echo "Next steps: Parse logs, plot graphs, and analyze bottlenecks."
