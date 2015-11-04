#ifndef SPIN_LOCK_HPP
#define SPIN_LOCK_HPP

class spin_lock_ac;

class spin_lock {
public:
    spin_lock() : m_lock(0) { }
    ~spin_lock() { }

private:
    volatile int m_lock;

    friend class spin_lock_ac;
};

class spin_lock_ac {
public:
    spin_lock_ac(spin_lock &lock) : m_spin_lock(lock)
    {
        while (__sync_lock_test_and_set(&lock.m_lock, 1)) {
            while (lock.m_lock) ;
            // busy-wait
        }
    }

    ~spin_lock_ac()
    {
        __sync_lock_release(&m_spin_lock.m_lock);
    }

private:
    spin_lock &m_spin_lock;
};

#endif // SPIN_LOCK_HPP
