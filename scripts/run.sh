#!/bin/bash

# --- Configuration ---
SERVER_HOST="localhost"
SERVER_PORT="8080"
LOAD_GENERATOR_BIN="../build/load_generator" # Path to your compiled load generator
SERVER_BIN="../build/server"                 # Path to your compiled server

# Database credentials (ensure these are set in your shell environment before running the script)
# export DB_USER="kvuser"
# export DB_PASS="#YourPassword"
# export DB_HOST="localhost"
# export DB_NAME="kvstore"

EXPERIMENT_DURATION_SEC=120 # 5 minutes per run (metrics collection duration)
WARMUP_TIME_SEC=10          # Time to let server stabilize before collecting client metrics
SLEEP_BETWEEN_RUNS=10       # Seconds to wait between starting server and load generator
LOG_INTERVAL=5              # Log metrics every 5 seconds

# --- CPU Affinity Settings ---
# Adjust these cores based on your specific hardware (use 'lscpu' to check)
SERVER_CPU_CORE=19 # Server process pinned to this single core
CLIENT_CPU_CORE="0-18" # Client process allowed to run on these cores

# Load levels (number of concurrent threads for the load generator)
LOAD_LEVELS=(1 2 4 8 16 32 64) 
# WORKLOADS=("put" "get_popular" "get" "delete")
WORKLOADS=("get_popular" "get" "delete") # Example: Testing the two extreme bottlenecks

# Output directories
LOG_DIR="./logs"
CLIENT_LOG_DIR="${LOG_DIR}/client"
SERVER_LOG_DIR="${LOG_DIR}/server"
ANALYSIS_LOG_DIR="${LOG_DIR}/analysis"

# Global variables to store PIDs
SERVER_PID=""
CLIENT_PID=""
MONITOR_PID=""

# --- Time Calculation Function ---

calculate_and_print_runtime() {
    local NUM_WORKLOADS="${#WORKLOADS[@]}"
    local NUM_LOAD_LEVELS="${#LOAD_LEVELS[@]}"
    
    # Estimate time per run: EXPERIMENT_DURATION + SLEEP_BETWEEN_RUNS + WARMUP_TIME + 10s buffer for overhead
    local TIME_PER_RUN_SEC=$((EXPERIMENT_DURATION_SEC + SLEEP_BETWEEN_RUNS + WARMUP_TIME_SEC + 10))
    local TOTAL_RUNS=$((NUM_WORKLOADS * NUM_LOAD_LEVELS))
    local TOTAL_TIME_SEC=$((TOTAL_RUNS * TIME_PER_RUN_SEC))
    
    local HOURS=$(($TOTAL_TIME_SEC / 3600))
    local MINUTES=$((($TOTAL_TIME_SEC % 3600) / 60))
    local SECONDS=$(($TOTAL_TIME_SEC % 60))

    echo "====================================================="
    echo "         ESTIMATED TOTAL EXECUTION TIME              "
    echo "====================================================="
    echo "Total Workloads:     $NUM_WORKLOADS"
    echo "Total Load Levels:   $NUM_LOAD_LEVELS"
    echo "Total Experiments:   $TOTAL_RUNS runs"
    echo "Time per Run:        $TIME_PER_RUN_SEC seconds (includes warm-up and cooldown)"
    echo "-----------------------------------------------------"
    echo "TOTAL ESTIMATED TIME: $HOURS hours, $MINUTES minutes, $SECONDS seconds"
    echo "====================================================="
    echo ""
}

# --- Setup Functions ---

setup_environment() {
    mkdir -p "$CLIENT_LOG_DIR" "$SERVER_LOG_DIR" "$ANALYSIS_LOG_DIR"
    echo "Logs will be stored in $LOG_DIR"
    
    if [[ -z "$DB_USER" || -z "$DB_PASS" || -z "$DB_HOST" || -z "$DB_NAME" ]]; then
        echo "ERROR: Database environment variables (DB_USER, DB_PASS, DB_HOST, DB_NAME) must be set."
        echo "Example: export DB_USER=\"kvuser\"; export DB_PASS=\"password\"..."
        exit 1
    fi
    
    if ! command -v taskset &> /dev/null; then
        echo "WARNING: 'taskset' command not found. CPU affinity will not be applied."
        SERVER_TASKSET_CMD=""
        CLIENT_TASKSET_CMD=""
    else
        SERVER_TASKSET_CMD="taskset -c $SERVER_CPU_CORE"
        CLIENT_TASKSET_CMD="taskset -c $CLIENT_CPU_CORE"
        echo "CPU affinity enabled: Server on Core $SERVER_CPU_CORE, Client on Cores $CLIENT_CPU_CORE."
    fi
}

