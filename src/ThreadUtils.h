/**
 * Copyright (c) Jonathan Cardoso Machado. All Rights Reserved.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#ifndef NODELIBCURL_THREAD_UTILS_H
#define NODELIBCURL_THREAD_UTILS_H

#include <curl/curl.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Platform-specific includes
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#elif defined(__linux__)
#include <sys/epoll.h>
#include <sys/socket.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#else
// Fallback to poll for other POSIX systems
#include <sys/socket.h>

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#endif

namespace NodeLibcurl {

// Cross-platform error string helper
inline std::string GetErrorString(int errorCode) {
#ifdef _WIN32
  char buffer[256];
  strerror_s(buffer, sizeof(buffer), errorCode);
  return std::string(buffer);
#else
  return std::string(strerror(errorCode));
#endif
}

// Cross-platform socket error string helper
inline std::string GetSocketErrorString() {
#ifdef _WIN32
  int error = WSAGetLastError();
  char* msgBuffer = nullptr;
  FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&msgBuffer), 0, nullptr);
  std::string result = msgBuffer ? msgBuffer : "Unknown error";
  LocalFree(msgBuffer);
  return result;
#else
  return GetErrorString(errno);
#endif
}

// Cross-platform file read with offset support
inline ssize_t ReadFileWithOffset(int fd, void* buffer, size_t count, off_t offset) {
#ifdef _WIN32
  if (offset >= 0) {
    if (_lseeki64(fd, offset, SEEK_SET) == -1) {
      return -1;
    }
  }
  return _read(fd, buffer, static_cast<unsigned int>(count));
#else
  if (offset >= 0) {
    return pread(fd, buffer, count, offset);
  }
  return read(fd, buffer, count);
#endif
}

// Poll event constants
constexpr int POLL_EVENT_READABLE = 1;
constexpr int POLL_EVENT_WRITABLE = 2;

// Single socket poll result
struct PollResult {
  int status;  // < 0 on error, 0 on timeout, > 0 on events
  int events;  // POLL_EVENT_READABLE | POLL_EVENT_WRITABLE
};

// Multi-socket poll result
struct MultiPollResult {
  curl_socket_t sockfd;
  int events;
};

//=============================================================================
// Single Socket Poller - Platform-specific implementations
//=============================================================================

#ifdef _WIN32
// Windows: WSAPoll
class SocketPoller {
 public:
  SocketPoller() = default;
  ~SocketPoller() = default;

  bool Init(curl_socket_t socket, int events) {
    socket_ = socket;
    events_ = events;
    return true;
  }

  void Close() {}

  PollResult Poll(int timeout_ms) {
    PollResult result = {0, 0};
    WSAPOLLFD pfd;
    pfd.fd = socket_;
    pfd.events = 0;
    pfd.revents = 0;
    if (events_ & POLL_EVENT_READABLE) pfd.events |= POLLIN;
    if (events_ & POLL_EVENT_WRITABLE) pfd.events |= POLLOUT;

    result.status = WSAPoll(&pfd, 1, timeout_ms);
    if (result.status > 0) {
      if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) result.events |= POLL_EVENT_READABLE;
      if (pfd.revents & POLLOUT) result.events |= POLL_EVENT_WRITABLE;
    }
    return result;
  }

 private:
  curl_socket_t socket_ = INVALID_SOCKET;
  int events_ = 0;
};

#elif defined(__linux__)
// Linux: epoll
class SocketPoller {
 public:
  SocketPoller() = default;
  ~SocketPoller() { Close(); }

  bool Init(curl_socket_t socket, int events) {
    socket_ = socket;
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1) return false;

    struct epoll_event ev;
    ev.events = 0;
    if (events & POLL_EVENT_READABLE) ev.events |= EPOLLIN;
    if (events & POLL_EVENT_WRITABLE) ev.events |= EPOLLOUT;
    ev.data.fd = socket;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, socket, &ev) == -1) {
      close(epoll_fd_);
      epoll_fd_ = -1;
      return false;
    }
    return true;
  }

  void Close() {
    if (epoll_fd_ != -1) {
      close(epoll_fd_);
      epoll_fd_ = -1;
    }
  }

  PollResult Poll(int timeout_ms) {
    PollResult result = {0, 0};
    struct epoll_event ev;
    result.status = epoll_wait(epoll_fd_, &ev, 1, timeout_ms);
    if (result.status > 0) {
      if (ev.events & (EPOLLIN | EPOLLHUP | EPOLLERR)) result.events |= POLL_EVENT_READABLE;
      if (ev.events & EPOLLOUT) result.events |= POLL_EVENT_WRITABLE;
    }
    return result;
  }

 private:
  curl_socket_t socket_ = -1;
  int epoll_fd_ = -1;
};

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
// macOS/BSD: kqueue
class SocketPoller {
 public:
  SocketPoller() = default;
  ~SocketPoller() { Close(); }

  bool Init(curl_socket_t socket, int events) {
    socket_ = socket;
    events_ = events;
    kqueue_fd_ = kqueue();
    if (kqueue_fd_ == -1) return false;

    struct kevent changes[2];
    int nchanges = 0;
    if (events & POLL_EVENT_READABLE) {
      EV_SET(&changes[nchanges++], socket, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }
    if (events & POLL_EVENT_WRITABLE) {
      EV_SET(&changes[nchanges++], socket, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }

    if (nchanges > 0 && kevent(kqueue_fd_, changes, nchanges, nullptr, 0, nullptr) == -1) {
      close(kqueue_fd_);
      kqueue_fd_ = -1;
      return false;
    }
    return true;
  }

  void Close() {
    if (kqueue_fd_ != -1) {
      close(kqueue_fd_);
      kqueue_fd_ = -1;
    }
  }

  PollResult Poll(int timeout_ms) {
    PollResult result = {0, 0};
    struct kevent events[2];
    struct timespec ts;
    struct timespec* ts_ptr = nullptr;

    if (timeout_ms >= 0) {
      ts.tv_sec = timeout_ms / 1000;
      ts.tv_nsec = (timeout_ms % 1000) * 1000000;
      ts_ptr = &ts;
    }

    result.status = kevent(kqueue_fd_, nullptr, 0, events, 2, ts_ptr);
    if (result.status > 0) {
      for (int i = 0; i < result.status; i++) {
        if (events[i].filter == EVFILT_READ) result.events |= POLL_EVENT_READABLE;
        if (events[i].filter == EVFILT_WRITE) result.events |= POLL_EVENT_WRITABLE;
        if (events[i].flags & (EV_EOF | EV_ERROR)) result.events |= POLL_EVENT_READABLE;
      }
    }
    return result;
  }

 private:
  curl_socket_t socket_ = -1;
  int kqueue_fd_ = -1;
  int events_ = 0;
};

#else
// Fallback: poll()
class SocketPoller {
 public:
  SocketPoller() = default;
  ~SocketPoller() = default;

  bool Init(curl_socket_t socket, int events) {
    socket_ = socket;
    events_ = events;
    return true;
  }

  void Close() {}

  PollResult Poll(int timeout_ms) {
    PollResult result = {0, 0};
    struct pollfd pfd;
    pfd.fd = socket_;
    pfd.events = 0;
    pfd.revents = 0;
    if (events_ & POLL_EVENT_READABLE) pfd.events |= POLLIN;
    if (events_ & POLL_EVENT_WRITABLE) pfd.events |= POLLOUT;

    result.status = poll(&pfd, 1, timeout_ms);
    if (result.status > 0) {
      if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) result.events |= POLL_EVENT_READABLE;
      if (pfd.revents & POLLOUT) result.events |= POLL_EVENT_WRITABLE;
    }
    return result;
  }

 private:
  curl_socket_t socket_ = -1;
  int events_ = 0;
};
#endif

//=============================================================================
// Multi-Socket Poller - Platform-specific implementations
//=============================================================================

#ifdef _WIN32
// Windows: WSAPoll
class MultiSocketPoller {
 public:
  MultiSocketPoller() = default;
  ~MultiSocketPoller() = default;

  bool Init() { return true; }
  void Close() { sockets_.clear(); }

  bool AddSocket(curl_socket_t socket, int events) {
    WSAPOLLFD pfd;
    pfd.fd = socket;
    pfd.events = 0;
    pfd.revents = 0;
    if (events & POLL_EVENT_READABLE) pfd.events |= POLLIN;
    if (events & POLL_EVENT_WRITABLE) pfd.events |= POLLOUT;
    sockets_[socket] = pfd;
    return true;
  }

  bool ModifySocket(curl_socket_t socket, int events) {
    auto it = sockets_.find(socket);
    if (it == sockets_.end()) return false;
    it->second.events = 0;
    if (events & POLL_EVENT_READABLE) it->second.events |= POLLIN;
    if (events & POLL_EVENT_WRITABLE) it->second.events |= POLLOUT;
    return true;
  }

  bool RemoveSocket(curl_socket_t socket) { return sockets_.erase(socket) > 0; }

  int Poll(int timeout_ms, std::vector<MultiPollResult>& results) {
    results.clear();
    if (sockets_.empty()) return 0;

    std::vector<WSAPOLLFD> pfds;
    pfds.reserve(sockets_.size());
    for (auto& pair : sockets_) {
      pair.second.revents = 0;
      pfds.push_back(pair.second);
    }

    int ret = WSAPoll(pfds.data(), static_cast<ULONG>(pfds.size()), timeout_ms);
    if (ret <= 0) return ret;

    for (const auto& pfd : pfds) {
      if (pfd.revents) {
        MultiPollResult r;
        r.sockfd = pfd.fd;
        r.events = 0;
        if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) r.events |= POLL_EVENT_READABLE;
        if (pfd.revents & POLLOUT) r.events |= POLL_EVENT_WRITABLE;
        results.push_back(r);
      }
    }
    return static_cast<int>(results.size());
  }

  size_t SocketCount() const { return sockets_.size(); }

 private:
  std::unordered_map<curl_socket_t, WSAPOLLFD> sockets_;
};

#elif defined(__linux__)
// Linux: epoll
class MultiSocketPoller {
 public:
  MultiSocketPoller() = default;
  ~MultiSocketPoller() { Close(); }

  bool Init() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    return epoll_fd_ != -1;
  }

  void Close() {
    if (epoll_fd_ != -1) {
      close(epoll_fd_);
      epoll_fd_ = -1;
    }
    sockets_.clear();
  }

  bool AddSocket(curl_socket_t socket, int events) {
    struct epoll_event ev;
    ev.events = 0;
    if (events & POLL_EVENT_READABLE) ev.events |= EPOLLIN;
    if (events & POLL_EVENT_WRITABLE) ev.events |= EPOLLOUT;
    ev.data.fd = socket;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, socket, &ev) == -1) return false;
    sockets_.insert(socket);
    return true;
  }

  bool ModifySocket(curl_socket_t socket, int events) {
    struct epoll_event ev;
    ev.events = 0;
    if (events & POLL_EVENT_READABLE) ev.events |= EPOLLIN;
    if (events & POLL_EVENT_WRITABLE) ev.events |= EPOLLOUT;
    ev.data.fd = socket;
    return epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, socket, &ev) == 0;
  }

  bool RemoveSocket(curl_socket_t socket) {
    if (sockets_.erase(socket) == 0) return false;
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, socket, nullptr);
    return true;
  }

  int Poll(int timeout_ms, std::vector<MultiPollResult>& results) {
    results.clear();
    if (sockets_.empty()) return 0;

    std::vector<struct epoll_event> events(sockets_.size());
    int ret = epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), timeout_ms);
    if (ret <= 0) return ret;

    for (int i = 0; i < ret; i++) {
      MultiPollResult r;
      r.sockfd = events[i].data.fd;
      r.events = 0;
      if (events[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR)) r.events |= POLL_EVENT_READABLE;
      if (events[i].events & EPOLLOUT) r.events |= POLL_EVENT_WRITABLE;
      results.push_back(r);
    }
    return static_cast<int>(results.size());
  }

  size_t SocketCount() const { return sockets_.size(); }

 private:
  int epoll_fd_ = -1;
  std::unordered_set<curl_socket_t> sockets_;
};

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
// macOS/BSD: kqueue
class MultiSocketPoller {
 public:
  MultiSocketPoller() = default;
  ~MultiSocketPoller() { Close(); }

  bool Init() {
    kqueue_fd_ = kqueue();
    return kqueue_fd_ != -1;
  }

  void Close() {
    if (kqueue_fd_ != -1) {
      close(kqueue_fd_);
      kqueue_fd_ = -1;
    }
    sockets_.clear();
  }

  bool AddSocket(curl_socket_t socket, int events) {
    struct kevent changes[2];
    int nchanges = 0;
    if (events & POLL_EVENT_READABLE) {
      EV_SET(&changes[nchanges++], socket, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }
    if (events & POLL_EVENT_WRITABLE) {
      EV_SET(&changes[nchanges++], socket, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }
    if (nchanges > 0 && kevent(kqueue_fd_, changes, nchanges, nullptr, 0, nullptr) == -1) {
      return false;
    }
    sockets_[socket] = events;
    return true;
  }

  bool ModifySocket(curl_socket_t socket, int events) {
    auto it = sockets_.find(socket);
    if (it == sockets_.end()) return false;

    int oldEvents = it->second;
    struct kevent changes[4];
    int nchanges = 0;

    // Remove old filters no longer needed
    if ((oldEvents & POLL_EVENT_READABLE) && !(events & POLL_EVENT_READABLE)) {
      EV_SET(&changes[nchanges++], socket, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    }
    if ((oldEvents & POLL_EVENT_WRITABLE) && !(events & POLL_EVENT_WRITABLE)) {
      EV_SET(&changes[nchanges++], socket, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    }
    // Add new filters
    if (!(oldEvents & POLL_EVENT_READABLE) && (events & POLL_EVENT_READABLE)) {
      EV_SET(&changes[nchanges++], socket, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }
    if (!(oldEvents & POLL_EVENT_WRITABLE) && (events & POLL_EVENT_WRITABLE)) {
      EV_SET(&changes[nchanges++], socket, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }

    if (nchanges > 0 && kevent(kqueue_fd_, changes, nchanges, nullptr, 0, nullptr) == -1) {
      return false;
    }
    it->second = events;
    return true;
  }

  bool RemoveSocket(curl_socket_t socket) {
    auto it = sockets_.find(socket);
    if (it == sockets_.end()) return false;

    struct kevent changes[2];
    int nchanges = 0;
    if (it->second & POLL_EVENT_READABLE) {
      EV_SET(&changes[nchanges++], socket, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    }
    if (it->second & POLL_EVENT_WRITABLE) {
      EV_SET(&changes[nchanges++], socket, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    }
    kevent(kqueue_fd_, changes, nchanges, nullptr, 0, nullptr);
    sockets_.erase(it);
    return true;
  }

  int Poll(int timeout_ms, std::vector<MultiPollResult>& results) {
    results.clear();
    if (sockets_.empty()) return 0;

    std::vector<struct kevent> events(sockets_.size() * 2);
    struct timespec ts;
    struct timespec* ts_ptr = nullptr;

    if (timeout_ms >= 0) {
      ts.tv_sec = timeout_ms / 1000;
      ts.tv_nsec = (timeout_ms % 1000) * 1000000;
      ts_ptr = &ts;
    }

    int ret =
        kevent(kqueue_fd_, nullptr, 0, events.data(), static_cast<int>(events.size()), ts_ptr);
    if (ret <= 0) return ret;

    // Merge events per socket
    std::unordered_map<curl_socket_t, int> socketEvents;
    for (int i = 0; i < ret; i++) {
      curl_socket_t sock = static_cast<curl_socket_t>(events[i].ident);
      int& evts = socketEvents[sock];
      if (events[i].filter == EVFILT_READ) evts |= POLL_EVENT_READABLE;
      if (events[i].filter == EVFILT_WRITE) evts |= POLL_EVENT_WRITABLE;
      if (events[i].flags & (EV_EOF | EV_ERROR)) evts |= POLL_EVENT_READABLE;
    }

    for (const auto& pair : socketEvents) {
      results.push_back({pair.first, pair.second});
    }
    return static_cast<int>(results.size());
  }

  size_t SocketCount() const { return sockets_.size(); }

 private:
  int kqueue_fd_ = -1;
  std::unordered_map<curl_socket_t, int> sockets_;
};

#else
// Fallback: poll()
class MultiSocketPoller {
 public:
  MultiSocketPoller() = default;
  ~MultiSocketPoller() = default;

  bool Init() { return true; }
  void Close() { sockets_.clear(); }

  bool AddSocket(curl_socket_t socket, int events) {
    sockets_[socket] = events;
    return true;
  }

  bool ModifySocket(curl_socket_t socket, int events) {
    auto it = sockets_.find(socket);
    if (it == sockets_.end()) return false;
    it->second = events;
    return true;
  }

  bool RemoveSocket(curl_socket_t socket) { return sockets_.erase(socket) > 0; }

  int Poll(int timeout_ms, std::vector<MultiPollResult>& results) {
    results.clear();
    if (sockets_.empty()) return 0;

    std::vector<struct pollfd> pfds;
    pfds.reserve(sockets_.size());
    for (const auto& pair : sockets_) {
      struct pollfd pfd;
      pfd.fd = pair.first;
      pfd.events = 0;
      pfd.revents = 0;
      if (pair.second & POLL_EVENT_READABLE) pfd.events |= POLLIN;
      if (pair.second & POLL_EVENT_WRITABLE) pfd.events |= POLLOUT;
      pfds.push_back(pfd);
    }

    int ret = poll(pfds.data(), pfds.size(), timeout_ms);
    if (ret <= 0) return ret;

    for (const auto& pfd : pfds) {
      if (pfd.revents) {
        MultiPollResult r;
        r.sockfd = pfd.fd;
        r.events = 0;
        if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) r.events |= POLL_EVENT_READABLE;
        if (pfd.revents & POLLOUT) r.events |= POLL_EVENT_WRITABLE;
        results.push_back(r);
      }
    }
    return static_cast<int>(results.size());
  }

  size_t SocketCount() const { return sockets_.size(); }

 private:
  std::unordered_map<curl_socket_t, int> sockets_;
};
#endif

}  // namespace NodeLibcurl

#endif  // NODELIBCURL_THREAD_UTILS_H
