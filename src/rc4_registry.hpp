#ifndef RC4_REGISTRY_HPP
#define RC4_REGISTRY_HPP

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <sqlite3.h>

class RC4Registry {
public:
    RC4Registry();
    ~RC4Registry(); 

    bool rc4 = false;
    int rc4_i = 0;
    uint64_t start_time = 0;
    uint64_t pre_time = 0;

    uint32_t register_transponder(uint64_t timestamp, uint32_t transponder_id);
    void clear();

private:
    sqlite3* db_disk = nullptr; 
    sqlite3* db_mem = nullptr;  

    void init_db(); 
    void sort_by_rc4_ids();
    uint64_t save_to_db();
    uint64_t find_id_by_transponder(uint64_t target_id);
    
   
    int sync_db(sqlite3* src, sqlite3* dest);

    std::unordered_map<uint64_t, uint64_t> lookup_cache;
};

extern RC4Registry g_rc4_registry;

#endif
