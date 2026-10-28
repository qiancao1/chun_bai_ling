#ifndef QCXXHASH_H
#define QCXXHASH_H

// ---------------------------------------------------------------------------
// XXH64 —— 非加密的 64 位内容哈希（缓存键专用）
//
// 为什么不用 QCryptographicHash::Md5：MD5 单核吞吐约 0.5 GB/s，是「读文件 + 算哈希」
// 这条链上唯一还能吃满一整个核的部分；同时发多张图/多段音视频时会把同进程的
// 收发、UI、插件逻辑挤出 CPU。XXH64 约 10~20 GB/s（快 20~40 倍），
// 64 位输出对「缓存键」这种用途足够（百万条约 3e-8 的碰撞概率）。
//
// ⚠ **只能用于缓存键 / 去重这类场景，绝不能用在上传协议的 MD5 字段上**
//   （COS 签名、分片校验要的是真 MD5，那里仍用 QCryptographicHash）。
//
// 算法按 XXH64 官方规范实现（不引第三方文件），纯 C++11、无依赖、可流式喂数据。
// 只假设小端（本项目的 Windows x64 / Linux x64 / arm64 都是小端；大端机需改 read32/read64）。
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstring>

namespace qcxxhash {

static const uint64_t kP1 = 11400714785074694791ULL;
static const uint64_t kP2 = 14029467366897019727ULL;
static const uint64_t kP3 =  1609587929392839161ULL;
static const uint64_t kP4 =  9650029242287828579ULL;
static const uint64_t kP5 =  2870177450012600261ULL;

inline uint64_t rotl64(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }

inline uint64_t read64(const uint8_t *p) { uint64_t v; std::memcpy(&v, p, 8); return v; }
inline uint32_t read32(const uint8_t *p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

inline uint64_t round64(uint64_t acc, uint64_t input)
{
    acc += input * kP2;
    acc  = rotl64(acc, 31);
    acc *= kP1;
    return acc;
}

inline uint64_t mergeRound(uint64_t acc, uint64_t val)
{
    acc ^= round64(0, val);
    acc  = acc * kP1 + kP4;
    return acc;
}

// 流式状态：update() 可以按任意长度分块喂（本项目按 256KB 读文件）
class State
{
public:
    State() { reset(); }

    void reset(uint64_t seed = 0)
    {
        m_seed  = seed;
        m_v1    = seed + kP1 + kP2;
        m_v2    = seed + kP2;
        m_v3    = seed;
        m_v4    = seed - kP1;
        m_total = 0;
        m_fill  = 0;
    }

    void update(const void *data, size_t len)
    {
        if (len == 0) return;
        const uint8_t *p   = static_cast<const uint8_t *>(data);
        const uint8_t *end = p + len;
        m_total += len;

        if (m_fill + len < 32) {                 // 凑不满一个 stripe → 先攒着
            std::memcpy(m_buf + m_fill, p, len);
            m_fill += len;
            return;
        }
        if (m_fill) {                            // 先把上次的零头补成一个完整 stripe
            const size_t need = 32 - m_fill;
            std::memcpy(m_buf + m_fill, p, need);
            consumeStripe(m_buf);
            p     += need;
            m_fill = 0;
        }
        while (p + 32 <= end) {                  // 成批消费
            consumeStripe(p);
            p += 32;
        }
        if (p < end) {                           // 剩下的留到下次
            m_fill = size_t(end - p);
            std::memcpy(m_buf, p, m_fill);
        }
    }

    uint64_t digest() const
    {
        uint64_t h;
        if (m_total >= 32) {
            h = rotl64(m_v1, 1) + rotl64(m_v2, 7) + rotl64(m_v3, 12) + rotl64(m_v4, 18);
            h = mergeRound(h, m_v1);
            h = mergeRound(h, m_v2);
            h = mergeRound(h, m_v3);
            h = mergeRound(h, m_v4);
        } else {
            h = m_seed + kP5;
        }
        h += m_total;

        const uint8_t *p   = m_buf;
        const uint8_t *end = m_buf + m_fill;
        while (p + 8 <= end) {
            h ^= round64(0, read64(p));
            h  = rotl64(h, 27) * kP1 + kP4;
            p += 8;
        }
        if (p + 4 <= end) {
            h ^= uint64_t(read32(p)) * kP1;
            h  = rotl64(h, 23) * kP2 + kP3;
            p += 4;
        }
        while (p < end) {
            h ^= uint64_t(*p) * kP5;
            h  = rotl64(h, 11) * kP1;
            ++p;
        }

        // avalanche
        h ^= h >> 33;
        h *= kP2;
        h ^= h >> 29;
        h *= kP3;
        h ^= h >> 32;
        return h;
    }

private:
    void consumeStripe(const uint8_t *p)
    {
        m_v1 = round64(m_v1, read64(p));
        m_v2 = round64(m_v2, read64(p + 8));
        m_v3 = round64(m_v3, read64(p + 16));
        m_v4 = round64(m_v4, read64(p + 24));
    }

    uint64_t m_seed  = 0;
    uint64_t m_v1    = 0, m_v2 = 0, m_v3 = 0, m_v4 = 0;
    uint64_t m_total = 0;
    uint8_t  m_buf[32];
    size_t   m_fill  = 0;
};

// 一次性算完（小数据用）
inline uint64_t hash(const void *data, size_t len, uint64_t seed = 0)
{
    State s;
    s.reset(seed);
    s.update(data, len);
    return s.digest();
}

// 转成 16 位定长 hex（当缓存键用）
inline void toHex16(uint64_t v, char out[17])
{
    static const char *kDigits = "0123456789abcdef";
    for (int i = 15; i >= 0; --i) {
        out[i] = kDigits[v & 0xF];
        v >>= 4;
    }
    out[16] = '\0';
}

} // namespace qcxxhash

#endif // QCXXHASH_H
