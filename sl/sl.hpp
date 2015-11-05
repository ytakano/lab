#ifndef SL_HPP
#define SL_HPP

#include <stdlib.h>

template <class K, class V, int MAX_LEVEL> class sl;

template <class K, class V, int MAX_LEVEL>
class sl_node {
public:
    sl_node() { }
    virtual ~sl_node() { }

private:
    K m_key;
    V m_val;
    uint8_t m_level;
    sl_node<K, V, MAX_LEVEL> **m_forward;

    friend class sl<K, V, MAX_LEVEL>;
};

template <class K, class V, int MAX_LEVEL = 32>
class sl {
public:
    sl();
    virtual ~sl();

private:
    sl_node<K, V, MAX_LEVEL> *m_header;
    uint64_t m_size;
    uint8_t  m_level;

    void insert(const K &key, const V &val, uint8_t lvl);
    uint8_t random_level();
};

template <class K, class V, int MAX_LEVEL>
inline sl<K, V, MAX_LEVEL>::sl() : m_size(0), m_level(1)
{
    m_header = new sl_node<K, V, MAX_LEVEL>;

    m_header->m_level = MAX_LEVEL;
    m_header->m_forward = new sl_node<K, V, MAX_LEVEL>*[MAX_LEVEL];

    for (int i = 0; i < MAX_LEVEL; i++) {
        m_header->m_forward[i] = nullptr;
    }
}

template <class K, class V, int MAX_LEVEL>
inline sl<K, V, MAX_LEVEL>::~sl()
{
    auto p = m_header;
    while (p != nullptr) {
        auto p1 = p->m_forward[0];
        delete[] p->m_forward;
        delete p;
        p = p1;
    }
}

template <class K, class V, int MAX_LEVEL>
inline uint8_t sl<K, V, MAX_LEVEL>::random_level()
{
    uint64_t max_level;
    uint64_t lvl = 1;

    asm (
        "lzcntq %0, %1;"
        : "=r" (max_level)
        : "r" (m_size)
        );

    max_level = 64 - max_level + 1;
    max_level = max_level > MAX_LEVEL ? MAX_LEVEL : max_level;

    while ((rand() / (RAND_MAX + 1.)) < 0.5 && lvl < max_level)
        lvl++;

    return lvl;
}

template <class K, class V, int MAX_LEVEL>
inline void sl<K, V, MAX_LEVEL>::insert(const K &key, const V &val, uint8_t lvl)
{

}

#endif // SL_HPP
