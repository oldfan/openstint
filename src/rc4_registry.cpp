#include "rc4_registry.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>

// 保持原有的全域 vector
std::vector<std::vector<uint32_t>> rc4_ids(1000, std::vector<uint32_t>(2, 1));

// 全域實體
RC4Registry g_rc4_registry;

RC4Registry::RC4Registry() {
    init_db();
}

RC4Registry::~RC4Registry() {

    if (db_mem && db_disk) {
        sync_db(db_mem, db_disk);
    }
    if (db_mem) sqlite3_close(db_mem);
    if (db_disk) sqlite3_close(db_disk);
}

int RC4Registry::sync_db(sqlite3* src, sqlite3* dest) {
    if (!src || !dest) return SQLITE_ERROR;
    sqlite3_backup* pBackup = sqlite3_backup_init(dest, "main", src, "main");
    if (pBackup) {
        sqlite3_backup_step(pBackup, -1);
        sqlite3_backup_finish(pBackup);
    }
    return sqlite3_errcode(dest);
}

void RC4Registry::init_db() {
    char* err_msg = 0;


    if (sqlite3_open("openstint_rc4.db", &db_disk) != SQLITE_OK) {
        std::cerr << "Open disk DB failed: " << sqlite3_errmsg(db_disk) << std::endl;
        return;
    }

    // 2. 開啟記憶體資料庫
    if (sqlite3_open(":memory:", &db_mem) != SQLITE_OK) {
        std::cerr << "Open memory DB failed: " << sqlite3_errmsg(db_mem) << std::endl;
        return;
    }

    
    sync_db(db_disk, db_mem);

  
    const char* init_sql = 
        "CREATE TABLE IF NOT EXISTS transponder_rc4 ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  transponder_id INTEGER UNIQUE,"
        "  rc4_ids TEXT,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "INSERT OR IGNORE INTO sqlite_sequence (name, seq) VALUES ('transponder_rc4', 999999);"
        "CREATE TRIGGER IF NOT EXISTS sync_transponder_id "
        "AFTER INSERT ON transponder_rc4 "
        "FOR EACH ROW "
        "WHEN NEW.transponder_id IS NULL " 
        "BEGIN "
        "  UPDATE transponder_rc4 SET transponder_id = NEW.id WHERE id = NEW.id; "
        "END;";

    if (sqlite3_exec(db_mem, init_sql, 0, 0, &err_msg) != SQLITE_OK) {
        std::cerr << "Init memory table failed: " << err_msg << std::endl;
        sqlite3_free(err_msg);
    }
}

uint64_t RC4Registry::save_to_db() { 
    sort_by_rc4_ids();
    if (rc4_i == 0 || rc4_ids[0][1] < 200) { 
        clear();
        return 0;
    }

    std::stringstream ss;
    int limit = std::min(this->rc4_i, 32);
    for (int i = 0; i < limit; i++) {
        ss << rc4_ids[i][0]; 
        if (i < limit - 1) ss << ","; 
    }
    std::string final_ids = ss.str();
    if (final_ids.empty()) return 0;

 
    std::string sql = "INSERT INTO transponder_rc4 (rc4_ids) VALUES ('" + final_ids + "');";
    char* err_msg = 0;
    uint64_t new_id = 0;

    if (sqlite3_exec(db_mem, sql.c_str(), 0, 0, &err_msg) == SQLITE_OK) {
        new_id = sqlite3_last_insert_rowid(db_mem);
        
    
        sync_db(db_mem, db_disk);
        
        std::cout << "Data saved & synced, ID: " << new_id << std::endl;
    } else {
        std::cerr << "SQL Error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
    }
    return new_id;
}


struct QueryResult {
    std::string transponder_id;
    bool found = false;
};

static int my_callback(void* data, int argc, char** argv, char** azColName) {
    QueryResult* res = static_cast<QueryResult*>(data);
    if (argc > 0 && argv[0]) {
        res->transponder_id = argv[0]; 
        res->found = true;
    }
    return 0; 
}

uint64_t RC4Registry::find_id_by_transponder(uint64_t target_id) {
    QueryResult result;
    uint64_t found_db_id = 0;

   
    std::string sql = "SELECT transponder_id FROM transponder_rc4 WHERE rc4_ids LIKE '%" 
                      + std::to_string(target_id) 
                      + "%' LIMIT 1;";

    if (sqlite3_exec(db_mem, sql.c_str(), my_callback, &result, 0) == SQLITE_OK) {
        if (result.found && !result.transponder_id.empty()) {
            found_db_id = std::stoull(result.transponder_id);
        }
    }
    return found_db_id;
}


uint32_t RC4Registry::register_transponder(uint64_t timestamp, uint32_t transponder_id) {
    uint64_t id = find_id_by_transponder(transponder_id);
    if (id != 0) {
        clear();
        return (uint32_t)id; 
    }
    if (pre_time != 0 && timestamp - pre_time > 1000) {
        clear();
        return 0;
    }     
    bool found = false;
    for (int i = 0; i < rc4_i; i++) {
        if (rc4_ids[i][0] == transponder_id) {
            rc4_ids[i][1]++;
            found = true;
            break;
        }
    }
    if (!found) {                
        if (rc4_i == 0) start_time = timestamp;
        rc4_ids[rc4_i][0] = transponder_id;
        rc4_ids[rc4_i][1] = 1;
        if (rc4_i < 999) rc4_i++;
    }
    if (pre_time != 0 && timestamp - start_time > 15000) {
        save_to_db();
        clear();
        return transponder_id;
    }
    pre_time = timestamp;
    return 0;
}

void RC4Registry::clear() {
    rc4 = false;    
    if (rc4_i > 0) {
        for (int i = 0; i < rc4_i; i++) {
            rc4_ids[i][0] = 0;
            rc4_ids[i][1] = 1;
        }
    }
    rc4_i = 0;
    pre_time = 0;
    start_time = 0;
}

void RC4Registry::sort_by_rc4_ids() {
    if (rc4_i < 2) return; 
    std::sort(rc4_ids.begin(), rc4_ids.begin() + rc4_i, 
        [](const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
            return a[1] > b[1]; 
        });
}
