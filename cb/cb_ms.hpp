#ifndef CB_MS_HPP
#define CB_MS_HPP

#include "rtm_lock.hpp"
#include "tsx-cpuid.h"

#include <iostream>

// multiple writers and single reader
template <class T>
class cb_ms {
public:
    cb_ms(int len) : m_max_len(len),
                     m_lock(0),
                     m_len(0),
                     m_buf(new T[len]),
                     m_buf_end(m_buf + len),
                     m_head(m_buf),
                     m_tail(m_buf),
                     m_is_rtm(cpu_has_rtm()) { }
    virtual ~cb_ms() { delete[] m_buf; }

    T    pop();
    void push(const T &val);
    int  get_len() { return m_len; }

private:
    int m_max_len;
    volatile int m_lock;
    volatile int m_len;
    T *m_buf;
    T *m_buf_end;

    T *m_head;
    volatile T *m_tail;

    bool m_is_rtm;

    rtm_lock m_rtm_lock;

    T    pop_lock();
    void push_lock(const T &val);
    T    pop_rtm();
    void push_rtm(const T &val);

};

template <class T>
inline T cb_ms<T>::pop()
{
    if (m_is_rtm)
        return pop_rtm();

    return pop_lock();
}

template <class T>
inline void cb_ms<T>::push(const T &val)
{
    if (m_is_rtm)
        push_rtm(val);
    else
        push_lock(val);
}

template <class T>
inline T cb_ms<T>::pop_rtm()
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

template <class T>
inline void cb_ms<T>::push_rtm(const T &val)
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

template <class T>
inline T cb_ms<T>::pop_lock()
{
    while (m_len == 0);

    T retval = *m_head;

    while (__sync_lock_test_and_set(&m_lock, 1)) {
        while (m_lock) ;
        // busy-wait
    }

    m_len--;

    __sync_lock_release(&m_lock);

    m_head++;

    if (m_head == m_buf_end) {
        m_head = m_buf;
    }

    return retval;
}

template <class T>
inline void cb_ms<T>::push_lock(const T &val)
{
    while (m_len == m_max_len);

    while (__sync_lock_test_and_set(&m_lock, 1)) {
        while (m_lock) ;
        // busy-wait
    }

    *m_tail = val;
    m_len++;
    m_tail++;

    if (m_tail == m_buf_end) {
        m_tail = m_buf;
    }

    __sync_lock_release(&m_lock);
}

#endif // CB_MS_HPP
