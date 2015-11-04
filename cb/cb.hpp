#ifndef CB_HPP
#define CB_HPP

// single writer and single reader
template <class T>
class cb {
public:
    cb(int len) : m_max_len(len),
                  m_len(0),
                  m_buf(new T[len]),
                  m_buf_end(m_buf + len),
                  m_head(m_buf),
                  m_tail(m_buf) { }
    virtual ~cb() { }

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

};

template <class T>
inline T cb<T>::pop()
{
    while (m_len == 0);

    T retval = *m_head;
    __sync_fetch_and_sub(&m_len, 1);

    m_head++;

    if (m_head == m_buf_end) {
        m_head = m_buf;
    }

    return retval;
}

template <class T>
inline void cb<T>::push(const T &val)
{
    while (m_len == m_max_len);

    *m_tail = val;
    __sync_fetch_and_add(&m_len, 1);

    m_tail++;

    if (m_tail == m_buf_end) {
        m_tail = m_buf;
    }
}

#endif // CB_HPP
