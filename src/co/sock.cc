#ifndef _WIN32

#include "co/co/sock.h"
#include "co/co/scheduler.h"
#include "co/co/io_event.h"

namespace co {

#ifdef SOCK_NONBLOCK
sock_t socket(int domain, int type, int protocol) {
    return ::socket(domain, type | SOCK_NONBLOCK | SOCK_CLOEXEC, protocol);
}

#else
sock_t socket(int domain, int type, int protocol) {
    sock_t fd = ::socket(domain, type, protocol);
    if (fd != -1) {
        co::set_nonblock(fd);
        co::set_cloexec(fd);
    }
    return fd;
}
#endif

int close(sock_t fd, int ms) {
    CHECK(xx::scheduler()) << "must be called in coroutine..";
    xx::scheduler()->del_io_event(fd);
    if (ms > 0) xx::scheduler()->sleep(ms);
    int r;
    while ((r = raw_api(close)(fd)) != 0 && errno == EINTR);
    return r;
}

int shutdown(sock_t fd, char c) {
    CHECK(xx::scheduler()) << "must be called in coroutine..";
    if (c == 'r') {
        xx::scheduler()->del_io_event(fd, EV_read);
        return raw_api(shutdown)(fd, SHUT_RD);
    } else if (c == 'w') {
        xx::scheduler()->del_io_event(fd, EV_write);
        return raw_api(shutdown)(fd, SHUT_WR);
    } else {
        xx::scheduler()->del_io_event(fd);
        return raw_api(shutdown)(fd, SHUT_RDWR);
    }
}

int bind(sock_t fd, const void* addr, int addrlen) {
    return ::bind(fd, (const struct sockaddr*)addr, (socklen_t)addrlen);
}

int listen(sock_t fd, int backlog) {
    return ::listen(fd, backlog);
}

sock_t accept(sock_t fd, void* addr, int* addrlen) {
    CHECK(xx::scheduler()) << "must be called in coroutine..";
    IoEvent ev(fd, EV_read);

    do {
      #ifdef SOCK_NONBLOCK
        sock_t connfd = raw_api(accept4)(fd, (sockaddr*)addr, (socklen_t*)addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (connfd != -1) return connfd;
      #else
        sock_t connfd = raw_api(accept)(fd, (sockaddr*)addr, (socklen_t*)addrlen);
        if (connfd != -1) {
            co::set_nonblock(connfd);
            co::set_cloexec(connfd);
            return connfd;
        }
      #endif

        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            ev.wait();
        } else if (errno != EINTR) {
            return -1;
        }
    } while (true);
}

int connect(sock_t fd, const void* addr, int addrlen, int ms) {
    CHECK(xx::scheduler()) << "must be called in coroutine..";
    do {
        int r = raw_api(connect)(fd, (const sockaddr*)addr, (socklen_t)addrlen);
        if (r == 0) return 0;

        if (errno == EINPROGRESS) {
            IoEvent ev(fd, EV_write);
            if (!ev.wait(ms)) return -1;

            int err, len = sizeof(err);
            r = co::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            if (r != 0) return -1;
            if (err == 0) return 0;
            errno = err;
            return -1;

        } else if (errno != EINTR) {
            return -1;
        }
    } while (true);
}

int recv(sock_t fd, void* buf, int n, int ms) {
    CHECK(xx::scheduler()) << "must be called in coroutine..";
    IoEvent ev(fd, EV_read);

    do {
        int r = (int) raw_api(recv)(fd, buf, n, 0);
        if (r != -1) return r;

        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            if (!ev.wait(ms)) return -1;
        } else if (errno != EINTR) {
            return -1;
        }
    } while (true);
}

int recvn(sock_t fd, void* buf, int n, int ms) {
    char* s = (char*) buf;
    int remain = n;
    IoEvent ev(fd, EV_read);

    do {
        int r = (int) raw_api(recv)(fd, s, remain, 0);
        if (r == remain) return n;
        if (r == 0) return 0;

        if (r == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                if (!ev.wait(ms)) return -1;
            } else if (errno != EINTR) {
                return -1;
            }
        } else {
            remain -= r;
            s += r;
        }
    } while (true);
}

int recvfrom(sock_t fd, void* buf, int n, void* addr, int* addrlen, int ms) {
    CHECK(xx::scheduler()) << "must be called in coroutine..";
    IoEvent ev(fd, EV_read);
    do {
        int r = (int) raw_api(recvfrom)(fd, buf, n, 0, (sockaddr*)addr, (socklen_t*)addrlen);
        if (r != -1) return r;

        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            if (!ev.wait(ms)) return -1;
        } else if (errno != EINTR) {
            return -1;
        }
    } while (true);
}

int send(sock_t fd, const void* buf, int n, int ms) {
    CHECK(xx::scheduler()) << "must be called in coroutine..";
    const char* s = (const char*) buf;
    int remain = n;
    IoEvent ev(fd, EV_write);

    do {
        int r = (int) raw_api(send)(fd, s, remain, 0);
        if (r == remain) return n;

        if (r == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                if (!ev.wait(ms)) return -1;
            } else if (errno != EINTR) {
                return -1;
            }
        } else {
            remain -= r;
            s += r;
        }
    } while (true);
}

int sendto(sock_t fd, const void* buf, int n, const void* addr, int addrlen, int ms) {
    CHECK(xx::scheduler()) << "must be called in coroutine..";
    const char* s = (const char*) buf;
    int remain = n;
    IoEvent ev(fd, EV_write);

    do {
        int r = (int) raw_api(sendto)(fd, s, remain, 0, (const sockaddr*)addr, (socklen_t)addrlen);
        if (r == remain) return n;

        if (r == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                if (!ev.wait(ms)) return -1;
            } else if (errno != EINTR) {
                return -1;
            }
        } else {
            remain -= r;
            s += r;
        }
    } while (true);
}

namespace xx {

class Error {
  public:
    Error() = default;
    ~Error() = default;

    struct T {
        T() : err(4096) {}
        fastream err;
        std::unordered_map<int, uint32> pos;
    };

    const char* strerror(int e) {
        if (_p == NULL) _p.reset(new T);
        auto it = _p->pos.find(e);
        if (it != _p->pos.end()) {
            return _p->err.data() + it->second;
        } else {
            uint32 pos = (uint32) _p->err.size();
            static ::Mutex mtx;
            {
                ::MutexGuard g(mtx);
                _p->err.append(::strerror(e)).append('\0');
            }
            _p->pos[e] = pos;
            return _p->err.data() + pos;
        }
    }

  private:
    thread_ptr<T> _p;
};

} // xx

const char* strerror(int err) {
    if (err == ETIMEDOUT) return "Timed out";
    static xx::Error e;
    return e.strerror(err);
}

} // co

#endif
