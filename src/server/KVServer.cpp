#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <shared_mutex>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <mutex>
#include <future>

#include "KVServer.h"
#include <LRUCache.h>

#include <httplib.h>

using namespace std::chrono_literals;
#define PORT 8080

// ------------------------------ DB ACCESS METHODS -------------------------------------

std::optional<std::string> db_select_value(MYSQL* conn, const std::string &db_name, const std::string &table_name, long long key) ; 
bool db_upsert(MYSQL* conn, const std::string &db_name, const std::string &table_name, long long key, const std::string &value) ; 
std::pair<bool, uint64_t> db_delete(MYSQL* conn, const std::string &db_name, const std::string &table_name, long long key) ;

// -------------------------------------------------------------------------------------

KVServer::KVServer(const std::string &db_user,
                   const std::string &db_password,
                   const std::string &db_host,
                   const std::string &db_name,
                   size_t pool_size,
                   size_t cache_capacity,
                   const std::string &table_name)
        :  _pool(pool_size),
          _dbpool(DBPool (db_host, PORT, db_user, db_password, db_name, pool_size)),
          _db_name(db_name),
          _table_name(table_name),
          _cache(cache_capacity, 8)
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
#ifdef DEBUG_MODE
    std::cout << "Get : " << key_param << std::endl ;
#endif

    std::string value ;
    
    int int_key = 0;
    try {
        int_key = std::stoll(key_param);
    } catch (const std::exception &e) {
        res.status = 400;
        res.set_content("Key must be an integer", "text/plain");
        return;
    }

    if (_cache.Get(int_key, value)) {
        // Cache Hit: Read the value from the cache and return immediately 
#ifdef DEBUG_MODE
        std::cout << "[CACHE HIT]" << " Value : " << value << std::endl ;
#endif
        res.status = 200 ; // OK
        res.set_content(value, "text/plain") ;
        return ; // Skip the database access and mutex lock
    }

    // Acquire DB connection
#if 0
    // Async DB read using thread pool
    auto fut = _pool.submit([this, int_key]() -> std::optional<std::string> {
            auto conn = _dbpool.acquire();
            return db_select_value(conn.get(), _db_name, _table_name, int_key);
            });

    // Wait for DB result (can also return immediately with async response if needed)
    auto opt = fut.get();
    if (!opt.has_value()) {
        res.status = 404; res.set_content("Key not found", "text/plain");
        return;
    }

    value = opt.value();
    _cache.Put(int_key, value);
    res.status = 200; res.set_content(value, "text/plain");
#else
    auto conn = _dbpool.acquire();
    if (!conn) {
        res.status = 503;
        res.set_content("No DB connection available", "text/plain");
        return;
    }

    auto opt = db_select_value(conn.get(), _db_name, _table_name, int_key);
    if (!opt.has_value()) {
        res.status = 404;
        res.set_content("Key not found", "text/plain");
        return;
    }

    value = opt.value();
    // update cache
    _cache.Put(int_key, value);

    res.status = 200;
    res.set_content(value, "text/plain");
#endif
}

void KVServer::HandlePut(const httplib::Request& req, httplib::Response& res)
{
    std::string key_param = req.get_param_value("key");
    std::string value_param = req.get_param_value("value");
#ifdef DEBUG_MODE
    std::cout << "Put: " << key_param << " " << value_param << " " << std::endl ;
#endif

    // Some sanity checks before everything else
    if (key_param.empty() || value_param.empty()) {
        res.status = 400 ; // Bad Request
        res.set_content("Missing Key/Value parameter", "text/plain") ;
        return ;
    }

    int int_key = 0;
    try {
        int_key = std::stoll(key_param);
    } catch (const std::exception &e) {
        res.status = 400;
        res.set_content("Key must be an integer", "text/plain");
        return;
    }

    // Acquire DB connection
#if 1
    auto fut = _pool.submit([this, int_key, value_param]() -> bool {
            auto conn = _dbpool.acquire();
            return db_upsert(conn.get(), _db_name, _table_name, int_key, value_param);
            });

    bool ok = false;
    try { ok = fut.get(); } catch (...) { ok = false; }

    if (!ok) {
        res.status = 500;
        res.set_content("Database write failed", "text/plain");
        return;
    }

    _cache.Put(int_key, value_param);
    res.status = 200;
    res.set_content("Key-value pair stored successfully", "text/plain");

#else
    auto conn = _dbpool.acquire();
    if (!conn) {
        res.status = 503;
        res.set_content("No DB connection available", "text/plain");
        return;
    }

    bool ok = db_upsert(conn.get(), _db_name, _table_name, int_key, value_param);
    if (!ok) {
        res.status = 503;
        std::string err = mysql_error(conn.get());
        res.set_content(std::string("Database write failed: ") + err, "text/plain");
        return;
    }

    // Update cache
    _cache.Put(int_key, value_param);

    res.status = 200;
    res.set_content("Key-value pair stored successfully", "text/plain");
#endif
}

