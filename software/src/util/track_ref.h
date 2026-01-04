/*
 * RAII wrapper for track reference counting
 *
 * Provides automatic reference counting for track objects, preventing
 * leaks and use-after-free bugs. Move-only to enforce single ownership
 * semantics.
 */

#pragma once

#include "../player/track.h"

namespace sc {

class TrackRef {
    struct track* ptr = nullptr;

public:
    // Construct empty reference
    TrackRef() = default;

    // Take ownership of a track (assumes refcount already incremented)
    explicit TrackRef(struct track* t) : ptr(t) {}

    // No copy - tracks have reference semantics
    TrackRef(const TrackRef&) = delete;
    TrackRef& operator=(const TrackRef&) = delete;

    // Move constructor
    TrackRef(TrackRef&& other) noexcept : ptr(other.ptr) {
        other.ptr = nullptr;
    }

    // Move assignment
    TrackRef& operator=(TrackRef&& other) noexcept {
        if (this != &other) {
            if (ptr) {
                track_release(ptr);
            }
            ptr = other.ptr;
            other.ptr = nullptr;
        }
        return *this;
    }

    // Destructor releases the track
    ~TrackRef() {
        if (ptr) {
            track_release(ptr);
        }
    }

    // Access the underlying track
    struct track* get() const { return ptr; }

    // Arrow operator for convenience
    struct track* operator->() const { return ptr; }

    // Dereference operator
    struct track& operator*() const { return *ptr; }

    // Boolean conversion for null checks
    explicit operator bool() const { return ptr != nullptr; }

    // Release ownership without decrementing refcount
    // Used when transferring to C code that takes ownership
    struct track* release() {
        struct track* t = ptr;
        ptr = nullptr;
        return t;
    }

    // Reset with a new track (releases old one if any)
    void reset(struct track* t = nullptr) {
        if (ptr) {
            track_release(ptr);
        }
        ptr = t;
    }
};

} // namespace sc
