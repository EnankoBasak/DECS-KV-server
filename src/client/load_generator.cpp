#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>
#include <random>
#include <algorithm>

#include "httplib.h"

// --- Configuration Constants ---
const std::string DEFAULT_SERVER_URL = "localhost" ;
const int DEFAULT_SERVER_PORT = 8080 ;
const int DEFAULT_TIMEOUT = 5  ;

// Key space size limits
const long long LARGE_KEY_SPACE = 10e4 ; // For Put All / Get All / Delete All
const int SMALL_KEY_SPACE = 100 ;         // For Get Popular
const size_t VALUE_SIZE = 256 ;           // Payload size
static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz" ;

// Enum to define the executable workload type
enum WorkloadType {
    PUT_ALL,
    GET_ALL,
    DELETE_ALL,
    GET_POPULAR,
    GET_PUT_MIX,
    GET_DELETE_MIX
} ;

// --- Shared Metrics Structure ---
struct SharedMetrics {
    std::atomic<long long> total_successful_requests{0} ;
    std::atomic<long long> total_latency_ns{0} ; // Nanoseconds
    std::atomic<long long> total_requests_sent{0} ;
} ;

// --- Key/Value Generation Logic ---

/**
 * @brief Generates a unique integer key (used for Put All / Get All / Delete All).
 * Keys wrap around the LARGE_KEY_SPACE to prevent running out of key IDs while 
 * maintaining a consistent workload that hits the database. 
 */
long long generate_key(std::mt19937& rng) {
    return (rng() % LARGE_KEY_SPACE) ; 
}

/**
 * @brief Generates a popular key (used for Get Popular to force cache hits).
 */
long long generate_popular_key(std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(1, SMALL_KEY_SPACE) ; 
    return dist(rng) ;
}

/**
 * @brief Generates a random value string of fixed size.
 */
std::string generate_value() {
    std::string result(VALUE_SIZE, 0) ;
    
    thread_local static std::mt19937 generator(std::random_device{}()) ;
    thread_local static std::uniform_int_distribution<> distribution(0, sizeof(charset) - 2) ;

    for (size_t i = 0 ; i < VALUE_SIZE ; ++i) {
        result[i] = charset[distribution(generator)] ;
    }
    return result ;
}

// --- Request Execution Functions ---

bool execute_get(httplib::Client& client, const std::string& key) 
{
    std::string path_with_params = "/get?key=" + key ;
    if (auto res = client.Get(path_with_params)) {
        return (res->status == 200)  ;
    }
    return false ;
}

bool execute_put(httplib::Client& client, const std::string& key) 
{
    std::string value = generate_value() ;
    std::string path_with_params = "/put?key=" + key + "&value=" + value ;
    if (auto res = client.Put(path_with_params, "", "text/plain")) {
        return (res->status == 200)  ; 
    }
    return false ;
}

bool execute_delete(httplib::Client& client, const std::string& key) 
{
    std::string path_with_params = "/delete?key=" + key ;
    if (auto res = client.Delete(path_with_params)) {
        // 200 OK (deleted) or 404 (not found, which is functionally equivalent to deleted)
        return (res->status == 200 || res->status == 404)  ; 
    }
    return false ;
}

bool execute_popular(httplib::Client& client) 
{
    std::string path_with_params = "/get_popular" ;
    if (auto res = client.Get(path_with_params)) {
        return (res->status == 200)  ;
    }
    return false ;
}

// --- Core Load Generation Logic ---

/**
 * @brief Executes one request based on the selected workload type.
 */
bool execute_workload_request( httplib::Client& client, WorkloadType workload, std::mt19937& rng) 
{
    long long key_int = 0 ;
    std::string key_str ;
    
    switch (workload) {
        case PUT_ALL:
            key_int = generate_key(rng) ;
            key_str = std::to_string(key_int) ;
            return execute_put(client, key_str) ;

        case GET_ALL:
            key_int = generate_key(rng) ;
            key_str = std::to_string(key_int) ;
            return execute_get(client, key_str) ;
            
        case DELETE_ALL:
            key_int = generate_key(rng) ;
            key_str = std::to_string(key_int) ;
            return execute_delete(client, key_str) ;

        case GET_POPULAR:
            // Get Popular: Repeated keys, forces Cache Hit -> CPU bound 
            key_int = generate_popular_key(rng) ;
            key_str = std::to_string(key_int) ;
            return execute_popular(client) ;

        case GET_PUT_MIX:
        case GET_DELETE_MIX: {
            // Mixed Workloads: Use unique keys to ensure a blend of cache hits and misses
            key_int = generate_key(rng) ;
            key_str = std::to_string(key_int) ;
            
            // Randomly choose the operation (50/50 split)
            if (rng() % 2 == 0) {
                return execute_get(client, key_str) ;
            } else {
                if (workload == GET_PUT_MIX) {
                    return execute_put(client, key_str) ;
                } else { // GET_DELETE_MIX
                    return execute_delete(client, key_str) ;
                }
            }
        }

        default:
            return false ; 
    }
}

