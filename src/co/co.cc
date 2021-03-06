#include "co/co/scheduler.h"
#include "co/co/event.h"
#include "co/co/mutex.h"
#include "co/co/pool.h"
#include <deque>
#include <unordered_set>

namespace co {
namespace xx {

class EventImpl {
  public:
    EventImpl() : _signaled(false) {}
    ~EventImpl() = default;

    void wait();

    bool wait(unsigned int ms);

    void signal();

  private:
    ::Mutex _mtx;
    std::unordered_set<Coroutine*> _co_wait;
    std::unordered_set<Coroutine*> _co_swap;
    bool _signaled;
};

void EventImpl::wait() {
    Scheduler* s = scheduler();
    CHECK(s) << "must be called in coroutine..";
    Coroutine* co = s->running();
    if (co->s != s) co->s = s;

    {
        ::MutexGuard g(_mtx);
        if (_signaled) { _signaled = false; return; }
        co->state = S_wait;
        _co_wait.insert(co);
    }

    s->yield();
    co->state = S_init;
}

bool EventImpl::wait(unsigned int ms) {
    Scheduler* s = scheduler();
    CHECK(s) << "must be called in coroutine..";
    Coroutine* co = s->running();
    if (co->s != s) co->s = s;

    {
        ::MutexGuard g(_mtx);
        if (_signaled) { _signaled = false; return true; }
        co->state = S_wait;
        _co_wait.insert(co);
    }

    s->add_timer(ms);
    s->yield();
    if (s->timeout()) {
        ::MutexGuard g(_mtx);
        _co_wait.erase(co);
    }

    co->state = S_init;
    return !s->timeout();
}

void EventImpl::signal() {
    {
        ::MutexGuard g(_mtx);
        if (!_co_wait.empty()) {
            _co_wait.swap(_co_swap);
        } else {
            if (!_signaled) _signaled = true;
            return;
        }
    }

    // Using atomic operation here, as check_timeout() in the Scheduler 
    // may also modify the state.
    for (auto it = _co_swap.begin(); it != _co_swap.end(); ++it) {
        Coroutine* co = *it;
        if (atomic_compare_swap(&co->state, S_wait, S_ready) == S_wait) {
            co->s->add_ready_task(co);
        }
    }
    _co_swap.clear();
}

class MutexImpl {
  public:
    MutexImpl() : _lock(false) {}
    ~MutexImpl() = default;

    void lock();

    void unlock();

    bool try_lock();

  private:
    ::Mutex _mtx;
    std::deque<Coroutine*> _co_wait;
    bool _lock;
};

inline bool MutexImpl::try_lock() {
    ::MutexGuard g(_mtx);
    return _lock ? false : (_lock = true);
}

inline void MutexImpl::lock() {
    Scheduler* s = scheduler();
    CHECK(s) << "must be called in coroutine..";
    _mtx.lock();
    if (!_lock) {
        _lock = true;
        _mtx.unlock();
    } else {
        Coroutine* co = s->running();
        if (co->s != s) co->s = s;
        _co_wait.push_back(co);
        _mtx.unlock();
        s->yield();
    }
}

inline void MutexImpl::unlock() {
    _mtx.lock();
    if (_co_wait.empty()) {
        _lock = false;
        _mtx.unlock();
    } else {
        Coroutine* co = _co_wait.front();
        _co_wait.pop_front();
        _mtx.unlock();
        co->s->add_ready_task(co);
    }
}

class PoolImpl {
  public:
    typedef std::vector<void*> V;

    PoolImpl()
        : _pools(scheduler_num()), _maxcap((size_t)-1) {
    }

    // @ccb:  a create callback       []() { return (void*) new T; }
    // @dcb:  a destroy callback      [](void* p) { delete (T*)p; }
    // @cap:  max capacity for each pool
    PoolImpl(std::function<void*()>&& ccb, std::function<void(void*)>&& dcb, size_t cap)
        : _pools(scheduler_num()), _maxcap(cap) {
        _ccb = std::move(ccb);
        _dcb = std::move(dcb);
    }

    ~PoolImpl() = default;

    void* pop() {
        Scheduler* s = scheduler();
        CHECK(s) << "must be called in coroutine..";
        auto& v = _pools[s->id()];
        if (v == NULL) v = this->create_pool();

        if (!v->empty()) {
            void* p = v->back();
            v->pop_back();
            return p;
        } else {
            return _ccb ? _ccb() : 0;
        }
    }

    void push(void* p) {
        if (!p) return; // ignore null pointer

        Scheduler* s = scheduler();
        CHECK(s) << "must be called in coroutine..";
        auto& v = _pools[s->id()];
        if (v == NULL) v = this->create_pool();

        if (!_dcb || v->size() < _maxcap) {
            v->push_back(p);
        } else {
            _dcb(p);
        }
    }

    size_t size() const {
        Scheduler* s = scheduler();
        CHECK(s) << "must be called in coroutine..";
        auto& v = _pools[s->id()];
        return v != NULL ? v->size() : 0;
    }

  private:
    // It is not safe to cleanup the pool from outside the Scheduler.
    // So we add a cleanup callback to the Scheduler. It will be called 
    // at the end of Scheduler::loop().
    V* create_pool() {
        V* v = new V();
        v->reserve(1024);
        scheduler()->add_cleanup_cb(std::bind(&PoolImpl::cleanup, v, _dcb));
        return v;
    }

    static void cleanup(V* v, const std::function<void(void*)>& dcb) {
        if (dcb) {
            for (size_t i = 0; i < v->size(); ++i) dcb((*v)[i]);
        }
        delete v;
    }

  private:
    std::vector<V*> _pools;
    std::function<void*()> _ccb;
    std::function<void(void*)> _dcb;
    size_t _maxcap;
};

} // xx

Event::Event() {
    _p = new xx::EventImpl;
}

Event::~Event() {
    delete (xx::EventImpl*) _p;
}

void Event::wait() {
    ((xx::EventImpl*)_p)->wait();
}

bool Event::wait(unsigned int ms) {
    return ((xx::EventImpl*)_p)->wait(ms);
}

void Event::signal() {
    ((xx::EventImpl*)_p)->signal();
}


Mutex::Mutex() {
    _p = new xx::MutexImpl;
}

Mutex::~Mutex() {
    delete (xx::MutexImpl*) _p;
}

void Mutex::lock() {
    ((xx::MutexImpl*)_p)->lock();
}

void Mutex::unlock() {
    ((xx::MutexImpl*)_p)->unlock();
}

bool Mutex::try_lock() {
    return ((xx::MutexImpl*)_p)->try_lock();
}

Pool::Pool() {
    _p = new xx::PoolImpl;
}

Pool::~Pool() {
    delete (xx::PoolImpl*) _p;
}

Pool::Pool(std::function<void*()>&& ccb, std::function<void(void*)>&& dcb, size_t cap) {
    _p = new xx::PoolImpl(std::move(ccb), std::move(dcb), cap);
}

void* Pool::pop() {
    return ((xx::PoolImpl*)_p)->pop();
}

void Pool::push(void* p) {
    ((xx::PoolImpl*)_p)->push(p);
}

size_t Pool::size() const {
    return ((xx::PoolImpl*)_p)->size();
}

} // co
