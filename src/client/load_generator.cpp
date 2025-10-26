#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>
#include <sstream>
#include <random>
#include <stdexcept>
#include <string>

// Include the necessary header for the HTTP client. 
#include <httplib.h>

// --- Configuration Constants ---
const std::string DEFAULT_SERVER_URL = "localhost";
const int DEFAULT_SERVER_PORT = 8080;
const std::string DEFAULT_ENDPOINT = "/kv/";
// Key space size for the "Get Popular" workload (e.g., keys 0-99) [1]
const int KEY_SPACE_SIZE = 100;

// --- Shared Metrics Structure ---
struct SharedMetrics {
    // Atomic variables for thread-safe accumulation without heavy locking
    std::atomic<long long> total_successful_requests{0};
    std::atomic<long long> total_latency_ns{0}; // Nanoseconds
};

// --- Request Generation Logic ---

/**
 * @brief Generates a key suitable for the "Get Popular" workload.
 * It randomly selects a key index from a small, fixed range. [1]
 */
std::string generate_key(std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(0, KEY_SPACE_SIZE - 1);
    int key_index = dist(rng);
    return "key-" + std::to_string(key_index);
}

/**
 * @brief Executes a single HTTP GET request.
 * @param client The cpp-httplib client object.
 * @param key The key to retrieve.
 * @return True if the request was successful (HTTP 2xx status), false otherwise.
 */
bool execute_request(httplib::Client& client, const std::string& key) {
    std::string path = "/get?key=" + key; // if server expects query param

    
    // Execute GET operation [1]
    if (auto res = client.Get(path)) {
        // Successful connection, now check HTTP status code
        if (res->status >= 200 && res->status < 300) {
            return true;
        } else {
            // Handle unexpected status codes
            // std::cerr << "Request failed with status: " << res->status << " for key: " << key << std::endl;
            return false;
        }
    } else {
        // Handle connection or socket error (e.g., timeout, refused connection) [1]
        std::cerr << "Connection error: " << httplib::to_string(res.error()) << " for key: " << key << std::endl;
        return false;
    }
}

// --- Core Load Generation Logic ---

/**
 * @brief Represents a single client thread in the closed loop.
 * A closed-loop client sends a request, waits for the response, and immediately sends the next one. [1]
 */
void client_worker(
    int id,
    const std::string& host,
    int port,
    std::chrono::seconds duration,
    SharedMetrics* metrics) 
{
    // Use a unique random number generator for thread-safe key generation
    std::mt19937 rng(std::random_device{}() + id);
    
    // Initialize the HTTP client for this thread
    httplib::Client client(host, port);
    client.set_connection_timeout(std::chrono::seconds(5)); // Set client timeout
    client.set_read_timeout(std::chrono::seconds(5));
    client.set_write_timeout(std::chrono::seconds(5));

    auto start_time = std::chrono::steady_clock::now();
    auto end_test_time = start_time + duration;

    // Closed-loop execution
    while (std::chrono::steady_clock::now() < end_test_time) {
        
        std::string key = generate_key(rng);
        auto request_start = std::chrono::steady_clock::now();
        
        bool success = execute_request(client, key);
        
        auto request_end = std::chrono::steady_clock::now();
        
        // Measure and record metrics
        if (success) {
            auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(request_end - request_start);
            metrics->total_successful_requests.fetch_add(1);
            metrics->total_latency_ns.fetch_add(latency.count());
        }
        
        // Note: No sleep here, fulfilling the "zero think time" requirement [1]
    }
}

// --- Main Execution and Reporting ---

int main(int argc, char* argv[]) {
    // 1. Configure Command-Line Flags and Defaults
    int concurrency = 1;
    int duration_sec = 10;
    std::string host = DEFAULT_SERVER_URL;
    int port = DEFAULT_SERVER_PORT;

    if (argc >= 2) concurrency = std::atoi(argv[1]);
    if (argc >= 3) duration_sec = std::atoi(argv[2]);
    if (argc >= 4) {
        // Simple URL parsing: assumes http://host:port
        std::string url_str = argv[3];
        size_t host_start = url_str.find("://") + 3;
        size_t port_sep = url_str.find(':', host_start);

        if (port_sep!= std::string::npos) {
            host = url_str.substr(host_start, port_sep - host_start);
            port = std::atoi(url_str.substr(port_sep + 1).c_str());
        } else {
            host = url_str.substr(host_start);
        }
    }
    
    if (concurrency <= 0 || duration_sec <= 0) {
        std::cerr << "Error: Concurrency and duration must be positive integers." << std::endl;
        return 1;
    }

    std::cout << "Starting C++ Load Test (Get Popular Workload):" << std::endl;
    std::cout << "  Target URL: " << host << ":" << port << DEFAULT_ENDPOINT << std::endl;
    std::cout << "  Concurrency (-c): " << concurrency << " threads" << std::endl;
    std::cout << "  Duration (-d): " << duration_sec << " seconds" << std::endl;

    // 2. Setup Resources
    SharedMetrics metrics;
    std::vector<std::thread> workers;
    std::chrono::seconds test_duration(duration_sec);

    // 3. Launch Workers
    auto test_start_time = std::chrono::steady_clock::now();
    for (int i = 0; i < concurrency; ++i) {
        workers.emplace_back(client_worker, i, host, port, test_duration, &metrics);
    }

    // 4. Wait for all threads to finish their full duration
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    auto test_end_time = std::chrono::steady_clock::now();

    // Actual measured duration
    auto actual_duration = std::chrono::duration_cast<std::chrono::duration<double>>(test_end_time - test_start_time);

    // 5. Calculate and Display Final Metrics [1]
    std::cout << "\n--- Load Test Summary ---" << std::endl;

    long long successful_requests = metrics.total_successful_requests.load();

    if (successful_requests > 0) {
        double duration_s = actual_duration.count();
        double total_latency_ms = (double)metrics.total_latency_ns.load() / 1e6; // Convert total nanoseconds to milliseconds

        // Average Throughput (req/s)
        double avg_throughput = (double)successful_requests / duration_s;
        
        // Average Response Time (ms)
        double avg_response_time = total_latency_ms / (double)successful_requests;

        std::cout << "Total Successful Requests: " << successful_requests << std::endl;
        std::cout << "Test Duration: " << std::fixed << std::setprecision(2) << duration_s << " s" << std::endl;
        std::cout << "Average Throughput: " << std::fixed << std::setprecision(2) << avg_throughput << " req/s" << std::endl;
        std::cout << "Average Response Time: " << std::fixed << std::setprecision(3) << avg_response_time << " ms" << std::endl;

    } else {
        std::cout << "No successful requests completed. Check server connection." << std::endl;
    }

    std::cout << "-------------------------" << std::endl;

    return 0;
}
