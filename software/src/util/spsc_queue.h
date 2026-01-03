/*
 * SPSC Queue wrapper
 *
 * Uses moodycamel::ReaderWriterQueue - a battle-tested lock-free
 * single-producer single-consumer queue.
 *
 * See deps/readerwriterqueue/ for the implementation.
 */

#pragma once

#include <readerwriterqueue/readerwriterqueue.h>

namespace sc {

// Alias for convenience
template<typename T, size_t MaxBlockSize = 512>
using SPSCQueue = moodycamel::ReaderWriterQueue<T, MaxBlockSize>;

} // namespace sc
