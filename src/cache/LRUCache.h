#ifndef LRUCache_H
#define LRUCache_H

#include <list>
#include <unordered_map>
#include <mutex>
#include <optional>


template <typename Key, typename Value>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) ;

    // Puts a key-value pair into the cache.
    void Put(const Key& key, const Value& value) ; 

    // Gets a value by its key. Returns std::nullopt if not found.
    std::optional<Value> Get(const Key& key) ; 

    // Deletes an item from the cache.
    void Erase(const Key& key) ; 

    size_t Capacity()           { return _capacity ; }

private:
    size_t _capacity;
    std::list<std::pair<Key, Value>> _item_list;
    std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator> _item_map;
    std::mutex _mutex;
};

template <typename Key, typename Value>
LRUCache<Key, Value>::LRUCache (size_t capacity):
         _capacity(capacity)
{

}


template <typename Key, typename Value>
void LRUCache<Key, Value>::Put(const Key& key, const Value& value)
{
    std::lock_guard<std::mutex> lock(_mutex);

    // If key exists, update value and move to front.
    auto it = _item_map.find(key);
    if (it != _item_map.end()) {
        it->second->second = value;
        _item_list.splice(_item_list.begin(), _item_list, it->second);
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
}


template <typename Key, typename Value>
std::optional<Value> LRUCache<Key, Value>::Get(const Key& key) {
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _item_map.find(key);
    if (it == _item_map.end()) {
        return std::nullopt; // Cache Miss
    }

    // Move accessed item to the front of the list.
    _item_list.splice(_item_list.begin(), _item_list, it->second);
    return it->second->second; // Cache Hit
}


template <typename Key, typename Value>
void LRUCache<Key, Value>::Erase(const Key& key) {
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _item_map.find(key);
    if (it != _item_map.end()) {
        _item_list.erase(it->second);
        _item_map.erase(it);
    }
}


#endif
