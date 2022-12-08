#ifndef BLOOM_FILTER_H
#define BLOOM_FILTER_H

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <list>
#include <string>
#include <bitset>

#include "md5.h"

const int BITMAP_SIZE = 104857600;

class BloomFilter
{
public:
    BloomFilter(int hash_num, int rand_seed = 114514);

    ~BloomFilter();

    void addKey(const std::string &str);

    bool hasKey(const std::string &str);

    // 获取当前的误判率
    double getFPP();

private:
    // 用以生成多个哈希种子
    int myRand();

private:
    std::bitset<BITMAP_SIZE> m_bitmap;

    static int now_rand_status;

    // 布隆过滤器的哈希次数
    int m_hashes_num;

    // 存储哈希种子
    std::list<uint> m_hash_seeds;

    // 布隆过滤器的大小
    int m_size;

    // 当前共计插入的数据个数
    int m_count;

    MD5 md5;
};

#endif // BLOOM_FILTER_H