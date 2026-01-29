/**
 * Copyright (c) Jonathan Cardoso Machado. All Rights Reserved.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#pragma once

#include "CurlPoller.h"
#include "macros.h"

#include <curl/curl.h>

#include <atomic>
#include <map>
#include <memory>
#include <napi.h>
#include <unordered_map>

namespace NodeLibcurl {

// Forward declaration
class Easy;

class Multi : public Napi::ObjectWrap<Multi> {
 public:
  // Constructor and destructor
  Multi(const Napi::CallbackInfo& info);
  ~Multi();

  // Static methods for JS class initialization
  static Napi::Function Init(Napi::Env env, Napi::Object exports);

  // Instance methods exposed to JS
  Napi::Value SetOpt(const Napi::CallbackInfo& info);
  Napi::Value AddHandle(const Napi::CallbackInfo& info);
  Napi::Value RemoveHandle(const Napi::CallbackInfo& info);
  Napi::Value Perform(const Napi::CallbackInfo& info);
  Napi::Value OnMessage(const Napi::CallbackInfo& info);
  Napi::Value GetCount(const Napi::CallbackInfo& info);
  Napi::Value Close(const Napi::CallbackInfo& info);
  Napi::Value GetterId(const Napi::CallbackInfo& info);

  // Static methods
  static Napi::Value StrError(const Napi::CallbackInfo& info);

  // Debug support
  uint64_t GetDebugId() const { return id; }

  // Public members
  CURLM* mh;
  bool isOpen = true;
  int amountOfHandles = 0;
  int runningHandles = 0;

 private:
  // Private methods
  void StopTimer();
  void Dispose();
  void ProcessMessages();
  void CallOnMessageCallback(CURL* easy, CURLcode statusCode);

  // Callback handlers for CurlPoller (called on main thread via TSFN)
  void OnSocketEvent(curl_socket_t sockfd, int events);
  void OnTimeoutEvent();

  // Callback management
  typedef std::map<CURLMoption, Napi::FunctionReference> CallbacksMap;
  CallbacksMap callbacks;
  Napi::FunctionReference cbOnMessage;

  // Promise-based perform tracking
  std::map<CURL*, std::shared_ptr<Napi::Promise::Deferred>> handlePromiseMap;

  // Easy handle references - prevents GC while handles are in the multi
  std::unordered_map<CURL*, Napi::ObjectReference> easyHandleRefs;

  // CurlPoller for socket polling (replaces libuv)
  std::unique_ptr<CurlPoller> poller_;

  // Timer state
  bool timerClosed = false;
  napi_async_cleanup_hook_handle removeHandle;
  uint64_t id;

  // Notification API support (libcurl >= 8.17.0)
  bool useNotificationsApi = false;

  // Static members
  static std::atomic<uint64_t> nextId;

  // libcurl multi callbacks
  static int HandleSocket(CURL* easy, curl_socket_t s, int action, void* userp, void* socketp);
  static int HandleTimeout(CURLM* multi, long timeoutMs, void* userp);
  static int CbPushFunction(CURL* parent, CURL* child, size_t numberOfHeaders,
                            struct curl_pushheaders* headers, void* userPtr);

  // Cleanup hooks
  static void CleanupHookAsync(napi_async_cleanup_hook_handle handle, void* data);

#if NODE_LIBCURL_VER_GE(8, 17, 0)
  // libcurl notification callback (available since 8.17.0)
  static void NotifyCallback(CURLM* multi, unsigned int notification, CURL* easy, void* notifyp);
#endif

  // Prevent copying
  Multi(const Multi& that) = delete;
  Multi& operator=(const Multi& that) = delete;
};

}  // namespace NodeLibcurl
