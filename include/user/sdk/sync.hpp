#pragma once

/*
 * Userspace sync + locks — CPU-level atomics (LOCK/XCHG + PAUSE).
 * No __sync_* as the locking source of truth (playbook).
 */

#include <kernel/types.h>
#include <kernel/syscall.h>
#include <user/sdk/syscall.hpp>
#include <user/sdk/thread.hpp>

namespace hsrc::sdk {
namespace detail {

inline int cpu_cas32(volatile uint32_t *ptr, uint32_t expected, uint32_t desired)
{
    uint32_t out;
    __asm__ volatile("lock; cmpxchgl %2, %1"
                     : "=a"(out), "+m"(*ptr)
                     : "r"(desired), "a"(expected)
                     : "memory", "cc");
    return out == expected;
}

inline uint32_t cpu_xchg32(volatile uint32_t *ptr, uint32_t val)
{
    __asm__ volatile("xchgl %0, %1"
                     : "+r"(val), "+m"(*ptr)
                     :
                     : "memory");
    return val;
}

inline void cpu_store_release32(volatile uint32_t *ptr, uint32_t val)
{
    __asm__ volatile("" ::: "memory");
    *ptr = val;
}

inline void cpu_pause(void)
{
    __asm__ volatile("pause" ::: "memory");
}

} /* namespace detail */

class Event {
public:
    Event()
    {
        long id = syscall0(SYS_EVENT_CREATE);
        id_ = (id >= 0) ? (int)id : -1;
    }

    ~Event() { close(); }

    Event(const Event &) = delete;
    Event &operator=(const Event &) = delete;

    Event(Event &&o) noexcept : id_(o.id_) { o.id_ = -1; }
    Event &operator=(Event &&o) noexcept
    {
        if (this != &o) {
            close();
            id_ = o.id_;
            o.id_ = -1;
        }
        return *this;
    }

    bool ok() const { return id_ >= 0; }
    int  id() const { return id_; }

    bool wait(uint32_t timeout_ticks = kWaitForever)
    {
        if (id_ < 0)
            return false;
        long to = (timeout_ticks == kWaitForever) ? (long)-1 : (long)timeout_ticks;
        return syscall2(SYS_EVENT_WAIT, id_, to) == 0;
    }

    bool try_wait()
    {
        if (id_ < 0)
            return false;
        return syscall2(SYS_EVENT_WAIT, id_, 0) == 0;
    }

    void signal()
    {
        if (id_ >= 0)
            (void)syscall1(SYS_EVENT_SIGNAL, id_);
    }

    void broadcast()
    {
        if (id_ >= 0)
            (void)syscall1(SYS_EVENT_BROADCAST, id_);
    }

    void close()
    {
        if (id_ >= 0) {
            (void)syscall1(SYS_EVENT_DESTROY, id_);
            id_ = -1;
        }
    }

private:
    int id_ = -1;
};

class Mutex {
public:
    Mutex() = default;
    Mutex(const Mutex &) = delete;
    Mutex &operator=(const Mutex &) = delete;

    void lock()
    {
        for (;;) {
            if (detail::cpu_cas32(&locked_, 0, 1))
                return;
            (void)gate_.wait(kWaitForever);
        }
    }

    bool try_lock() { return detail::cpu_cas32(&locked_, 0, 1) != 0; }

    void unlock()
    {
        detail::cpu_store_release32(&locked_, 0);
        gate_.signal();
    }

private:
    volatile uint32_t locked_ = 0;
    Event gate_{};
};

class LockGuard {
public:
    explicit LockGuard(Mutex &m) : m_(m) { m_.lock(); }
    ~LockGuard() { m_.unlock(); }
    LockGuard(const LockGuard &) = delete;
    LockGuard &operator=(const LockGuard &) = delete;

private:
    Mutex &m_;
};

class ConditionVariable {
public:
    ConditionVariable() = default;
    ConditionVariable(const ConditionVariable &) = delete;
    ConditionVariable &operator=(const ConditionVariable &) = delete;

    void wait(Mutex &m)
    {
        m.unlock();
        (void)ev_.wait(kWaitForever);
        m.lock();
    }

    bool wait_for(Mutex &m, uint32_t timeout_ticks)
    {
        m.unlock();
        bool ok = ev_.wait(timeout_ticks);
        m.lock();
        return ok;
    }

    void notify_one() { ev_.signal(); }
    void notify_all() { ev_.broadcast(); }

private:
    Event ev_{};
};

namespace lock {

using Mutex = hsrc::sdk::Mutex;
using LockGuard = hsrc::sdk::LockGuard;
using Event = hsrc::sdk::Event;
using ConditionVariable = hsrc::sdk::ConditionVariable;

class SpinLock {
public:
    SpinLock() = default;
    SpinLock(const SpinLock &) = delete;
    SpinLock &operator=(const SpinLock &) = delete;

    void lock()
    {
        for (;;) {
            if (detail::cpu_xchg32(&locked_, 1) == 0)
                return;
            while (locked_ != 0)
                detail::cpu_pause();
        }
    }

    bool try_lock() { return detail::cpu_xchg32(&locked_, 1) == 0; }

    void unlock() { detail::cpu_store_release32(&locked_, 0); }

private:
    volatile uint32_t locked_ = 0;
};

class RecursiveMutex {
public:
    RecursiveMutex() = default;
    RecursiveMutex(const RecursiveMutex &) = delete;
    RecursiveMutex &operator=(const RecursiveMutex &) = delete;

    void lock()
    {
        tid_t self = this_thread::get_id();
        if (owner_ == self) {
            depth_++;
            return;
        }
        for (;;) {
            if (detail::cpu_cas32(&locked_, 0, 1)) {
                owner_ = self;
                depth_ = 1;
                return;
            }
            (void)gate_.wait(kWaitForever);
        }
    }

    bool try_lock()
    {
        tid_t self = this_thread::get_id();
        if (owner_ == self) {
            depth_++;
            return true;
        }
        if (!detail::cpu_cas32(&locked_, 0, 1))
            return false;
        owner_ = self;
        depth_ = 1;
        return true;
    }

    void unlock()
    {
        tid_t self = this_thread::get_id();
        if (owner_ != self || depth_ <= 0)
            return;
        if (--depth_ > 0)
            return;
        owner_ = 0;
        detail::cpu_store_release32(&locked_, 0);
        gate_.signal();
    }

private:
    volatile uint32_t locked_ = 0;
    tid_t owner_ = 0;
    int depth_ = 0;
    Event gate_{};
};

} /* namespace lock */
} /* namespace hsrc::sdk */
