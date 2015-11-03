#ifndef CB_MS_HPP
#define CB_MS_HPP

#include "rtm.h"
#include "tsx-cpuid.h"

#include <iostream>

// multiple writers and single reader
template <class T, int MAXLEN>
class cb_ms {
public:
    cb_ms() : m_lock(0),
              m_len(0),
              m_buf_end(m_buf + MAXLEN),
              m_head(m_buf),
              m_tail(m_buf),
//              m_is_rtm(false) { }
              m_is_rtm(cpu_has_rtm()) { }
    virtual ~cb_ms() { }

    T    pop();
    void push(const T &val);
    int  get_len() { return m_len; }

private:
    volatile int m_lock;
    int m_len;
    T   m_buf[MAXLEN];
    T  *m_buf_end;

    T *m_head;
    T *m_tail;

    bool m_is_rtm;

    T    pop_lock();
    void push_lock(const T &val);
    T    pop_rtm();
    void push_rtm(const T &val);

};

template <class T, int MAXLEN>
inline T cb_ms<T, MAXLEN>::pop()
{
    if (m_is_rtm)
        return pop_rtm();

    return pop_lock();
}

template <class T, int MAXLEN>
inline void cb_ms<T, MAXLEN>::push(const T &val)
{
    if (m_is_rtm)
        push_rtm(val);
    else
        push_lock(val);
}

template <class T, int MAXLEN>
inline T cb_ms<T, MAXLEN>::pop_rtm()
{
    asm volatile (
        "RETRY_POP_RTM:"
        "xbegin ABORT_POP_RTM;"
        );

    while (m_len == 0);

    T retval = *m_head;

    m_len--;

    asm volatile (
        "xend;"
        "xorl %eax, %eax;"
        "ABORT_POP_RTM:"
        "cmpl $0, %eax;"
        "jne RETRY_POP_RTM;"
        );

    m_head++;

    if (m_head == m_buf_end) {
        m_head = m_buf;
    }

    return retval;
}

template <class T, int MAXLEN>
inline void cb_ms<T, MAXLEN>::push_rtm(const T &val)
{
    asm volatile (
        "RETRY_PUSH_RTM:"
        "xbegin ABORT_PUSH_RTM;"
        );

    while (m_len == MAXLEN);

    *m_tail = val;
    m_len++;
    m_tail++;

    if (m_tail == m_buf_end) {
        m_tail = m_buf;
    }

    asm volatile (
        "xend;"
        "xorl %eax, %eax;"
        "ABORT_PUSH_RTM:"
        "cmpl $0, %eax;"
        "jne RETRY_PUSH_RTM;"
        );

    std::cout << "pushed!: m_len = " << m_len << ", val = " << val
              << ", m_tail = " << (void*)m_tail << std::endl;

    return;
}

template <class T, int MAXLEN>
inline T cb_ms<T, MAXLEN>::pop_lock()
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

template <class T, int MAXLEN>
inline void cb_ms<T, MAXLEN>::push_lock(const T &val)
{
    while (m_len == MAXLEN);

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
