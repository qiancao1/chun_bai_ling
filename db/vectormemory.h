#ifndef VECTOR_MEMORY_H
#define VECTOR_MEMORY_H

#include <hnswlib.h>
#include <lmdb.h>
#include <qdebug.h>

#include <vector>
#include <string>
#include <queue>
#include <algorithm>
#include <cstring>
#include <fstream>

class VectorMemory {
public:
    // 构造函数：打开 LMDB 环境
    VectorMemory(const std::string& lmdbPath, int dim = 384, int maxElements = 100000)
        : dim_(dim), maxElements_(maxElements) {
        // 打开 LMDB 环境
        int rc = mdb_env_create(&env_);
        if (rc != MDB_SUCCESS) {
            throw std::runtime_error("mdb_env_create failed: " + std::string(mdb_strerror(rc)));
        }
        // 设置最大内存映射大小（可根据数据量调整）
        mdb_env_set_mapsize(env_, 1024 * 1024 * 1024); // 1GB
        rc = mdb_env_open(env_, lmdbPath.c_str(), MDB_WRITEMAP | MDB_NOMETASYNC, 0664);
        if (rc != MDB_SUCCESS) {
            mdb_env_close(env_);
            throw std::runtime_error("mdb_env_open failed: " + std::string(mdb_strerror(rc)));
        }
        loadOrCreateIndex();
        if (index_ && index_->getCurrentElementCount() == 0 && getCurrentCount() > 0) {
            qDebug() << "[WARN] 索引为空但 LMDB 中有向量数据，尝试重建..." ;
            rebuildIndexFromVectors();
        }
    }

    ~VectorMemory() {
        saveIndex();
        mdb_env_close(env_);
        if (space_) delete space_;
        if (index_) delete index_;
    }

    // 插入向量和元数据
    void insert(const std::vector<float>& vec, const std::string& metadata) {
        uint64_t id = getNextId();
        writeVector(id, vec);
        writeMetadata(id, metadata);
        if (id < maxElements_) {
            index_->addPoint(vec.data(), id);
        } else {
            // 超出容量，可考虑重新构建索引或忽略
            // 这里简单处理：扩容（需要重建索引）
            // 实际项目中可提前规划容量
        }
    }

    // 搜索最相似 K 条，返回 pair<id, score>
    std::vector<std::pair<uint64_t, float>> search(const std::vector<float>& query, int k = 5) {
        std::vector<std::pair<uint64_t, float>> results;
        if (index_) {
            using ResultQueue = std::priority_queue<std::pair<float, hnswlib::labeltype>>;
            ResultQueue res = index_->searchKnn(query.data(), k);
            while (!res.empty()) {
                auto p = res.top();
                res.pop();
                results.push_back({p.second, p.first}); // 注意顺序：{id, score}
            }
            // 按分数从高到低排序（默认是低到高）
            std::reverse(results.begin(), results.end());
        }
        return results;
    }

    // 根据 ID 获取元数据
    std::string getMetadata(uint64_t id) {
        return readMetadata(id);
    }

private:
    MDB_env* env_ = nullptr;
    hnswlib::L2Space* space_ = nullptr;
    hnswlib::HierarchicalNSW<float>* index_ = nullptr;
    int dim_;
    int maxElements_;

