#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <httplib.h>
#include <LRUCache.h>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <cassert>
#include <future>
#include <mysql/mysql.h>

class ThreadPool {
public:
    ThreadPool(size_t n) : stop_flag(false) {
        if (n == 0) n = 1;
        for (size_t i = 0; i < n; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mtx);
                        cv.wait(lock, [this] { return stop_flag || !tasks.empty(); });
                        if (stop_flag && tasks.empty()) return;
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    try { task(); } catch (...) { /* ignore task exceptions */ }
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            stop_flag = true;
        }
        cv.notify_all();
        for (auto &t : workers) if (t.joinable()) t.join();
    }

    // Submit a task and get a future
    template<typename F>
    auto submit(F&& f) -> std::future<decltype(f())> {
        using R = decltype(f());
        auto task_ptr = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        std::future<R> fut = task_ptr->get_future();
        {
            std::unique_lock<std::mutex> lock(mtx);
            if (stop_flag) throw std::runtime_error("ThreadPool stopped");
            tasks.emplace([task_ptr](){ (*task_ptr)(); });
        }
        cv.notify_one();
        return fut;
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex mtx;
    std::condition_variable cv;
    bool stop_flag;
};

class DBPool {
public:
    DBPool(const std::string &host, unsigned int port,
           const std::string &user, const std::string &pass,
           const std::string &db, size_t pool_size)
        : _host(host), _user(user), _pass(pass), _db(db), _port(port), _pool_size(pool_size)
    {
        for (size_t i = 0; i < pool_size; ++i) {
            MYSQL* conn = mysql_init(nullptr);
            if (!conn) {
                throw std::runtime_error("mysql_init() failed");
            }
            // Optional: set reconnect option
            unsigned int timeout = 5;
            mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

            if (!mysql_real_connect(conn, host.c_str(), user.c_str(), pass.c_str(),
                                    db.c_str(), port, nullptr, 0)) {
                std::string err = mysql_error(conn);
                mysql_close(conn);
                throw std::runtime_error("mysql_real_connect failed: " + err);
            }

            // set charset (optional)
            mysql_set_character_set(conn, "utf8mb4");

            // push into pool
            _pool.push(conn);
        }
    }

    ~DBPool() {
        std::lock_guard lock(_mutex);
        while (!_pool.empty()) {
            MYSQL* c = _pool.front();
            _pool.pop();
            mysql_close(c);
        }
    }

    // Acquire returns a unique_ptr with custom deleter that returns connection to pool
    std::unique_ptr<MYSQL, std::function<void(MYSQL*)>> acquire() {
        std::unique_lock lock(_mutex);
        _cv.wait(lock, [&]{ return !_pool.empty(); });
        MYSQL* conn = _pool.front();
        _pool.pop();

        auto deleter = [this](MYSQL* c) {
            std::unique_lock lock(_mutex);
            _pool.push(c);
            lock.unlock();
            _cv.notify_one();
        };

        return std::unique_ptr<MYSQL, std::function<void(MYSQL*)>>(conn, deleter);
    }

private:
    std::string _host, _user, _pass, _db;
    unsigned int _port;
    size_t _pool_size;
    std::queue<MYSQL*> _pool;
    std::mutex _mutex;
    std::condition_variable _cv;
};

class KVServer {
public:
    // Constructor
    KVServer(const std::string& user, const std::string& password, const std::string& host, const std::string &db_name, size_t pool_size, size_t cache_capacity = 10000, const std::string &table_name = "kv") ;
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




private:
    httplib::Server _http_server;
    ThreadPool _pool;
    DBPool _dbpool;
    std::string _db_name;
    std::string _table_name;
    ShardedLRUCache _cache;
};
