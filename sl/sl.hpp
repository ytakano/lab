#ifndef SL_HPP
#define SL_HPP

#include <stdlib.h>

class xorshift {
public:
    xorshift() : x(123456789), y(362436069), z(521288629), w(88675123) { }

    void init_xor128(uint32_t s) {
        x = s = 1812433253U * (s ^ (s >> 30));
        y = s = 1812433253U * (s ^ (s >> 30)) + 1;
        z = s = 1812433253U * (s ^ (s >> 30)) + 2;
        w = s = 1812433253U * (s ^ (s >> 30)) + 3;
    }

    uint32_t xor128() {
        uint32_t t = x ^ (x << 11);
        x = y; y = z; z = w;
        return (w = (w ^ (w >> 19)) ^ (t ^ (t >> 8)));
    }

private:
    uint32_t x, y, z, w;
};

template <typename K, typename V, int MAX_LEVEL> class sl;

template <typename K, typename V, int MAX_LEVEL>
class sl_node {
public:
    sl_node() { }
    virtual ~sl_node() { delete[] m_forward; }

private:
    uint8_t m_level;
    K m_key;
    V m_val;
    sl_node **m_forward;

    friend class sl<K, V, MAX_LEVEL>;
};

template <typename K, typename V, int MAX_LEVEL = 32>
class sl {
public:
    sl();
    virtual ~sl();

    void insert(const K &key, const V &val);
    void erase(const K &key);
    const V* find(const K &key);

private:
    sl_node<K, V, MAX_LEVEL> *m_header;
    uint64_t m_size;
    uint8_t  m_level;

    xorshift m_xs;

    uint8_t random_level();
};

template <typename K, typename V, int MAX_LEVEL>
inline sl<K, V, MAX_LEVEL>::sl() : m_size(0), m_level(1)
{
    m_header = new sl_node<K, V, MAX_LEVEL>;

    m_header->m_level = MAX_LEVEL;
    m_header->m_forward = new sl_node<K, V, MAX_LEVEL>*[MAX_LEVEL];

    for (int i = 0; i < MAX_LEVEL; i++) {
        m_header->m_forward[i] = nullptr;
    }
}

template <typename K, typename V, int MAX_LEVEL>
inline sl<K, V, MAX_LEVEL>::~sl()
{
    auto p = m_header;
    while (p != nullptr) {
        auto p1 = p->m_forward[0];
        delete p;
        p = p1;
    }
}

template <typename K, typename V, int MAX_LEVEL>
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

    while (lvl < max_level && m_xs.xor128() < (UINT32_MAX / 2))
        lvl++;

    return lvl;
}

template <typename K, typename V, int MAX_LEVEL>
inline void sl<K, V, MAX_LEVEL>::insert(const K &key, const V &val)
{
    sl_node<K, V, MAX_LEVEL> *update[MAX_LEVEL];
    sl_node<K, V, MAX_LEVEL> *x = m_header;
    auto new_node = new sl_node<K, V, MAX_LEVEL>();

    new_node->m_level = random_level();
    new_node->m_forward = new sl_node<K, V, MAX_LEVEL>*[new_node->m_level];

    for (int i = m_level - 1; i >= 0; i--) {
        while (x->m_forward[i] != nullptr && x->m_forward[i]->m_key < key)
            x = x->m_forward[i];

        update[i] = x;
    }

    if (x->m_key == key) {
        x->m_val = val;
        delete new_node;
    } else {
        if (new_node->m_level > m_level) {
            for (int i = m_level; i < new_node->m_level; i++) {
                update[i] = m_header;
            }

            m_level = new_node->m_level;
        }

        new_node->m_key = key;
        new_node->m_val = val;

        for (int i = 0; i < new_node->m_level; i++) {
            new_node->m_forward[i]  = update[i]->m_forward[i];
            update[i]->m_forward[i] = new_node;
        }

        m_size++;
    }
}

template <typename K, typename V, int MAX_LEVEL>
inline void sl<K, V, MAX_LEVEL>::erase(const K &key)
{
    sl_node<K, V, MAX_LEVEL> *update[MAX_LEVEL];
    sl_node<K, V, MAX_LEVEL> *x = m_header, *p = nullptr;

    for (int i = m_level - 1; i >= 0; i--) {
        while (x->m_forward[i] != nullptr && x->m_forward[i]->m_key < key)
            x = x->m_forward[i];

        update[i] = x;
    }

    x = x->m_forward[0];

    if (x != nullptr && x->m_key == key) {
        for (int i = 0; i < m_level; i++) {
            if (update[i]->m_forward[i] != x)
                break;

            update[i]->m_forward[i] = x->m_forward[i];
        }

        p = x;

        int i = m_level - 1;
        while(i >= 0 && m_header->m_forward[i] == nullptr) {
            i--;
        }

        m_level = i + 1;
    }

    delete p;
}

template <typename K, typename V, int MAX_LEVEL>
inline const V* sl<K, V, MAX_LEVEL>::find(const K &key)
{
    auto x = m_header;

    for (int i = m_level - 1; i >= 0; i--) {
        while (x->m_forward[i] != nullptr && x->m_forward[i]->m_key < key)
            x = x->m_forward[i];
    }

    x = x->m_forward[0];

    if (x != nullptr && x->m_key == key) {
        return &x->m_val;
    }

    return nullptr;
}

#endif // SL_HPP