start_server() {
    echo "Starting server on CPU Core $SERVER_CPU_CORE..."
    # Launch server in background with DB credentials and CPU affinity, redirecting its output to a log file
    DB_USER=$DB_USER DB_PASS=$DB_PASS DB_HOST=$DB_HOST DB_NAME=$DB_NAME \
    $SERVER_TASKSET_CMD "$SERVER_BIN" > "${SERVER_LOG_DIR}/server_output_${workload_type}_${threads}threads.log" 2>&1 &
    SERVER_PID=$!
    echo "Server started with PID: $SERVER_PID"
    sleep $SLEEP_BETWEEN_RUNS # Give server some time to initialize
}

stop_processes() {
    if [[ -n "$SERVER_PID" ]]; then
        echo "Stopping server (PID: $SERVER_PID)..."
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    if [[ -n "$CLIENT_PID" ]]; then
        # The client should have finished by itself, but we kill it just in case
        # echo "Stopping client (PID: $CLIENT_PID)..."
        kill "$CLIENT_PID" 2>/dev/null
        wait "$CLIENT_PID" 2>/dev/null
    fi
    if [[ -n "$MONITOR_PID" ]]; then
        echo "Stopping resource monitor (PID: $MONITOR_PID)..."
        kill "$MONITOR_PID" 2>/dev/null
        wait "$MONITOR_PID" 2>/dev/null
    fi
}

# --- Resource Monitoring Function (Background) ---

monitor_resources() {
    local workload_type=$1
    local threads=$2
    local duration=$3
    local log_file="${SERVER_LOG_DIR}/resource_utilization_${workload_type}_${threads}threads.log"

    echo "Monitoring resources for Server PID $SERVER_PID and Client PID $CLIENT_PID. Logging to $log_file"
    echo "Timestamp,Server_CPU_pct,Server_IO_pct,Client_CPU_pct,Client_IO_pct" > "$log_file"

    # --- WARM-UP PHASE ---
    echo "Warm-up phase: Sleeping for $WARMUP_TIME_SEC seconds before logging..."
    sleep "$WARMUP_TIME_SEC"

    local start_time=$(date +%s) # Set start time AFTER warm-up
    
    echo "Logging phase: Collecting metrics for $duration seconds..."

    while true; do
        local current_time=$(date +%s)
        local elapsed=$((current_time - start_time))

        if [[ "$elapsed" -ge "$duration" ]]; then
            break
        fi

        # Check if Server PID still exists to avoid errors
        # if ! pgrep -P 1 -F /dev/null "$SERVER_PID" &>/dev/null; then
        if ! ps -p "$SERVER_PID" > /dev/null 2>&1; then
            echo "WARN: Server PID $SERVER_PID lost. Terminating monitoring for this run."
            break
        fi
        

        # Get SERVER metrics
        SERVER_METRICS=$(ps -p "$SERVER_PID" -o %cpu --no-headers 2>/dev/null | xargs)
        
        # Get CLIENT metrics
        CLIENT_METRICS=$(ps -p "$CLIENT_PID" -o %cpu --no-headers 2>/dev/null | xargs)

        if [[ -z "$SERVER_METRICS" ]]; then
            echo "WARN: Server metrics empty. Assuming PID loss. Terminating monitoring."
            break
        fi

        read -r S_CPU S_IO <<< "$SERVER_METRICS"
        read -r C_CPU C_IO <<< "$CLIENT_METRICS"

        echo "$(date +%Y-%m-%d_%H-%M-%S),${S_CPU:-0},${S_IO:-0},${C_CPU:-0},${C_IO:-0}" >> "$log_file"
        sleep "$LOG_INTERVAL"
    done
    echo "Resource monitoring completed."
}

# --- Analysis Function ---

calculate_average_metrics() {
    local workload_type=$1
    local threads=$2
    local log_file="${SERVER_LOG_DIR}/resource_utilization_${workload_type}_${threads}threads.log"
    
    if [[ ! -f "$log_file" ]]; then
        echo "WARN: Log file $log_file not found. Skipping average calculation."
        return
    fi

    local data_lines=$(tail -n +2 "$log_file") 
    local count=0
    local sum_s_cpu=0.0
    local sum_s_io=0.0
    local sum_c_cpu=0.0
    local sum_c_io=0.0

    while IFS=',' read -r timestamp s_cpu s_io c_cpu c_io; do
        # Check for empty or invalid lines
        if [[ -z "$s_cpu" || -z "$s_io" || -z "$c_cpu" || -z "$c_io" ]]; then continue; fi
        sum_s_cpu=$(echo "$sum_s_cpu + $s_cpu" | bc -l 2>/dev/null)
        sum_s_io=$(echo "$sum_s_io + $s_io" | bc -l 2>/dev/null)
        sum_c_cpu=$(echo "$sum_c_cpu + $c_cpu" | bc -l 2>/dev/null)
        sum_c_io=$(echo "$sum_c_io + $c_io" | bc -l 2>/dev/null)
        count=$((count + 1))
    done <<< "$data_lines"

    if [[ "$count" -gt 0 ]]; then
        AVG_S_CPU=$(echo "scale=2; $sum_s_cpu / $count" | bc -l)
        AVG_S_IO=$(echo "scale=2; $sum_s_io / $count" | bc -l)
        AVG_C_CPU=$(echo "scale=2; $sum_c_cpu / $count" | bc -l)
        AVG_C_IO=$(echo "scale=2; $sum_c_io / $count" | bc -l)
    else
        AVG_S_CPU=0.0
        AVG_S_IO=0.0
        AVG_C_CPU=0.0
        AVG_C_IO=0.0
    fi

    echo "$workload_type,$threads,$AVG_S_CPU,$AVG_S_IO,$AVG_C_CPU,$AVG_C_IO" >> "${ANALYSIS_LOG_DIR}/average_resource_utilization.csv"

    echo "  -> Avg Server CPU: ${AVG_S_CPU}% | Avg Server IO: ${AVG_S_IO}%"
}


