#ifndef NOMINMAX
#define NOMINMAX
#include <curl/curl.h>
#endif

#include "curl/multi.h"
#include "macros.h"
#include "napi.h"

#include <cassert>

/**
 * Copyright (c) Jonathan Cardoso Machado. All Rights Reserved.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include "Curl.h"
#include "CurlError.h"
#include "CurlPoller.h"
#include "Easy.h"
#include "Http2PushFrameHeaders.h"
#include "LocaleGuard.h"
#include "Multi.h"
#include "js_native_api.h"
#include "napi.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// 85233 was allocated on Win64
#define MEMORY_PER_HANDLE 60000

namespace NodeLibcurl {

std::atomic<uint64_t> Multi::nextId = 0;

// Constructor
Multi::Multi(const Napi::CallbackInfo& info) : Napi::ObjectWrap<Multi>(info), id(nextId++) {
  NODE_LIBCURL_DEBUG_LOG(this, "Multi::Constructor", "");
  Napi::Env env = info.Env();
  auto curl = env.GetInstanceData<Curl>();

#if NODE_LIBCURL_VER_GE(8, 17, 0)
  bool shouldUseNotificationsApi = true;
#else
  bool shouldUseNotificationsApi = false;
#endif

  if (info.Length() >= 1 && info[0].IsObject()) {
    Napi::Object options = info[0].As<Napi::Object>();
    if (options.Has("shouldUseNotificationsApi")) {
      Napi::Value value = options.Get("shouldUseNotificationsApi");
      if (value.IsBoolean()) {
        shouldUseNotificationsApi = value.As<Napi::Boolean>().Value();
      }
    }
  }

  // Initialize multi handle
  this->mh = curl_multi_init();
  assert(this->mh && "Failed to initialize multi handle");

  // Set default options
  curl_multi_setopt(this->mh, CURLMOPT_SOCKETFUNCTION, Multi::HandleSocket);
  curl_multi_setopt(this->mh, CURLMOPT_SOCKETDATA, this);
  curl_multi_setopt(this->mh, CURLMOPT_TIMERFUNCTION, Multi::HandleTimeout);
  curl_multi_setopt(this->mh, CURLMOPT_TIMERDATA, this);

  // Create CurlPoller with callbacks
  this->poller_ = std::make_unique<CurlPoller>(
      env,
      // Socket callback - called when socket has events
      [this](curl_socket_t sockfd, int events) { this->OnSocketEvent(sockfd, events); },
      // Timeout callback - called when timeout expires
      [this]() { this->OnTimeoutEvent(); });

  // Start the polling thread
  this->poller_->Start();

  // We need to keep the reference alive for the duration of the poller
  this->Ref();

  // Enable notification API if requested and supported
  if (shouldUseNotificationsApi) {
#if NODE_LIBCURL_VER_GE(8, 17, 0)
    // Enable notification callback
    curl_multi_setopt(this->mh, CURLMOPT_NOTIFYFUNCTION, Multi::NotifyCallback);
    curl_multi_setopt(this->mh, CURLMOPT_NOTIFYDATA, this);

    // Enable INFO_READ notifications
    CURLMcode code = curl_multi_notify_enable(this->mh, CURLMNOTIFY_INFO_READ);
    if (code == CURLM_OK) {
      this->useNotificationsApi = true;
      NODE_LIBCURL_DEBUG_LOG(this, "Multi::Constructor", "Notification API enabled");
    } else {
      NODE_LIBCURL_DEBUG_LOG(this, "Multi::Constructor",
                             "Failed to enable notifications, falling back to ProcessMessages");
    }
#else
    NODE_LIBCURL_DEBUG_LOG(this, "Multi::Constructor",
                           "shouldUseNotificationsApi enabled but compiled against "
                           "libcurl < 8.17, falling back to ProcessMessages");
#endif
  }

  napi_add_async_cleanup_hook(env, Multi::CleanupHookAsync, this, &removeHandle);

  curl->AdjustHandleMemory(CURL_HANDLE_TYPE_MULTI, 1);
}

void Multi::CleanupHookAsync(napi_async_cleanup_hook_handle handle, void* data) {
  Multi* multi = static_cast<Multi*>(data);
  NODE_LIBCURL_DEBUG_LOG(multi, "Multi::CleanupHookAsync", "");

  // Stop the poller
  if (multi->poller_) {
    multi->poller_->Stop();
    multi->poller_.reset();
  }

  // Mark timer as closed and unref
  if (!multi->timerClosed) {
    multi->timerClosed = true;
    napi_remove_async_cleanup_hook(multi->removeHandle);
    multi->Unref();
  }
}

// Destructor
Multi::~Multi() {
  NODE_LIBCURL_DEBUG_LOG(this, "Multi::Destructor", "isOpen: " + std::to_string(this->isOpen));
  if (this->isOpen) {
    this->Dispose();
  }
}

void Multi::Dispose() {
  if (!this->isOpen) return;

  NODE_LIBCURL_DEBUG_LOG(this, "Multi::Dispose", "");

  this->isOpen = false;

  // Stop the poller
  if (this->poller_) {
    this->poller_->Stop();
    this->poller_.reset();
  }

  auto curl = this->Env().GetInstanceData<Curl>();

  // Clear callbacks
  this->callbacks.clear();
  this->cbOnMessage.Reset();

  // Clear Easy handle references
  this->easyHandleRefs.clear();

  // Clean up multi handle
  if (this->mh) {
    CURLMcode code = curl_multi_cleanup(this->mh);
    assert(code == CURLM_OK);
    this->mh = nullptr;
  }
  if (!this->timerClosed) {
    this->timerClosed = true;
    napi_remove_async_cleanup_hook(this->removeHandle);
    this->Unref();
  }

  curl->AdjustHandleMemory(CURL_HANDLE_TYPE_MULTI, -1);
}

void Multi::StopTimer() {
  if (this->poller_) {
    this->poller_->SetTimeout(-1);  // Disable timeout
  }
}

// Initialize the class for export
Napi::Function Multi::Init(Napi::Env env, Napi::Object exports) {
  NODE_LIBCURL_DEBUG_LOG_STATIC(static_cast<napi_env>(env), "Multi::Init");

  Napi::Function func = DefineClass(
      env, "Multi",
      {// Instance methods
       InstanceMethod("setOpt", &Multi::SetOpt), InstanceMethod("addHandle", &Multi::AddHandle),
       InstanceMethod("removeHandle", &Multi::RemoveHandle),
       InstanceMethod("perform", &Multi::Perform), InstanceMethod("onMessage", &Multi::OnMessage),
       InstanceMethod("getCount", &Multi::GetCount), InstanceMethod("close", &Multi::Close),

       // Instance accessors
       InstanceAccessor("id", &Multi::GetterId, nullptr),

       // Static methods
       StaticMethod("strError", &Multi::StrError)});

  exports.Set("Multi", func);

  return func;
}

Napi::Value Multi::SetOpt(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (!this->isOpen) {
    throw CurlError::New(env, "Multi handle is closed", CURLM_BAD_HANDLE);
  }

  if (info.Length() < 2) {
    throw Napi::TypeError::New(env, "Wrong number of arguments");
  }

  Napi::Value opt = info[0];
  Napi::Value value = info[1];

  CURLMcode setOptRetCode = CURLM_UNKNOWN_OPTION;

  int optionId;

  // array of strings option
  if ((optionId = IsInsideCurlConstantStruct(curlMultiOptionNotImplemented, opt))) {
    throw Napi::TypeError::New(env,
                               "Unsupported option, probably because it's too complex to implement "
                               "using javascript or unecessary when using javascript.");
  } else if ((optionId = IsInsideCurlConstantStruct(curlMultiOptionStringArray, opt))) {
    if (value.IsNull()) {
      setOptRetCode = curl_multi_setopt(this->mh, static_cast<CURLMoption>(optionId), nullptr);

    } else {
      if (!value.IsArray()) {
        throw CurlError::New(env, "Option value must be an Array.", CURLM_BAD_FUNCTION_ARGUMENT);
      }

      Napi::Array array = value.As<Napi::Array>();
      uint32_t arrayLength = array.Length();
      std::vector<std::string> strings;
      std::vector<const char*> cStrings;

      for (uint32_t i = 0; i < arrayLength; ++i) {
        Napi::Value element = array.Get(i);

        if (!element.IsString()) {
          throw CurlError::New(env, "Option value must be an Array of Strings.",
                               CURLM_BAD_FUNCTION_ARGUMENT);
        }

        strings.push_back(element.As<Napi::String>().Utf8Value());
        cStrings.push_back(strings.back().c_str());
      }

      cStrings.push_back(nullptr);

      setOptRetCode = curl_multi_setopt(this->mh, static_cast<CURLMoption>(optionId), &cStrings[0]);
    }

    // check if option is integer, and the value is correct
  } else if ((optionId = IsInsideCurlConstantStruct(curlMultiOptionInteger, opt))) {
    // If not an integer, throw error
    if (!value.IsNumber()) {
      throw CurlError::New(env, "Option value must be an integer.", CURLM_BAD_FUNCTION_ARGUMENT);
    }

    int32_t val = value.As<Napi::Number>().Int32Value();

    setOptRetCode = curl_multi_setopt(this->mh, static_cast<CURLMoption>(optionId), val);
  } else if ((optionId = IsInsideCurlConstantStruct(curlMultiOptionFunction, opt))) {
    bool isNull = value.IsNull();

    if (!value.IsFunction() && !isNull) {
      throw CurlError::New(env, "Option value must be null or a function.",
                           CURLM_BAD_FUNCTION_ARGUMENT);
    }

    switch (optionId) {
#if NODE_LIBCURL_VER_GE(7, 44, 0)
      case CURLMOPT_PUSHFUNCTION:

        if (isNull) {
          this->callbacks.erase(CURLMOPT_PUSHFUNCTION);

          curl_multi_setopt(this->mh, CURLMOPT_PUSHDATA, nullptr);
          setOptRetCode = curl_multi_setopt(this->mh, CURLMOPT_PUSHFUNCTION, nullptr);
        } else {
          this->callbacks[CURLMOPT_PUSHFUNCTION] = Napi::Persistent(value.As<Napi::Function>());

          curl_multi_setopt(this->mh, CURLMOPT_PUSHDATA, this);
          setOptRetCode = curl_multi_setopt(this->mh, CURLMOPT_PUSHFUNCTION, Multi::CbPushFunction);
        }

        break;
#endif
    }
  }

  return Napi::Number::New(env, setOptRetCode);
}

Napi::Value Multi::AddHandle(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  auto curl = env.GetInstanceData<Curl>();

  if (!this->isOpen) {
    throw CurlError::New(env, "Multi handle is closed", CURLM_BAD_HANDLE);
  }

  if (info.Length() < 1) {
    throw CurlError::New(env, "Wrong number of arguments", CURLM_BAD_FUNCTION_ARGUMENT);
  }

  if (!info[0].IsObject() ||
      !info[0].As<Napi::Object>().InstanceOf(curl->EasyConstructor.Value())) {
    throw CurlError::New(env, "Argument must be an Easy instance", CURLM_BAD_FUNCTION_ARGUMENT);
  }

  Napi::Object obj = info[0].As<Napi::Object>();
  Easy* easy = Napi::ObjectWrap<Easy>::Unwrap(obj);

  if (!easy || !easy->isOpen) {
    throw CurlError::New(env, "Easy handle is closed or invalid", CURLM_BAD_EASY_HANDLE);
  }

  if (easy->isInsideMultiHandle) {
    throw CurlError::New(env, "Easy handle is already inside a multi handle", CURLM_ADDED_ALREADY);
  }

  NODE_LIBCURL_DEBUG_LOG(this, "Multi::AddHandle", "adding handle " + std::to_string(easy->id));

  // reset callback error in case it is set
  easy->callbackError.Reset();

  // Check comment on node_libcurl.cc
  LocaleGuard localeGuard;
  CURLMcode code = curl_multi_add_handle(this->mh, easy->ch);

  if (code != CURLM_OK) {
    throw CurlError::New(env, "Could not add easy handle to the multi handle.", code, true);
  }

  ++this->amountOfHandles;
  easy->isInsideMultiHandle = true;

  // Store a reference to the Easy handle to prevent GC
  this->easyHandleRefs[easy->ch] = Napi::Persistent(obj);

  return Napi::Number::New(env, static_cast<int>(code));
}

Napi::Value Multi::RemoveHandle(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  auto curl = env.GetInstanceData<Curl>();

  if (!this->isOpen) {
    throw CurlError::New(env, "Multi handle is closed", CURLM_BAD_HANDLE);
  }

  if (info.Length() < 1) {
    throw CurlError::New(env, "Wrong number of arguments", CURLM_BAD_FUNCTION_ARGUMENT);
  }

  if (!info[0].IsObject() ||
      !info[0].As<Napi::Object>().InstanceOf(curl->EasyConstructor.Value())) {
    throw CurlError::New(env, "Argument must be an Easy instance", CURLM_BAD_FUNCTION_ARGUMENT);
  }

  Napi::Object obj = info[0].As<Napi::Object>();
  Easy* easy = Napi::ObjectWrap<Easy>::Unwrap(obj);

  if (!easy || !easy->isOpen) {
    throw CurlError::New(env, "Easy handle is closed or invalid", CURLM_BAD_EASY_HANDLE);
  }

  NODE_LIBCURL_DEBUG_LOG(this, "Multi::RemoveHandle",
                         "removing handle " + std::to_string(easy->id));

  CURLMcode code = curl_multi_remove_handle(this->mh, easy->ch);

  if (code != CURLM_OK) {
    throw CurlError::New(env, "Could not remove easy handle from multi handle.", code, true);
  }

  --this->amountOfHandles;
  easy->isInsideMultiHandle = false;

  // Remove the Easy handle reference (allows GC)
  this->easyHandleRefs.erase(easy->ch);

  return Napi::Number::New(env, static_cast<int>(code));
}

Napi::Value Multi::Perform(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  auto curl = env.GetInstanceData<Curl>();

  if (!this->isOpen) {
    throw CurlError::New(env, "Multi handle is closed", CURLM_BAD_HANDLE);
  }

  if (info.Length() < 1) {
    throw CurlError::New(env, "Wrong number of arguments", CURLM_BAD_FUNCTION_ARGUMENT);
  }

  if (!info[0].IsObject() ||
      !info[0].As<Napi::Object>().InstanceOf(curl->EasyConstructor.Value())) {
    throw CurlError::New(env, "Argument must be an Easy instance", CURLM_BAD_FUNCTION_ARGUMENT);
  }

  Napi::Object obj = info[0].As<Napi::Object>();
  Easy* easy = Napi::ObjectWrap<Easy>::Unwrap(obj);

  if (!easy || !easy->isOpen) {
    throw CurlError::New(env, "Easy handle is closed or invalid", CURLM_BAD_EASY_HANDLE);
  }

  if (easy->isInsideMultiHandle) {
    throw CurlError::New(env, "Easy handle is already inside a multi handle", CURLM_ADDED_ALREADY);
  }

  NODE_LIBCURL_DEBUG_LOG(this, "Multi::Perform", "adding handle " + std::to_string(easy->id));

  // Create deferred promise
  auto deferred = Napi::Promise::Deferred::New(env);

  // reset callback error in case it is set
  easy->callbackError.Reset();

  // Check comment on node_libcurl.cc
  LocaleGuard localeGuard;
  CURLMcode code = curl_multi_add_handle(this->mh, easy->ch);

  if (code != CURLM_OK) {
    throw CurlError::New(env, "Could not add easy handle to the multi handle.", code, true);
  }

  ++this->amountOfHandles;
  easy->isInsideMultiHandle = true;

  // Store the deferred promise for this handle
  this->handlePromiseMap[easy->ch] = std::make_shared<Napi::Promise::Deferred>(std::move(deferred));

  // Store a reference to the Easy handle to prevent GC
  this->easyHandleRefs[easy->ch] = Napi::Persistent(obj);

  // Return the promise
  return this->handlePromiseMap[easy->ch]->Promise();
}

Napi::Value Multi::OnMessage(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (!info.Length()) {
    throw CurlError::New(env,
                         "You must specify the callback function. If you want to remove the "
                         "current one you can pass null.",
                         CURLM_BAD_FUNCTION_ARGUMENT);
  }

  Napi::Value arg = info[0];
  bool isNull = arg.IsNull();

  if (!arg.IsFunction() && !isNull) {
    throw CurlError::New(env,
                         "Argument must be a Function. If you want to remove the current one "
                         "you can pass null.",
                         CURLM_BAD_FUNCTION_ARGUMENT);
  }

  if (isNull) {
    this->cbOnMessage.Reset();
  } else {
    this->cbOnMessage = Napi::Persistent(arg.As<Napi::Function>());
  }

  return info.This();
}

Napi::Value Multi::GetCount(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (!this->isOpen) {
    throw CurlError::New(env, "Multi handle is closed", CURLM_BAD_HANDLE);
  }

  return Napi::Number::New(env, this->amountOfHandles);
}

Napi::Value Multi::Close(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (!this->isOpen) {
    throw CurlError::New(env, "Multi handle already closed.", CURLM_BAD_HANDLE);
  }

  NODE_LIBCURL_DEBUG_LOG(this, "Multi::Close", "");

  this->Dispose();

  return env.Undefined();
}

Napi::Value Multi::GetterId(const Napi::CallbackInfo& info) {
  return Napi::Number::New(info.Env(), this->id);
}

Napi::Value Multi::StrError(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsNumber()) {
    throw CurlError::New(env, "Argument must be an error code", CURLM_BAD_FUNCTION_ARGUMENT);
  }

  int32_t errorCode = info[0].As<Napi::Number>().Int32Value();
  const char* errorMsg = curl_multi_strerror(static_cast<CURLMcode>(errorCode));

  return Napi::String::New(env, errorMsg);
}

void Multi::ProcessMessages() {
  NODE_LIBCURL_DEBUG_LOG(this, "Multi::ProcessMessages", "isOpen: " + std::to_string(this->isOpen));
  if (!this->isOpen) return;

  int msgsLeft = 0;
  CURLMsg* msg = nullptr;

  while (this->isOpen && (msg = curl_multi_info_read(this->mh, &msgsLeft))) {
    NODE_LIBCURL_DEBUG_LOG(
        this, "Multi::ProcessMessages",
        "msg->msg: " + std::to_string(msg->msg) + " isOpen: " + std::to_string(this->isOpen));
    if (msg->msg == CURLMSG_DONE) {
      CURL* easy = msg->easy_handle;
      CURLcode result = msg->data.result;

      this->CallOnMessageCallback(easy, result);
    }
  }
}

void Multi::CallOnMessageCallback(CURL* easy, CURLcode handleCode) {
  if (!this->isOpen) return;

  Napi::Env env = Env();
  Napi::HandleScope scope(env);

  // From https://curl.haxx.se/libcurl/c/CURLINFO_PRIVATE.html
  // > Please note that for internal reasons, the value is returned as a char
  // pointer, although effectively being a 'void *'.
  char* ptr = nullptr;
  CURLcode code = curl_easy_getinfo(easy, CURLINFO_PRIVATE, &ptr);
  assert(code == CURLE_OK && "Error retrieving current handle instance.");

  assert(ptr != nullptr && "Invalid handle returned from CURLINFO_PRIVATE.");
  Easy* easyObj = reinterpret_cast<Easy*>(ptr);

  bool hasError = !easyObj->callbackError.IsEmpty();

  // Determine the final status code
  CURLcode statusCode = handleCode == CURLE_OK && hasError ? CURLE_ABORTED_BY_CALLBACK : handleCode;

  // Handle promise-based perform() if exists
  auto promiseIt = this->handlePromiseMap.find(easy);
  if (promiseIt != this->handlePromiseMap.end()) {
    NODE_LIBCURL_DEBUG_LOG(
        this, "Multi::CallOnMessageCallback",
        "resolving/rejecting promise for handle, statusCode: " + std::to_string(statusCode));

    auto deferred = promiseIt->second;

    if (statusCode != CURLE_OK || hasError) {
      // Reject the promise with Error
      if (hasError) {
        Napi::Error error =
            CurlError::New(env, "Request was aborted by a callback", CURLE_ABORTED_BY_CALLBACK);

        auto errorValue = error.Value();
        errorValue.Set("cause", easyObj->callbackError.Value());
        deferred->Reject(errorValue);
      } else {
        auto error = CurlError::New(env, "Request failed", statusCode, true);
        deferred->Reject(error.Value());
      }
    } else {
      // Resolve the promise with the Easy instance
      deferred->Resolve(easyObj->Value());
    }

    // Clean up the promise reference
    this->handlePromiseMap.erase(promiseIt);

    // Remove the Easy handle reference (allows GC)
    this->easyHandleRefs.erase(easy);

    // Mark handle as no longer in multi
    easyObj->isInsideMultiHandle = false;
    --this->amountOfHandles;

    // Remove from multi handle
    curl_multi_remove_handle(this->mh, easy);

    return;
  }

  Napi::Function callback = this->cbOnMessage.Value();

  // Create arguments: error (null or Error object), Easy instance
  Napi::Value error = env.Null();
  Napi::Number errorCode = Napi::Number::New(env, static_cast<int32_t>(statusCode));

  if (statusCode != CURLE_OK || hasError) {
    error = hasError ? easyObj->callbackError.Value()
                     : CurlError::New(env, "Request failed", statusCode, true).Value();
  }

  NODE_LIBCURL_DEBUG_LOG(this, "Multi::CallOnMessageCallback",
                         "calling onMessage callback, statusCode: " + std::to_string(statusCode));

  try {
    callback.Call(this->Value(), {error, easyObj->Value(), errorCode});

  } catch (const Napi::Error&) {
    // ignore any and all errors
  }

  // Some re-entrant calls may have closed the Multi handle, it is not safe to continue
  if (!this->isOpen) return;
}

// CurlPoller callback - socket event occurred (called on main thread via TSFN)
void Multi::OnSocketEvent(curl_socket_t sockfd, int events) {
  if (!this->isOpen) return;

  NODE_LIBCURL_DEBUG_LOG(
      this, "Multi::OnSocketEvent",
      "socket: " + std::to_string(sockfd) + " events: " + std::to_string(events));

  int flags = 0;
  if (events & POLL_EVENT_READABLE) flags |= CURL_CSELECT_IN;
  if (events & POLL_EVENT_WRITABLE) flags |= CURL_CSELECT_OUT;

  // Check comment on node_libcurl.cc
  LocaleGuard localeGuard;

  // Before version 7.20.0: If you receive CURLM_CALL_MULTI_PERFORM, this
  // basically means that you should call curl_multi_socket_action again
  // before you wait for more actions on libcurl's sockets.
  CURLMcode code;
  do {
    code = curl_multi_socket_action(this->mh, sockfd, flags, &this->runningHandles);
  } while (code == CURLM_CALL_MULTI_PERFORM);

  assert(code == CURLM_OK && "curl_multi_socket_action failed");

  // When notifications are enabled, libcurl will call our NotifyCallback when needed
  if (!this->useNotificationsApi) {
    this->ProcessMessages();
  }
}

// CurlPoller callback - timeout expired (called on main thread via TSFN)
void Multi::OnTimeoutEvent() {
  if (!this->isOpen) return;

  NODE_LIBCURL_DEBUG_LOG(this, "Multi::OnTimeoutEvent", "");

  // Check comment on node_libcurl.cc
  LocaleGuard localeGuard;
  CURLMcode code =
      curl_multi_socket_action(this->mh, CURL_SOCKET_TIMEOUT, 0, &this->runningHandles);

  assert((CURLM_OK == code || true) &&
         "Calling curl_multi_socket_action from within Multi::OnTimeoutEvent failed.");

  // When notifications are enabled, libcurl will call our NotifyCallback when needed
  if (!this->useNotificationsApi) {
    this->ProcessMessages();
  }
}

// libcurl callback implementations
int Multi::HandleSocket(CURL* easy, curl_socket_t s, int action, void* userp, void* socketp) {
  Multi* obj = static_cast<Multi*>(userp);

  NODE_LIBCURL_DEBUG_LOG(obj, "Multi::HandleSocket",
                         "socket: " + std::to_string(s) + " action: " + std::to_string(action));

  if (!obj->poller_) {
    return -1;
  }

  if (action == CURL_POLL_IN || action == CURL_POLL_OUT || action == CURL_POLL_INOUT ||
      action == CURL_POLL_NONE) {
    // Convert action to poll events
    int events = 0;
    if (action != CURL_POLL_OUT) events |= POLL_EVENT_READABLE;
    if (action != CURL_POLL_IN) events |= POLL_EVENT_WRITABLE;

    if (socketp) {
      // Modify existing socket
      obj->poller_->ModifySocket(s, events);
    } else {
      // Add new socket
      obj->poller_->AddSocket(s, events);
      curl_multi_assign(obj->mh, s, reinterpret_cast<void*>(1));  // Mark as registered
    }

    return 0;
  }

  if (action == CURL_POLL_REMOVE) {
    if (socketp) {
      obj->poller_->RemoveSocket(s);
      curl_multi_assign(obj->mh, s, nullptr);
    }
    return 0;
  }

  // see this: https://github.com/curl/curl/issues/14860#issuecomment-2452663239
  return -1;
}

// This function will be called when the timeout value changes from libcurl.
int Multi::HandleTimeout(CURLM* multi,
                         long timeoutMs,  // NOLINT(runtime/int)
                         void* userp) {
  Multi* obj = static_cast<Multi*>(userp);

  NODE_LIBCURL_DEBUG_LOG(obj, "Multi::HandleTimeout",
                         "timeout: " + std::to_string(timeoutMs) + "ms");

  if (obj->timerClosed || !obj->poller_) {
    return 0;
  }

  if (timeoutMs < 0) {
    obj->poller_->SetTimeout(-1);  // Disable timeout
    return 0;
  }

  obj->poller_->SetTimeout(timeoutMs);
  return 0;
}

int Multi::CbPushFunction(CURL* parent, CURL* child, size_t numberOfHeaders,
                          struct curl_pushheaders* headers, void* userPtr) {
  // Note:
  //  We cannot throw js errors inside this callback
  //   as there is no way to signal libcurl to mark this request as failed
  //   and stop calling this callback for this connection (in case there are more pushes)
  //   this means that we must not rethrow errors we catch from user land.
  //   doing so would cause the whole library code to fall apart as it would not be safe to
  //   use other v8 objects.
  int returnValue = CURL_PUSH_DENY;

  Multi* obj = static_cast<Multi*>(userPtr);
  assert(obj);
  assert(obj->isOpen);

  auto it = obj->callbacks.find(CURLMOPT_PUSHFUNCTION);
  assert(it != obj->callbacks.end() && "PUSHFUNCTION callback not set.");

  if (it->second.IsEmpty()) {
    return CURL_PUSH_DENY;
  }

  char* parentEasyPtr = nullptr;
  CURLcode code = curl_easy_getinfo(parent, CURLINFO_PRIVATE, &parentEasyPtr);
  assert(code == CURLE_OK &&
         "It was not possible to retrieve the current Easy instance from the libcurl easy handle");
  assert(parentEasyPtr != nullptr && "Invalid handle returned from CURLINFO_PRIVATE.");

  Easy* parentEasyObj = reinterpret_cast<Easy*>(parentEasyPtr);
  assert(parentEasyObj->isOpen &&
         "The Easy instance doing the current request was closed prematurely");

  Napi::Env env = it->second.Env();
  Napi::HandleScope scope(env);
  auto curl = env.GetInstanceData<Curl>();

  try {
    Napi::Object parentEasyJsObj = parentEasyObj->Value();
    Napi::Object childEasyJsObj = Easy::FromCURLHandle(env, child);

    auto headersExternal = Napi::External<curl_pushheaders>::New(env, headers);
    headersExternal.TypeTag(&HTTP2_PUSH_FRAME_HEADERS_TYPE_TAG);

    auto http2PushFrameJsObj = curl->Http2PushFrameHeadersConstructor.New({
        headersExternal,
        Napi::Number::New(env, numberOfHeaders),
    });

    Napi::Function callback = it->second.Value();
    // TODO(jonathan, migration): capture this when perform is called or similar (either on Easy or
    // Multi)
    Napi::AsyncContext asyncContext(env, "Multi::CbPushFunction");

    Napi::Value returnValueCallback = callback.MakeCallback(obj->Value(),
                                                            {
                                                                parentEasyJsObj,
                                                                childEasyJsObj,
                                                                http2PushFrameJsObj,
                                                            },
                                                            asyncContext);

    if (!returnValueCallback.IsEmpty() && returnValueCallback.IsNumber()) {
      returnValue = returnValueCallback.As<Napi::Number>().Int32Value();
    }
  } catch (const Napi::Error&) {
    // See the note at the top of this function, we must not rethrow this error.
    // Show some Debug message?
    return returnValue;
  }

  return returnValue;
}

#if NODE_LIBCURL_VER_GE(8, 17, 0)
void Multi::NotifyCallback(CURLM* multi, unsigned int notification, CURL* easy, void* notifyp) {
  Multi* obj = static_cast<Multi*>(notifyp);
  assert(obj && "Multi::NotifyCallback - Invalid Multi instance");

  NODE_LIBCURL_DEBUG_LOG(obj, "Multi::NotifyCallback",
                         "notification: " + std::to_string(notification));

  if (notification == CURLMNOTIFY_INFO_READ) {
    obj->ProcessMessages();
  }
}
#endif

}  // namespace NodeLibcurl
