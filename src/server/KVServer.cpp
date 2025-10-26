#include <iostream>
#include <thread>
#include <httplib.h>
#include <stdexcept>
#include "KVServer.h"
using namespace std ;

#define PORT 8080

KVServer::KVServer(const std::string &user,
                   const std::string &password,
                   const std::string &host,
                   const std::string &db_name,
                   const size_t init_cache_capacity)
    : _db_session(host, 33060, user, password),
      _db(_db_session.getSchema(db_name)),
      _kv_table(_db.getTable("kv")),
      _db_mutex(),
      _cache(init_cache_capacity)
{
    std::cout << "KVServer running." << std::endl;
    Run(PORT) ;
}

KVServer::~KVServer() 
{
    // Stop HTTP server if it’s running
    if (_http_server.is_running()) {
        _http_server.stop();
    }

    std::cout << "KVServer shut down successfully." << std::endl;
}


// Register the callback functions for get put update and delete
void KVServer::setup_routes() 
{
    _http_server.Get("/get", [this](const httplib::Request &req, httplib::Response &res) {
        HandleGet(req, res);
    });

    _http_server.Put("/put", [this](const httplib::Request &req, httplib::Response &res) {
        HandlePut(req, res);
    });

    _http_server.Delete("/delete", [this](const httplib::Request &req, httplib::Response &res) {
        HandleDelete(req, res);
    });

    _http_server.Get("/get_popular", [this](const httplib::Request &req, httplib::Response &res) {
        HandleGetPopular(req, res);
    });
}


void KVServer::HandleGet(const httplib::Request& req, httplib::Response& res)
{
    // Some sanity checks before everything else
    std::string key_param = req.get_param_value("key");
    // This is a bad request, the parameter is missing
    if (key_param.empty()) {
        res.status = 400 ; // Bad Request
        res.set_content("Missing Key parameter", "text/plain") ;
        return ;
    }
    
    std::string value ;
    bool cache_hit = false ;
    (void) cache_hit ;
    
    // FIXME
    /* ----------------------------------------------- */ 
    //      NEED TO CHECK CACHE BEFORE GOING TO DB
    /* ----------------------------------------------- */ 


    int int_key = std::stoi(key_param) ;

    // Lock
    // ------------------ Critical Region Begins --------------------------------
    _db_mutex.lock() ;

    try {
        mysqlx::RowResult result = _kv_table.select("value")
                                           .where("k = :k")
                                           .bind("k", int_key)
                                           .execute() ;
        _db_mutex.unlock() ;  // Unlock DB after query

        std::size_t row_count = result.count() ;

        if (row_count > 0) {
            mysqlx::Row row = result.fetchOne() ;
            value = row[0].get<std::string>() ;

            // Store in cache, on successful DB access
            _cache.Put(int_key, value) ;

            res.status = 200 ;  // OK
            res.set_content(value, "text/plain") ;
        } else {
            res.status = 404 ; // Not Found
            res.set_content("Key not found", "text/plain") ;
        }
    } catch (const std::exception &e) {
        _db_mutex.unlock() ;  // Unlock DB in case of an exception
        res.status = 500 ; // Internal Server Error
        res.set_content(std::string("Database error: ") + e.what(), "text/plain") ;
    }
    // ------------------ Critical Region Ends --------------------------------
    // Unlock
}

void KVServer::HandlePut(const httplib::Request& req, httplib::Response& res)
{
    std::string key_param = req.get_param_value("key");
    std::string value_param = req.get_param_value("value");

    // Some sanity checks before everything else
    if (key_param.empty() || value_param.empty()) {
        res.status = 400 ; // Bad Request
        res.set_content("Missing Key/Value parameter", "text/plain") ;
        return ;
    }

    bool cache_hit = false ;
    (void) cache_hit ;
    // FIXME
    /* ----------------------------------------------- */ 
    //      NEED TO CHECK CACHE BEFORE GOING TO DB
    /* ----------------------------------------------- */ 


    // Lock
    // ------------------ Critical Region Begins --------------------------------
    _db_mutex.lock() ;



    // ------------------ Critical Region Ends --------------------------------
    // Unlock
}

void KVServer::HandleDelete(const httplib::Request& req, httplib::Response& res)
{
    (void) req ; (void) res ;
}

void KVServer::HandleGetPopular(const httplib::Request& req, httplib::Response& res)
{
    (void) req ; (void) res ;
}




void KVServer::Run(int port)
{
    // Runnnn Forrresst Runnnn
    setup_routes();

    _http_server.new_task_queue = [] {
        // Each task (incoming request) gets its own thread
        return new httplib::ThreadPool(1); // 1 thread per task
    };

    // This will accept new connections and process each in its own thread
    if (!_http_server.listen("0.0.0.0", port)) {
        std::cerr << "❌ Failed to bind to port " << port << std::endl;
    }
}


int main()
{
    KVServer server("kvuser", "#Jhuma1611", "localhost", "kvstore", 100) ;
    // while(1) ;
    return 0 ;
}

