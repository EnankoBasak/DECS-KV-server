#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <httplib.h>
#include <LRUCache.h>
#include <mysqlx/xdevapi.h>

class KVServer {
public:
    // Constructor
    KVServer(const std::string& user, const std::string& password, const std::string& host, const std::string& db_name, const size_t init_cache_capacity);
    // Destructor
    ~KVServer() ;
    void Run(int port);

private:
    // API to set up the callback functions for the get/put routines
    void setup_routes();

    // REST API Handlers
    void HandleGet(const httplib::Request& req, httplib::Response& res);
    void HandlePut(const httplib::Request& req, httplib::Response& res);
    void HandleDelete(const httplib::Request& req, httplib::Response& res);
    void HandleGetPopular(const httplib::Request& req, httplib::Response& res);

    // Database and Cache
    mysqlx::Session _db_session;
    mysqlx::Schema _db;
    mysqlx::Table _kv_table;

    std::mutex _db_mutex;      // protects database session/table
    mutable std::shared_mutex cache_mutex; // Mutable to allow locking in const methods if needed

    httplib::Server _http_server;
    LRUCache <int, std::string> _cache;
};
