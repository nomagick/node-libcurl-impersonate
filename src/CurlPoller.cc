/**
 * Copyright (c) Jonathan Cardoso Machado. All Rights Reserved.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include "CurlPoller.h"

#include <algorithm>
#include <cassert>

#ifdef NODE_LIBCURL_DEBUG
#include <iostream>
#endif

namespace NodeLibcurl {

// Default poll timeout when no libcurl timeout is set (100ms for responsiveness)
constexpr int DEFAULT_POLL_TIMEOUT_MS = 100;

CurlPoller::CurlPoller(Napi::Env env, SocketCallback onSocket, TimeoutCallback onTimeout)
    : onSocket_(std::move(onSocket)), onTimeout_(std::move(onTimeout)) {
  // Initialize the multi-socket poller
  poller_.Init();

  // Create ThreadSafeFunction for callbacks to JS thread
  tsfn_ = TsfnType::New(env,
                        Napi::Function(),  // No JS function, we use the callback in TsfnCallback
                        "CurlPollerCallback",
                        0,   // Unlimited queue
                        1);  // Initial thread count
}

CurlPoller::~CurlPoller() {
  Stop();
  poller_.Close();
}

bool CurlPoller::Start() {
  if (running_.load()) {
    return true;  // Already running
  }

  running_.store(true);
  stopping_.store(false);

  pollThread_ = std::thread(&CurlPoller::PollThreadFunc, this);
  return true;
}

void CurlPoller::Stop() {
  if (!running_.load()) {
    return;  // Not running
  }

  stopping_.store(true);

  // Send stop command
  SendCommand({PollerCommandType::STOP});

  // Wait for thread to finish
  if (pollThread_.joinable()) {
    pollThread_.join();
  }

  running_.store(false);

  // Release the TSFN
  tsfn_.Release();
}

void CurlPoller::AddSocket(curl_socket_t sockfd, int events) {
  SendCommand({PollerCommandType::ADD_SOCKET, sockfd, events});
}

void CurlPoller::ModifySocket(curl_socket_t sockfd, int events) {
  SendCommand({PollerCommandType::MODIFY_SOCKET, sockfd, events});
}

void CurlPoller::RemoveSocket(curl_socket_t sockfd) {
  SendCommand({PollerCommandType::REMOVE_SOCKET, sockfd});
}

void CurlPoller::SetTimeout(long timeout_ms) {
  timeoutMs_.store(timeout_ms);
  timeoutStart_ = std::chrono::steady_clock::now();

  // Wake up the polling thread if it's waiting
  SendCommand({PollerCommandType::SET_TIMEOUT, -1, 0, timeout_ms});
}

void CurlPoller::SendCommand(PollerCommand cmd) {
  {
    std::lock_guard<std::mutex> lock(commandMutex_);
    commandQueue_.push(std::move(cmd));
  }
  commandCv_.notify_one();
}

void CurlPoller::ProcessCommand(const PollerCommand& cmd) {
  switch (cmd.type) {
    case PollerCommandType::ADD_SOCKET:
      poller_.AddSocket(cmd.sockfd, cmd.events);
#ifdef NODE_LIBCURL_DEBUG
      std::cout << "[CurlPoller] Added socket " << cmd.sockfd << " with events " << cmd.events
                << std::endl;
#endif
      break;

    case PollerCommandType::MODIFY_SOCKET:
      poller_.ModifySocket(cmd.sockfd, cmd.events);
#ifdef NODE_LIBCURL_DEBUG
      std::cout << "[CurlPoller] Modified socket " << cmd.sockfd << " with events " << cmd.events
                << std::endl;
#endif
      break;

    case PollerCommandType::REMOVE_SOCKET:
      poller_.RemoveSocket(cmd.sockfd);
#ifdef NODE_LIBCURL_DEBUG
      std::cout << "[CurlPoller] Removed socket " << cmd.sockfd << std::endl;
#endif
      break;

    case PollerCommandType::SET_TIMEOUT:
      // Timeout is handled via atomic variable, just need to wake up
#ifdef NODE_LIBCURL_DEBUG
      std::cout << "[CurlPoller] Set timeout to " << cmd.timeout_ms << "ms" << std::endl;
#endif
      break;

    case PollerCommandType::STOP:
      // Handled in main loop
      break;
  }
}

void CurlPoller::PollThreadFunc() {
#ifdef NODE_LIBCURL_DEBUG
  std::cout << "[CurlPoller] Poll thread started" << std::endl;
#endif

  while (!stopping_.load()) {
    // Process any pending commands
    {
      std::unique_lock<std::mutex> lock(commandMutex_);

      // If no sockets and no timeout, wait for commands
      while (!stopping_.load() && commandQueue_.empty() && poller_.SocketCount() == 0 &&
             timeoutMs_.load() < 0) {
        commandCv_.wait(lock);
      }

      // Process all pending commands
      while (!commandQueue_.empty()) {
        PollerCommand cmd = std::move(commandQueue_.front());
        commandQueue_.pop();

        if (cmd.type == PollerCommandType::STOP) {
          return;  // Exit thread
        }

        lock.unlock();
        ProcessCommand(cmd);
        lock.lock();
      }
    }

    if (stopping_.load()) {
      break;
    }

    // Calculate poll timeout
    int pollTimeout = DEFAULT_POLL_TIMEOUT_MS;
    long currentTimeoutMs = timeoutMs_.load();

    if (currentTimeoutMs >= 0) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - timeoutStart_)
                         .count();
      long remaining = currentTimeoutMs - elapsed;

      if (remaining <= 0) {
        // Timeout expired, fire callback
        timeoutMs_.store(-1);

        auto* context = new TsfnContext{this, -1, 0, true};
        tsfn_.NonBlockingCall(context);

#ifdef NODE_LIBCURL_DEBUG
        std::cout << "[CurlPoller] Timeout expired, firing callback" << std::endl;
#endif
        continue;
      }

      pollTimeout = std::min(pollTimeout, static_cast<int>(remaining));
    }

    // If no sockets, just sleep briefly and check for commands
    if (poller_.SocketCount() == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(std::min(pollTimeout, 10)));
      continue;
    }

    // Poll sockets
    std::vector<MultiPollResult> results;
    int ret = poller_.Poll(pollTimeout, results);

    if (ret < 0) {
#ifdef NODE_LIBCURL_DEBUG
      std::cout << "[CurlPoller] Poll error: " << GetSocketErrorString() << std::endl;
#endif
      continue;
    }

    // Process socket events
    for (const auto& result : results) {
      auto* context = new TsfnContext{this, result.sockfd, result.events, false};
      tsfn_.NonBlockingCall(context);

#ifdef NODE_LIBCURL_DEBUG
      std::cout << "[CurlPoller] Socket " << result.sockfd << " has events " << result.events
                << std::endl;
#endif
    }

    // Check for timeout after polling
    currentTimeoutMs = timeoutMs_.load();
    if (currentTimeoutMs >= 0) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - timeoutStart_)
                         .count();
      if (elapsed >= currentTimeoutMs) {
        // Timeout expired
        timeoutMs_.store(-1);

        auto* context = new TsfnContext{this, -1, 0, true};
        tsfn_.NonBlockingCall(context);

#ifdef NODE_LIBCURL_DEBUG
        std::cout << "[CurlPoller] Timeout expired after poll" << std::endl;
#endif
      }
    }
  }

#ifdef NODE_LIBCURL_DEBUG
  std::cout << "[CurlPoller] Poll thread exiting" << std::endl;
#endif
}

void CurlPoller::TsfnCallback(Napi::Env env, Napi::Function /*jsCallback*/, void* /*context*/,
                              TsfnContext* data) {
  if (!data) return;

  // Clean up context when we're done
  std::unique_ptr<TsfnContext> ctx(data);

  // Check if environment is valid
  if (env.IsExceptionPending()) {
    return;
  }

  CurlPoller* poller = ctx->poller;
  if (!poller) return;

  try {
    if (ctx->isTimeout) {
      if (poller->onTimeout_) {
        poller->onTimeout_();
      }
    } else {
      if (poller->onSocket_) {
        poller->onSocket_(ctx->sockfd, ctx->events);
      }
    }
  } catch (const std::exception& e) {
#ifdef NODE_LIBCURL_DEBUG
    std::cout << "[CurlPoller] Exception in callback: " << e.what() << std::endl;
#endif
  }
}

int CurlPoller::CurlActionToPollEvents(int action) {
  int events = 0;
  if (action == CURL_POLL_IN || action == CURL_POLL_INOUT) {
    events |= POLL_EVENT_READABLE;
  }
  if (action == CURL_POLL_OUT || action == CURL_POLL_INOUT) {
    events |= POLL_EVENT_WRITABLE;
  }
  return events;
}

int CurlPoller::PollEventsToCurlFlags(int events) {
  int flags = 0;
  if (events & POLL_EVENT_READABLE) {
    flags |= CURL_CSELECT_IN;
  }
  if (events & POLL_EVENT_WRITABLE) {
    flags |= CURL_CSELECT_OUT;
  }
  return flags;
}

}  // namespace NodeLibcurl
