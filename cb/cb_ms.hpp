#ifndef CB_MS_HPP
#define CB_MS_HPP

#include "rtm_lock.hpp"
#include "tsx-cpuid.h"

#include <iostream>

// multiple writers and single reader
template <typename T>
class cb_ms {
public:
    cb_ms(int len) : m_max_len(len),
                     m_len(0),
                     m_buf(new T[len]),
                     m_buf_end(m_buf + len),
                     m_head(m_buf),
                     m_tail(m_buf) { }
    virtual ~cb_ms() { delete[] m_buf; }

    T    pop();
    void push(const T &val);
    int  get_len() { return m_len; }

private:
    int m_max_len;
    volatile int m_len;
    T *m_buf;
    T *m_buf_end;

    T *m_head;
    T *m_tail;

    rtm_lock m_rtm_lock;
};

template <typename T>
inline T cb_ms<T>::pop()
{
    while (m_len == 0);

    T retval = *m_head;

    {
        rtm_transaction transaction(m_rtm_lock);
        m_len--;
    }

    m_head++;

    if (m_head == m_buf_end) {
        m_head = m_buf;
    }

    return retval;
}

template <typename T>
inline void cb_ms<T>::push(const T &val)
{
    while (m_len == m_max_len);

    rtm_transaction transaction(m_rtm_lock);

    *m_tail = val;
    m_len++;
    m_tail++;

    if (m_tail == m_buf_end) {
        m_tail = m_buf;
    }
}

#endif // CB_MS_HPP