/**
 * @brief Represents a single client thread in the closed loop.
 */
void client_worker( int id, int port, std::chrono::seconds duration, WorkloadType workload, SharedMetrics* metrics) 
{
    std::mt19937 rng(std::random_device{}() + id) ;
    
    httplib::Client client(DEFAULT_SERVER_URL, port) ;
    client.set_connection_timeout(std::chrono::seconds(DEFAULT_TIMEOUT)) ;
    client.set_read_timeout(std::chrono::seconds(DEFAULT_TIMEOUT)) ;
    client.set_write_timeout(std::chrono::seconds(DEFAULT_TIMEOUT)) ;

    auto end_test_time = std::chrono::steady_clock::now() + duration ;

    // Closed-loop execution
    while (std::chrono::steady_clock::now() < end_test_time) {
        
        auto request_start = std::chrono::steady_clock::now() ;
        bool success = execute_workload_request(client, workload, rng) ;
        auto request_end = std::chrono::steady_clock::now() ;
        
        metrics->total_requests_sent.fetch_add(1) ;
        if (success) {
            auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(request_end - request_start) ;
            metrics->total_successful_requests.fetch_add(1) ;
            metrics->total_latency_ns.fetch_add(latency.count()) ;
        }
    }
}

// --- Main Execution and Reporting ---

WorkloadType parse_workload(const std::string& w_str) {
    if (w_str == "put") return PUT_ALL ;
    if (w_str == "get") return GET_ALL ;
    if (w_str == "delete") return DELETE_ALL ;
    if (w_str == "get_popular") return GET_POPULAR ;
    if (w_str == "get_put_mix") return GET_PUT_MIX ;
    if (w_str == "get_delete_mix") return GET_DELETE_MIX ;
    throw std::invalid_argument("Invalid workload type: " + w_str) ;
}

int main(int argc, char* argv[]) {
    // Default Parameters
    int concurrency = 1 ;
    int duration_sec = 10 ;
    int port = DEFAULT_SERVER_PORT ;
    std::string workload_str = "get_popular" ;
    
    // Argument Parsing: Expects <concurrency> <duration> <workload> 
    if (argc >= 2) concurrency = std::stoi(argv[1]) ;
    if (argc >= 3) duration_sec = std::stoi(argv[2]) ;
    if (argc >= 4) workload_str = argv[3] ;

    try {
        WorkloadType workload = parse_workload(workload_str) ;
        
        if (concurrency <= 0 || duration_sec <= 0) {
            std::cerr << "Error: Concurrency and duration must be positive integers." << std::endl ;
            return 1 ;
        }

        std::cout << "Starting Unified Load Test:" << std::endl ;
        std::cout << "  Workload: " << workload_str << std::endl ;

        // Execution logic (omitted for brevity, same as before)
        SharedMetrics metrics ;
        std::vector<std::thread> workers ;
        std::chrono::seconds test_duration(duration_sec) ;
        
        auto test_start_time = std::chrono::steady_clock::now() ;
        for (int i = 0 ; i < concurrency ; ++i) {
            workers.emplace_back(client_worker, i, port, test_duration, workload, &metrics) ;
        }

        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join() ;
            }
        }
        auto test_end_time = std::chrono::steady_clock::now() ;


        // Reporting logic 
        auto actual_duration = std::chrono::duration_cast<std::chrono::duration<double>>(test_end_time - test_start_time) ;
        long long successful_requests = metrics.total_successful_requests.load() ;
        long long requests = metrics.total_requests_sent.load() ;

        std::cout << "\n--- Load Test Summary ---" << std::endl ;
        
        if (successful_requests > 0) {
            double duration_s = actual_duration.count() ;
            double total_latency_ms = (double)metrics.total_latency_ns.load() / 1e6 ;

            double avg_throughput = (double)successful_requests / duration_s ;
            double avg_response_time = total_latency_ms / (double)successful_requests ;

            std::cout << "Total Requests: " << requests << std::endl ;
            std::cout << "Total Successful Requests: " << successful_requests << std::endl ;
            std::cout << "Test Duration: " << std::fixed << std::setprecision(2) << duration_s << " s" << std::endl ;
            std::cout << "Average Throughput: " << std::fixed << std::setprecision(2) << avg_throughput << " req/s" << std::endl ;
            std::cout << "Average Response Time: " << std::fixed << std::setprecision(3) << avg_response_time << " ms" << std::endl ;

        } else {
            std::cout << "No successful requests completed." << std::endl ;
        }

        std::cout << "-------------------------" << std::endl ;

    } catch (const std::exception& e) {
        std::cerr << "Error during setup or execution: " << e.what() << std::endl ;
        std::cerr << "Supported workloads: put_all, get_all, delete_all, get_popular, get_put_mix, get_delete_mix" << std::endl ;
        return 1 ;
    }
    return 0 ;
}
