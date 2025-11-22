#ifndef LRUCache_H
#define LRUCache_H

#include <list>
#include <unordered_map>
#include <mutex>
#include <string>


template <typename Key, typename Value>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) ;

    // Puts a key-value pair into the cache.
    void Put(const Key& key, const Value& value) ; 

    // Gets a value by its key. Returns std::nullopt if not found.
    bool Get(const Key& key, Value &ret_val) ; 

    // Deletes an item from the cache.
    void Erase(const Key& key) ; 

    // Return the contents of the cache in the form of key,value pair
    std::string GetContents() ;

    size_t Capacity()                                   { return _capacity ; }
    size_t Size()                                       { return _item_map.size() ; }

private:
    size_t _capacity;
    std::list<std::pair<Key, Value>> _item_list;
    std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator> _item_map;
    // std::mutex _mutex;
};

template <typename Key, typename Value>
LRUCache<Key, Value>::LRUCache (size_t capacity):
         _capacity(capacity)
{

}


template <typename Key, typename Value>
void LRUCache<Key, Value>::Put(const Key& key, const Value& value)
{
    // _mutex.lock() ;

    // If key exists, update value and move to front.
    auto it = _item_map.find(key);
    if (it != _item_map.end()) {
        it->second->second = value;
        _item_list.splice(_item_list.begin(), _item_list, it->second);
        // _mutex.unlock() ;
        return;
    }

    // If cache is full, evict the least recently used item.
    if (_item_list.size() == _capacity) {
        Key lru_key = _item_list.back().first;
        _item_list.pop_back();
        _item_map.erase(lru_key);
    }

    // Add the new item to the front.
    _item_list.emplace_front(key, value);
    _item_map[key] = _item_list.begin();
    // _mutex.unlock() ;
}


template <typename Key, typename Value>
bool LRUCache<Key, Value>::Get(const Key& key, Value &ret_val) {
    // _mutex.lock() ;

    auto it = _item_map.find(key);
    if (it == _item_map.end()) {
        // _mutex.unlock() ;
        // std::cout << "CACHE MISS \n" ;
        return false ; // Cache Miss
    }

    // std::cout << "CACHE HIT\n" ;
    // Move accessed item to the front of the list.
    _item_list.splice(_item_list.begin(), _item_list, it->second);
    ret_val = it->second->second; // Cache Hit
    // _mutex.unlock() ;
    return true ;
}


template <typename Key, typename Value>
void LRUCache<Key, Value>::Erase(const Key& key) {
    // _mutex.lock() ;

    auto it = _item_map.find(key);
    if (it != _item_map.end()) {
        _item_list.erase(it->second);
        _item_map.erase(it);
    }
    // _mutex.unlock() ;
}

template <typename Key, typename Value>
std::string LRUCache<Key, Value>::GetContents()
{
    // _mutex.lock() ;
    std::string body = "" ;
    // Iterate over the contents,a nd append to response
    for (auto &item : _item_list) {
        body += "Key = " ;
        body += std::to_string(item.first) ;
        body += " Value = " ;
        body += item.second ;
        body += "\n" ;
    }
    // _mutex.unlock() ;
    return body ;
}

class LRUShard {
public:
    LRUShard(size_t capacity) : _cache(capacity) {}

    bool Get(long long key, std::string &value) {
        std::shared_lock<std::shared_mutex> lock(_mutex);
        return _cache.Get(key, value);
    }

    void Put(long long key, const std::string &value) {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        _cache.Put(key, value);
    }

    void Erase(long long key) {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        _cache.Erase(key);
    }

private:
    LRUCache<long long, std::string> _cache;
    mutable std::shared_mutex _mutex;
};

class ShardedLRUCache {
public:
    ShardedLRUCache(size_t total_capacity, size_t shard_count)
        : _shard_count(shard_count)
    {
        if (_shard_count == 0) _shard_count = 1;
        size_t per_shard = std::max<size_t>(1, total_capacity / _shard_count);
        for (size_t i = 0; i < _shard_count; ++i)
            _shards.push_back(std::make_unique<LRUShard>(per_shard));
    }

    bool Get(long long key, std::string &value) {
        // if (key == 1) {
        //     std::cout << "GET SHARD " << shard_of(key) << std::endl ;
        // }
        return _shards[shard_of(key)]->Get(key, value);
    }

    void Put(long long key, const std::string &value) {
        // if (key == 1) {
        //     std::cout << "PUT SHARD " << shard_of(key) << std::endl ;
        // }
        _shards[shard_of(key)]->Put(key, value);
    }

    void Erase(long long key) {
        // if (key == 1) {
        //     std::cout << "DELETE SHARD " << shard_of(key) << std::endl ;
        // }
        _shards[shard_of(key)]->Erase(key);
    }

private:
    size_t shard_of(long long key) const {
        return std::hash<long long>{}(key) % _shard_count;
    }

    size_t _shard_count;
    std::vector<std::unique_ptr<LRUShard>> _shards;
};



#endif