# --- Main Experiment Loop ---

run_experiment() {
    local workload_type=$1
    local threads=$2
    local taskset_client_cmd="$CLIENT_TASKSET_CMD"
    
    # Total running time for the client is warmup + experiment duration
    local total_client_duration=$((WARMUP_TIME_SEC + EXPERIMENT_DURATION_SEC))

    echo -e "\n--- Running Experiment: Workload=$workload_type, Threads=$threads ---"

    # 1. Start the server on its assigned core
    start_server
    
    # 2. Run the load generator on its assigned cores (in background to get PID)
    echo "Starting load generator for $total_client_duration seconds on CPU Cores $CLIENT_CPU_CORE..."
    CLIENT_OUTPUT_FILE="${CLIENT_LOG_DIR}/client_metrics_${workload_type}_${threads}threads.log"
    
    # Run client in background and capture its PID. 
    # Note: Passing total_client_duration so it runs during warmup too.
    $taskset_client_cmd "$LOAD_GENERATOR_BIN" "$threads" "$total_client_duration" "$workload_type" "http://${SERVER_HOST}:${SERVER_PORT}" > "$CLIENT_OUTPUT_FILE" 2>&1 &
    CLIENT_PID=$!
    echo "Client started with PID: $CLIENT_PID"

    # 3. Start resource monitoring in background. 
    # It will sleep for WARMUP_TIME_SEC and then log for EXPERIMENT_DURATION_SEC.
    # monitor_resources "$workload_type" "$threads" "$EXPERIMENT_DURATION_SEC" &
    monitor_resources "$workload_type" "$threads" "$EXPERIMENT_DURATION_SEC" \
    >> "${SERVER_LOG_DIR}/resource_monitor_stdout_${workload_type}_${threads}.log" \
    2>&1 &
    MONITOR_PID=$!

    # 4. Wait for the client to finish its total run time
    wait "$CLIENT_PID"
    CLIENT_PID="" # Clear PID once finished

    # 5. Stop resource monitoring (if it hasn't finished already)
    if [[ -n "$MONITOR_PID" ]]; then
        # echo "Stopping resource monitor (PID: $MONITOR_PID)..."
        kill "$MONITOR_PID" 2>/dev/null
        wait "$MONITOR_PID" 2>/dev/null
        MONITOR_PID=""
    fi

    # 6. Extract and calculate average resource metrics
    calculate_average_metrics "$workload_type" "$threads"
    
    # 7. Stop the server and any remaining processes
    stop_processes
    SERVER_PID="" # Clear PID once finished

    # 8. Add a small delay before the next experiment to ensure socket cleanup
    echo "Waiting for 10 seconds before next experiment to cool down..."
    sleep 10
}

# --- Main Script Execution ---

# Trap Ctrl+C (SIGINT) to ensure cleanup
trap 'echo -e "\nCaught SIGINT. Stopping processes..."; stop_processes; exit 1' INT

setup_environment
calculate_and_print_runtime

# Initialize the analysis CSV header
echo "Workload,Load_Level,Avg_Server_CPU_pct,Avg_Server_IO_pct,Avg_Client_CPU_pct,Avg_Client_IO_pct" > "${ANALYSIS_LOG_DIR}/average_resource_utilization.csv"

echo "Starting all load tests..."

for workload in "${WORKLOADS[@]}"; do
    for threads in "${LOAD_LEVELS[@]}"; do
        run_experiment "$workload" "$threads"
    done
done

echo -e "\n--- All experiments completed ---"
echo "Final averaged resource utilization metrics are in: ${ANALYSIS_LOG_DIR}/average_resource_utilization.csv"
echo "Individual client logs are in: ${CLIENT_LOG_DIR}"
