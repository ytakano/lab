#ifndef RTM_LOCK_HPP
#define RTM_LOCK_HPP

#include "rtm.h"

#include <assert.h>

#define RTM_MAX_RETRY 6

#ifdef DEBUG_RTM
    #include <iostream>
#endif // DEBUG_RTM

class rtm_transaction;

class rtm_lock {
public:
#ifdef DEBUG_RTM
    rtm_lock() : m_lock(0), m_nlock(0), m_nrtm(0) { }
#else
    rtm_lock() : m_lock(0) {}
#endif // DEBUG_RTM
    
    ~rtm_lock() { }

private:
    volatile int m_lock;
#ifdef DEBUG_RTM
    volatile int m_nlock;
    volatile int m_nrtm;
#endif // DEBUG_RTM

    friend class rtm_transaction;
};

class rtm_transaction {
public:
    rtm_transaction(rtm_lock &lock) : m_rtm_lock(lock)
    {
        unsigned status;
        int i;
        for (i = 0; i < RTM_MAX_RETRY; i++) {
            status = _xbegin();
            if (status == _XBEGIN_STARTED) {
                if (! lock.m_lock) {
                    return;
                }
                _xabort(0xff);
            }

            if ((status & _XABORT_EXPLICIT) &&
                _XABORT_CODE(status) == 0xff) {
                
                assert(!(status & _XABORT_NESTED));

                while (lock.m_lock) ; // busy-wait
            } else if (!(status & _XABORT_RETRY)) {
                break;
            }
        }

        while (__sync_lock_test_and_set(&lock.m_lock, 1)) {
            while (lock.m_lock) ;
            // busy-wait
        }
    }

    ~rtm_transaction()
    {
        if (m_rtm_lock.m_lock) {
#ifdef DEBUG_RTM
            m_rtm_lock.m_nlock++;
#endif // DEBUG_RTM
            __sync_lock_release(&m_rtm_lock.m_lock);
        } else {
#ifdef DEBUG_RTM
            m_rtm_lock.m_nrtm++;
#endif // DEBUG_RTM
            _xend();
        }

#ifdef DEBUG_RTM
        if (((m_rtm_lock.m_nlock + m_rtm_lock.m_nrtm) % 10000000) == 0) {
            std::cout << "m_nlock = " << m_rtm_lock.m_nlock
                      << ", m_nrtm = " << m_rtm_lock.m_nrtm
                      << std::endl;
        }
#endif // DEBUG_RTM
    }

private:
    rtm_lock &m_rtm_lock;
};

#endif // RTM_LOCK_HPP