void KVServer::HandleDelete(const httplib::Request &req, httplib::Response &res)
{
    std::string key_param = req.get_param_value("key");
    // Some sanity checks before everything else
    if (key_param.empty()) {
        res.status = 400 ;
        res.set_content("Missing Key parameter", "text/plain") ;
        return ;
    }
    long long int_key = 0;
    try { int_key = std::stoll(key_param); }
    catch (...) {
        res.status = 400;
        res.set_content("Key must be integer", "text/plain");
        return;
    }
#if 1
    auto fut = _pool.submit([this, int_key]() -> std::pair<bool,uint64_t> {
            auto conn = _dbpool.acquire();
            return db_delete(conn.get(), _db_name, _table_name, int_key);
            });

    bool ok = false;
    uint64_t affected = 0;
    try {
        auto res_pair = fut.get();
        ok = res_pair.first;
        affected = res_pair.second;
    } catch (...) { ok = false; }

    if (!ok) {
        res.status = 500;
        res.set_content("Database delete failed", "text/plain");
        return;
    }

    if (affected > 0) {
        _cache.Erase(int_key);
        res.status = 200;
        res.set_content("Key deleted successfully", "text/plain");
    } else {
        res.status = 404;
        res.set_content("Key not found in database", "text/plain");
    }

#else 
    auto conn = _dbpool.acquire();
    if (!conn) {
        res.status = 503;
        res.set_content("No DB connection available", "text/plain");
        return;
    }

    auto [ok, affected] = db_delete(conn.get(), _db_name, _table_name, int_key);
    if (!ok) {
        res.status = 503;
        std::string err = mysql_error(conn.get());
        res.set_content(std::string("Database delete failed: ") + err, "text/plain");
        return;
    }

    if (affected > 0) {
        _cache.Erase(int_key);
        res.status = 200;
        res.set_content("Key deleted successfully", "text/plain");
    } else {
        res.status = 404;
        res.set_content("Key not found in database", "text/plain");
    }
#endif
}

