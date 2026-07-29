#pragma once

/*
 * Userspace sync + locks (single header).
 *
 *   Event / ConditionVariable  — wait for a signal
 *   Mutex / LockGuard          — blocking mutual exclusion
 *   lock::SpinLock             — busy-wait (tiny CS only)
 *   lock::RecursiveMutex       — same-thread reentrant mutex
 *
 * Kernel side: <kernel/klock.h> (spin/klock), <kernel/sync.h> (kevent/suspend).
 */

#include <kernel/types.h>
#include <kernel/syscall.h>
#include <user/sdk/syscall.hpp>
#include <user/sdk/thread.hpp>

namespace hsrc::sdk {

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
            if (__sync_bool_compare_and_swap(&locked_, 0, 1))
                return;
            (void)gate_.wait(kWaitForever);
        }
    }

    bool try_lock()
    {
        return __sync_bool_compare_and_swap(&locked_, 0, 1);
    }

    void unlock()
    {
        __sync_lock_release(&locked_);
        gate_.signal();
    }

private:
    volatile int locked_ = 0;
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
            if (__sync_bool_compare_and_swap(&locked_, 0, 1))
                return;
            __asm__ volatile("pause" ::: "memory");
        }
    }

    bool try_lock()
    {
        return __sync_bool_compare_and_swap(&locked_, 0, 1);
    }

    void unlock() { __sync_lock_release(&locked_); }

private:
    volatile int locked_ = 0;
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
            if (__sync_bool_compare_and_swap(&locked_, 0, 1)) {
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
        if (!__sync_bool_compare_and_swap(&locked_, 0, 1))
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
        __sync_lock_release(&locked_);
        gate_.signal();
    }

private:
    volatile int locked_ = 0;
    volatile int depth_ = 0;
    volatile tid_t owner_ = 0;
    Event gate_{};
};

template <typename L>
class Guard {
public:
    explicit Guard(L &lock) : lock_(lock) { lock_.lock(); }
    ~Guard() { lock_.unlock(); }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;

private:
    L &lock_;
};

using SpinGuard = Guard<SpinLock>;
using RecursiveGuard = Guard<RecursiveMutex>;

} // namespace lock
} // namespace hsrc::sdk
