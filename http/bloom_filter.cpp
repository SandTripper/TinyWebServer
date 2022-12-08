#include "bloom_filter.h"
#include "math.h"

int BloomFilter::now_rand_status = 114514;

BloomFilter::BloomFilter(int hash_num, int rand_seed) : m_hashes_num(hash_num)
{
    m_size = BITMAP_SIZE;
    now_rand_status = rand_seed;
    for (int i = 0; i < hash_num; ++i)
    {
        m_hash_seeds.emplace_back(myRand());
    }
}

BloomFilter::~BloomFilter()
{
}

void BloomFilter::addKey(const std::string &str)
{
    for (const auto &hash_seed : m_hash_seeds)
    {
        m_bitmap.set(md5.murmur3(str.c_str(), str.length(), hash_seed) % m_size, true);
    }
}

bool BloomFilter::hasKey(const std::string &str)
{
    for (const auto &hash_seed : m_hash_seeds)
    {
        if (!m_bitmap[md5.murmur3(str.c_str(), str.length(), hash_seed) % m_size])
        {
            return false;
        }
    }
    return true;
}

double BloomFilter::getFPP()
{
    double fpp = pow(1 - exp(-(m_hashes_num * m_count) / BITMAP_SIZE), m_hashes_num);
    return fpp;
}

int BloomFilter::myRand()
{
    now_rand_status = 214013 * now_rand_status + 2531011;
    return now_rand_status >> 16 & ((1 << 15) - 1);
}
