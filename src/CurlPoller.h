/**
 * Copyright (c) Jonathan Cardoso Machado. All Rights Reserved.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#ifndef NODELIBCURL_CURL_POLLER_H
#define NODELIBCURL_CURL_POLLER_H

#include "ThreadUtils.h"

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <napi.h>
#include <queue>
#include <thread>

namespace NodeLibcurl {

/**
 * CurlPoller: A dedicated polling thread for libcurl socket operations.
 *
 * This class replaces libuv-based polling with a dedicated thread that uses
 * platform-specific efficient polling (epoll/kqueue/WSAPoll) and communicates
 * back to the JavaScript thread via Napi::ThreadSafeFunction.
 *
 * Design:
 * - Main thread calls AddSocket/ModifySocket/RemoveSocket/SetTimeout
 * - Polling thread runs continuously, polling sockets and timers
 * - When events occur, polling thread calls TSFN to execute callbacks on main thread
 */

// Forward declaration
class CurlPoller;

// Callback types
using SocketCallback = std::function<void(curl_socket_t sockfd, int events)>;
using TimeoutCallback = std::function<void()>;

// Command types for thread-safe communication
enum class PollerCommandType { ADD_SOCKET, MODIFY_SOCKET, REMOVE_SOCKET, SET_TIMEOUT, STOP };

struct PollerCommand {
  PollerCommandType type;
  curl_socket_t sockfd = -1;
  int events = 0;
  long timeout_ms = -1;  // -1 means no timeout
};

/**
 * CurlPoller manages socket polling in a dedicated thread.
 *
 * Usage:
 * 1. Create CurlPoller with callbacks
 * 2. Call Start() to begin polling
 * 3. Use AddSocket/ModifySocket/RemoveSocket/SetTimeout as needed
 * 4. Call Stop() before destruction
 */
class CurlPoller {
 public:
  /**
   * Create a CurlPoller.
   *
   * @param env The Napi environment
   * @param onSocket Callback when socket events occur (called on main thread)
   * @param onTimeout Callback when timeout expires (called on main thread)
   */
  CurlPoller(Napi::Env env, SocketCallback onSocket, TimeoutCallback onTimeout);

  ~CurlPoller();

  /**
   * Start the polling thread.
   * Must be called before any socket operations.
   */
  bool Start();

  /**
   * Stop the polling thread.
   * Safe to call multiple times.
   */
  void Stop();

  /**
   * Add a socket to be polled.
   *
   * @param sockfd The socket file descriptor
   * @param events Bitmask of POLL_EVENT_READABLE | POLL_EVENT_WRITABLE
   */
  void AddSocket(curl_socket_t sockfd, int events);

  /**
   * Modify the events being watched on a socket.
   *
   * @param sockfd The socket file descriptor
   * @param events New bitmask of POLL_EVENT_READABLE | POLL_EVENT_WRITABLE
   */
  void ModifySocket(curl_socket_t sockfd, int events);

  /**
   * Remove a socket from polling.
   *
   * @param sockfd The socket file descriptor
   */
  void RemoveSocket(curl_socket_t sockfd);

  /**
   * Set the timeout for the next poll.
   * libcurl uses this for connection timeouts and retries.
   *
   * @param timeout_ms Timeout in milliseconds. -1 to disable timeout.
   */
  void SetTimeout(long timeout_ms);

  /**
   * Check if the polling thread is running.
   */
  bool IsRunning() const { return running_.load(); }

 private:
  // Thread function
  void PollThreadFunc();

  // Process a command from the queue
  void ProcessCommand(const PollerCommand& cmd);

  // Send command to the polling thread
  void SendCommand(PollerCommand cmd);

  // Convert libcurl action to poll events
  static int CurlActionToPollEvents(int action);

  // Convert poll events to libcurl flags
  static int PollEventsToCurlFlags(int events);

  // ThreadSafeFunction callback context
  struct TsfnContext {
    CurlPoller* poller;
    curl_socket_t sockfd;
    int events;
    bool isTimeout;
  };

  // ThreadSafeFunction callback
  static void TsfnCallback(Napi::Env env, Napi::Function jsCallback, void* context,
                           TsfnContext* data);

  // Poller state
  MultiSocketPoller poller_;
  std::thread pollThread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopping_{false};

  // Command queue for thread-safe communication
  std::queue<PollerCommand> commandQueue_;
  std::mutex commandMutex_;
  std::condition_variable commandCv_;

  // Current timeout
  std::atomic<long> timeoutMs_{-1};
  std::chrono::steady_clock::time_point timeoutStart_;

  // Callbacks
  SocketCallback onSocket_;
  TimeoutCallback onTimeout_;

  // ThreadSafeFunction for calling back to JS thread
  using TsfnType = Napi::TypedThreadSafeFunction<void, TsfnContext, TsfnCallback>;
  TsfnType tsfn_;

  // Prevent copying
  CurlPoller(const CurlPoller&) = delete;
  CurlPoller& operator=(const CurlPoller&) = delete;
};

}  // namespace NodeLibcurl

#endif  // NODELIBCURL_CURL_POLLER_H
