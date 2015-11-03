#ifndef CB_HPP
#define CB_HPP

// single writer and single reader
template <class T, int MAXLEN>
class cb {
public:
    cb() : m_len(0),
           m_buf(new T[MAXLEN]),
           m_buf_end(m_buf + MAXLEN),
           m_head(m_buf),
           m_tail(m_buf) { }
    virtual ~cb() { delete[] m_buf; }

    T pop();
    void push(const T &val);
    int get_len() { return m_len; }

private:
    volatile int m_len;
    T *m_buf;
    T *m_buf_end;

    T *m_head;
    T *m_tail;

};

template <class T, int MAXLEN>
inline T cb<T, MAXLEN>::pop()
{
    while (m_len == 0);

    T retval = *m_head;
    __sync_fetch_and_sub(&m_len, 1);

    if (m_head == m_buf_end) {
        m_head = m_buf;
    } else {
        m_head++;
    }

    return retval;
}

template <class T, int MAXLEN>
inline void cb<T, MAXLEN>::push(const T &val)
{
    while (m_len == MAXLEN);

    *m_tail = val;
    __sync_fetch_and_add(&m_len, 1);

    if (m_tail == m_buf_end) {
        m_tail = m_buf;
    } else {
        m_tail++;
    }
}

#endif // CB_HPP