    // ---------- 索引序列化（直接存 LMDB） ----------
    void saveIndex() {
        if (!index_) return;

        std::string tempPath = "temp_index_save.bin";
        try {
            // 保存索引到临时文件
            index_->saveIndex(tempPath);

            // 检查文件是否存在且大小 > 0
            std::ifstream file(tempPath, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                throw std::runtime_error("无法打开临时文件 " + tempPath);
            }
            std::streamsize size = file.tellg();
            if (size <= 0) {
                throw std::runtime_error("索引文件大小为 0");
            }
            file.seekg(0, std::ios::beg);
            std::vector<char> buffer(size);
            if (!file.read(buffer.data(), size)) {
                throw std::runtime_error("读取临时文件失败");
            }
            file.close();
            std::remove(tempPath.c_str()); // 删除临时文件

            // 存入 LMDB
            putBinary("index_bin", buffer.data(), buffer.size());
            std::cout << "[INFO] 索引保存成功，大小: " << buffer.size() << " 字节" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] 保存索引失败: " << e.what() << std::endl;
            // 尝试清理临时文件
            std::remove(tempPath.c_str());
            // 可考虑重试或抛出异常
        }
    }

    void loadOrCreateIndex() {
        // 创建空间
        space_ = new hnswlib::L2Space(dim_);
        std::string indexData = getBinary("index_bin");

        if (!indexData.empty()) {
            std::string tempPath = "temp_index_load.bin"; // 使用独立文件名避免冲突
            try {
                std::ofstream file(tempPath, std::ios::binary);
                if (!file.is_open()) {
                    throw std::runtime_error("无法创建临时文件 " + tempPath);
                }
                file.write(indexData.c_str(), indexData.size());
                if (!file.good()) {
                    throw std::runtime_error("写入临时文件失败");
                }
                file.close();
                index_ = new hnswlib::HierarchicalNSW<float>(space_, tempPath);
                std::remove(tempPath.c_str());
                return; // 正常返回
            } catch (const std::exception& e) {

                std::remove(tempPath.c_str());

                try {
                    putBinary("index_bin", nullptr, 0);

                } catch (...) {

                }
            }
        }
        // 2. 创建新索引（初始为空）
        index_ = new hnswlib::HierarchicalNSW<float>(space_, maxElements_, 16, 200);
    }
    // 获取当前最大 ID（从计数器读取）
    uint64_t getCurrentCount() {
        MDB_txn* txn = nullptr;
        MDB_dbi dbi;
        int rc = mdb_txn_begin(env_, nullptr, MDB_RDONLY, &txn);
        if (rc != MDB_SUCCESS) throw std::runtime_error("mdb_txn_begin failed");
        rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
        if (rc != MDB_SUCCESS) { mdb_txn_abort(txn); throw std::runtime_error("mdb_dbi_open failed"); }

        uint64_t count = 0;
        MDB_val key, data;
        key.mv_data = const_cast<char*>("count");
        key.mv_size = strlen("count");
        rc = mdb_get(txn, dbi, &key, &data);
        if (rc == MDB_SUCCESS) {
            memcpy(&count, data.mv_data, sizeof(uint64_t));
        } else if (rc != MDB_NOTFOUND) {
            mdb_txn_abort(txn);
            throw std::runtime_error("mdb_get failed");
        }
        mdb_txn_abort(txn);
        return count;
    }

    void rebuildIndexFromVectors() {
        if (!index_) return;

        uint64_t maxId = getCurrentCount();
        if (maxId == 0) {
            return;
        }
        delete index_;
        index_ = new hnswlib::HierarchicalNSW<float>(space_, maxElements_, 16, 200);

        int restored = 0;
        for (uint64_t id = 1; id <= maxId; ++id) {
            std::string key = "vec_" + std::to_string(id);
            std::string data = getBinary(key);
            if (data.empty()) continue;
            if (data.size() % sizeof(float) != 0) {

                continue;
            }
            std::vector<float> vec(data.size() / sizeof(float));
            memcpy(vec.data(), data.c_str(), data.size());
            try {
                index_->addPoint(vec.data(), id);
                restored++;
            } catch (const std::exception& e) {

            }
        }
        saveIndex();
    }
    // ---------- LMDB 核心读写封装 ----------
    uint64_t getNextId() {
        // 原子递增计数器
        MDB_txn* txn = nullptr;
        MDB_dbi dbi;
        int rc = mdb_txn_begin(env_, nullptr, 0, &txn);
        if (rc != MDB_SUCCESS) throw std::runtime_error("mdb_txn_begin failed");
        rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
        if (rc != MDB_SUCCESS) { mdb_txn_abort(txn); throw std::runtime_error("mdb_dbi_open failed"); }

        uint64_t count = 0;
        MDB_val key, data;
        key.mv_data = const_cast<char*>("count");
        key.mv_size = strlen("count");
        rc = mdb_get(txn, dbi, &key, &data);
        if (rc == MDB_SUCCESS) {
            memcpy(&count, data.mv_data, sizeof(uint64_t));
        } else if (rc != MDB_NOTFOUND) {
            mdb_txn_abort(txn);
            throw std::runtime_error("mdb_get failed");
        }
        count++;
        data.mv_data = &count;
        data.mv_size = sizeof(uint64_t);
        rc = mdb_put(txn, dbi, &key, &data, 0);
        if (rc != MDB_SUCCESS) { mdb_txn_abort(txn); throw std::runtime_error("mdb_put failed"); }
        rc = mdb_txn_commit(txn);
        if (rc != MDB_SUCCESS) throw std::runtime_error("mdb_txn_commit failed");
        return count;
    }

    void writeVector(uint64_t id, const std::vector<float>& vec) {
        std::string key = "vec_" + std::to_string(id);
        putBinary(key, reinterpret_cast<const char*>(vec.data()), vec.size() * sizeof(float));
    }

    std::vector<float> readVector(uint64_t id) {
        std::string key = "vec_" + std::to_string(id);
        std::string data = getBinary(key);
        std::vector<float> vec;
        if (data.size() % sizeof(float) != 0) return vec; // 数据损坏
        vec.resize(data.size() / sizeof(float));
        memcpy(vec.data(), data.c_str(), data.size());
        return vec;
    }

    void writeMetadata(uint64_t id, const std::string& meta) {
        std::string key = "meta_" + std::to_string(id);
        putString(key, meta);
    }

    std::string readMetadata(uint64_t id) {
        std::string key = "meta_" + std::to_string(id);
        return getString(key);
    }

    void putString(const std::string& key, const std::string& val) {
        putBinary(key, val.c_str(), val.size());
    }

    std::string getString(const std::string& key) {
        return getBinary(key);
    }

    void putBinary(const std::string& key, const char* data, size_t len) {
        MDB_txn* txn = nullptr;
        MDB_dbi dbi;
        int rc = mdb_txn_begin(env_, nullptr, 0, &txn);
        if (rc != MDB_SUCCESS) throw std::runtime_error("mdb_txn_begin failed");
        rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
        if (rc != MDB_SUCCESS) { mdb_txn_abort(txn); throw std::runtime_error("mdb_dbi_open failed"); }

        MDB_val mkey, mdata;
        mkey.mv_data = const_cast<char*>(key.c_str());
        mkey.mv_size = key.size();
        mdata.mv_data = const_cast<char*>(data);
        mdata.mv_size = len;
        rc = mdb_put(txn, dbi, &mkey, &mdata, 0);
        if (rc != MDB_SUCCESS) { mdb_txn_abort(txn); throw std::runtime_error("mdb_put failed"); }
        rc = mdb_txn_commit(txn);
        if (rc != MDB_SUCCESS) throw std::runtime_error("mdb_txn_commit failed");
    }

    std::string getBinary(const std::string& key) {
        MDB_txn* txn = nullptr;
        MDB_dbi dbi;
        int rc = mdb_txn_begin(env_, nullptr, MDB_RDONLY, &txn);
        if (rc != MDB_SUCCESS) throw std::runtime_error("mdb_txn_begin failed");
        rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
        if (rc != MDB_SUCCESS) { mdb_txn_abort(txn); throw std::runtime_error("mdb_dbi_open failed"); }

        MDB_val mkey, mdata;
        mkey.mv_data = const_cast<char*>(key.c_str());
        mkey.mv_size = key.size();
        rc = mdb_get(txn, dbi, &mkey, &mdata);
        std::string result;
        if (rc == MDB_SUCCESS) {
            result.assign(reinterpret_cast<const char*>(mdata.mv_data), mdata.mv_size);
        } else if (rc != MDB_NOTFOUND) {
            mdb_txn_abort(txn);
            throw std::runtime_error("mdb_get failed");
        }
        mdb_txn_abort(txn);
        return result;
    }
};

#endif // VECTOR_MEMORY_H