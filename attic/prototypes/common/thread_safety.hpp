// Clang thread-safety annotations and an annotated mutex (libstdc++'s
// std::mutex carries none, so -Wthread-safety can't see it).
#pragma once

#include <atomic>
#include <mutex>
#include <thread>

#if defined(__clang__)
#define TS_ATTR(x) __attribute__((x))
#else
#define TS_ATTR(x)
#endif

#define TS_CAPABILITY(x) TS_ATTR(capability(x))
#define TS_SCOPED_CAPABILITY TS_ATTR(scoped_lockable)
#define TS_GUARDED_BY(x) TS_ATTR(guarded_by(x))
#define TS_REQUIRES(...) TS_ATTR(requires_capability(__VA_ARGS__))
#define TS_EXCLUDES(...) TS_ATTR(locks_excluded(__VA_ARGS__))
#define TS_ACQUIRE(...) TS_ATTR(acquire_capability(__VA_ARGS__))
#define TS_RELEASE(...) TS_ATTR(release_capability(__VA_ARGS__))

namespace proto {

class TS_CAPABILITY("mutex") Mutex {
public:
    void lock() TS_ACQUIRE() {
        m_.lock();
        owner_.store(std::this_thread::get_id(), std::memory_order_relaxed);
    }
    void unlock() TS_RELEASE() {
        owner_.store(std::thread::id{}, std::memory_order_relaxed);
        m_.unlock();
    }
    // For the debug assert that factories and turns run outside the lock.
    bool heldByThisThread() const {
        return owner_.load(std::memory_order_relaxed) == std::this_thread::get_id();
    }

private:
    std::mutex m_;
    std::atomic<std::thread::id> owner_{};
};

class TS_SCOPED_CAPABILITY Lock {
public:
    explicit Lock(Mutex& m) TS_ACQUIRE(m) : m_(m) { m_.lock(); }
    ~Lock() TS_RELEASE() { m_.unlock(); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

private:
    Mutex& m_;
};

}  // namespace proto
