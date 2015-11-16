#ifndef CB_MS_SPIN_HPP
#define CB_MS_SPIN_HPP

#include "spin_lock.hpp"

#include <iostream>

// multiple writers and single reader
template <typename T>
class cb_ms_spin {
public:
    cb_ms_spin(int len) : m_max_len(len),
                          m_len(0),
                          m_buf(new T[len]),
                          m_buf_end(m_buf + len),
                          m_head(m_buf),
                          m_tail(m_buf) { }
    virtual ~cb_ms_spin() { delete[] m_buf; }

    T    pop();
    void push(const T &val);
    int  get_len() { return m_len; }

private:
    int m_max_len;
    volatile int m_len;
    T *m_buf;
    T *m_buf_end;

    T *m_head;
    volatile T *m_tail;

    spin_lock m_lock;
};

template <typename T>
inline T cb_ms_spin<T>::pop()
{
    while (m_len == 0);

    T retval = *m_head;

    {
        spin_lock_ac lock(m_lock);
        m_len--;
    }

    m_head++;

    if (m_head == m_buf_end) {
        m_head = m_buf;
    }

    return retval;
}

template <typename T>
inline void cb_ms_spin<T>::push(const T &val)
{
    while (m_len == m_max_len);

    spin_lock_ac lock(m_lock);

    *m_tail = val;
    m_len++;
    m_tail++;

    if (m_tail == m_buf_end) {
        m_tail = m_buf;
    }
}

#endif // CB_MS_SPIN_HPP