void KVServer::HandleGetPopular(const httplib::Request& req, httplib::Response& res)
{
    // Some sanity checks before everything else
    std::string key_param = req.get_param_value("key");
    // This is a bad request, the parameter is missing
    if (key_param.empty()) {
        res.status = 400 ; // Bad Request
        res.set_content("Missing Key parameter", "text/plain") ;
        return ;
    }
#ifdef DEBUG_MODE
    std::cout << "Get : " << key_param << std::endl ;
#endif

    std::string value ;
    
    int int_key = 0;
    try {
        int_key = std::stoll(key_param);
    } catch (const std::exception &e) {
        res.status = 400;
        res.set_content("Key must be an integer", "text/plain");
        return;
    }

    if (_cache.Get(int_key, value)) {
        // Cache Hit: Read the value from the cache and return immediately 
#ifdef DEBUG_MODE
        std::cout << "[CACHE HIT]" << " Value : " << value << std::endl ;
#endif
        res.status = 200 ; // OK
        res.set_content(value, "text/plain") ;
        return ; // Skip the database access and mutex lock
    }

    // Acquire DB connection
#if 1
    // Async DB read using thread pool
    auto fut = _pool.submit([this, int_key]() -> std::optional<std::string> {
            auto conn = _dbpool.acquire();
            return db_select_value(conn.get(), _db_name, _table_name, int_key);
            });

    // Wait for DB result (can also return immediately with async response if needed)
    auto opt = fut.get();
    if (!opt.has_value()) {
        res.status = 404; res.set_content("Key not found", "text/plain");
        return;
    }

    value = opt.value();
    _cache.Put(int_key, value);
    res.status = 200; res.set_content(value, "text/plain");
#else
    auto conn = _dbpool.acquire();
    if (!conn) {
        res.status = 503;
        res.set_content("No DB connection available", "text/plain");
        return;
    }

    auto opt = db_select_value(conn.get(), _db_name, _table_name, int_key);
    if (!opt.has_value()) {
        res.status = 404;
        res.set_content("Key not found", "text/plain");
        return;
    }

    value = opt.value();
    // update cache
    _cache.Put(int_key, value);

    res.status = 200;
    res.set_content(value, "text/plain");
#endif
}

// --------------------------- Utility helpers ---------------------------
std::string esc_string(MYSQL* conn, const std::string &s) 
{
    if (!conn) return "";
    std::string out;
    out.resize(s.size() * 2 + 1);
    unsigned long out_len = mysql_real_escape_string(conn, out.data(), s.c_str(), s.size());
    out.resize(out_len);
    return out;
}

// ----------------------------------------------------------------------------

std::optional<std::string> db_select_value(MYSQL* conn, const std::string &db_name, const std::string &table_name, long long key) 
{
    if (!conn) return std::nullopt;
    // Build query: SELECT value FROM db.table WHERE k = <key> LIMIT 1
    std::string q = "SELECT value FROM " + db_name + "." + table_name + " WHERE k = " + std::to_string(key) + " LIMIT 1";
    if (mysql_query(conn, q.c_str())) {
        // error
        return std::nullopt;
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return std::nullopt;
    MYSQL_ROW row = mysql_fetch_row(res);
    std::optional<std::string> ret = std::nullopt;
    if (row) {
        unsigned long *lengths = mysql_fetch_lengths(res);
        if (lengths && lengths[0] > 0) {
            ret = std::string(row[0], lengths[0]);
        } else {
            ret = std::string(); // empty string
        }
    }
    mysql_free_result(res);
    return ret;
}

bool db_upsert(MYSQL* conn, const std::string &db_name, const std::string &table_name, long long key, const std::string &value) 
{
    if (!conn) return false;
    std::string val_esc = esc_string(conn, value);
    std::string q = "INSERT INTO " + db_name + "." + table_name + " (k, value) VALUES (" +
                    std::to_string(key) + ", '" + val_esc + "') ON DUPLICATE KEY UPDATE value = VALUES(value)";
    if (mysql_query(conn, q.c_str())) {
        return false;
    }
    return true;
}

std::pair<bool, uint64_t> db_delete(MYSQL* conn, const std::string &db_name, const std::string &table_name, long long key)
{
    if (!conn) return {false, 0};
    std::string q = "DELETE FROM " + db_name + "." + table_name + " WHERE k = " + std::to_string(key);
    if (mysql_query(conn, q.c_str())) {
        return {false, 0};
    }
    my_ulonglong affected = mysql_affected_rows(conn);
    return {true, static_cast<uint64_t>(affected)};
}

void KVServer::Run(int port) 
{
    // Runnnn Forrresst Runnnn
    setup_routes() ;
    std::cout << "Listening on 0.0.0.0:" << port << std::endl;

    if (!_http_server.listen("0.0.0.0", port)) {
        std::cerr << "Failed to bind to port " << port << std::endl;
    }
}

int main() 
{
    KVServer server(getenv("DB_USER"), getenv("DB_PASS"), getenv("DB_HOST"), getenv("DB_NAME"), 8);
    return 0;
}

