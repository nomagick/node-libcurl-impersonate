/**
 * Copyright (c) Jonathan Cardoso Machado. All Rights Reserved.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#ifndef NODELIBCURL_MACROS_H
#define NODELIBCURL_MACROS_H

// inspired from the LUA bindings.
#define NODE_LIBCURL_MAKE_VERSION(MAJ, MIN, PAT) ((MAJ << 16) + (MIN << 8) + PAT)
#define NODE_LIBCURL_VER_GE(MAJ, MIN, PAT) \
  (LIBCURL_VERSION_NUM >= NODE_LIBCURL_MAKE_VERSION(MAJ, MIN, PAT))
#define NODE_LIBCURL_VER_LE(MAJ, MIN, PAT) \
  (LIBCURL_VERSION_NUM <= NODE_LIBCURL_MAKE_VERSION(MAJ, MIN, PAT))

// Debug logging macros with zero runtime overhead when disabled
#ifdef NODE_LIBCURL_DEBUG
#include <iostream>
#include <string>
#include <thread>
#define NODE_LIBCURL_DEBUG_LOG(obj, message, extra)                   \
  do {                                                                \
    std::cout << "[thread: " << std::this_thread::get_id()            \
              << "] [env: " << static_cast<napi_env>((obj)->Env())    \
              << "] [id: " << (obj)->GetDebugId() << "] " << message; \
    std::string extra_str(extra);                                     \
    if (!extra_str.empty()) {                                         \
      std::cout << " - " << extra_str;                                \
    }                                                                 \
    std::cout << std::endl;                                           \
  } while (0)

#define NODE_LIBCURL_DEBUG_LOG_STATIC(env, message)                                                \
  do {                                                                                             \
    std::cout << "[thread: " << std::this_thread::get_id() << "] [env: " << env << "] " << message \
              << std::endl;                                                                        \
  } while (0)
#else
#define NODE_LIBCURL_DEBUG_LOG(obj, message, extra) \
  do {                                              \
  } while (0)
#define NODE_LIBCURL_DEBUG_LOG_STATIC(env, message) \
  do {                                              \
  } while (0)
#endif

#endif
