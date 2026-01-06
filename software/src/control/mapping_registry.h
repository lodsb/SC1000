#pragma once

#include "../core/sc_input.h"
#include "../input/midi_command.h"
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace sc {
namespace control {

//
// ButtonState - Runtime state for a mapped button
//
// Separated from Mapping (which is configuration only).
// Stored in a map keyed by mapping index.
//
struct ButtonState {
    int debounce = 0;
    bool shifted_at_press = false;
};

//
// GpioKey - Hash key for GPIO mapping lookup
//
struct GpioKey {
    uint8_t port;
    uint8_t pin;
    EventType edge;

    bool operator==(const GpioKey& o) const {
        return port == o.port && pin == o.pin && edge == o.edge;
    }
};

struct GpioKeyHash {
    size_t operator()(const GpioKey& k) const {
        return (static_cast<size_t>(k.port) << 16) |
               (static_cast<size_t>(k.pin) << 8) |
               static_cast<size_t>(k.edge);
    }
};

//
// MidiKey - Hash key for MIDI mapping lookup
//
struct MidiKey {
    MidiCommand cmd;
    EventType edge;

    bool operator==(const MidiKey& o) const {
        return cmd == o.cmd && edge == o.edge;
    }
};

struct MidiKeyHash {
    size_t operator()(const MidiKey& k) const {
        return MidiCommandHash{}(k.cmd) ^ (static_cast<size_t>(k.edge) << 24);
    }
};

//
// MappingRegistry - Indexed storage for input mappings
//
// Provides O(1) lookup by GPIO (port, pin, edge) or MIDI (command, edge).
// Stores mappings in a vector with hash indices for fast lookup.
//
class MappingRegistry {
public:
    // Add a mapping (updates indices automatically)
    void add(mapping m);

    // Clear all mappings
    void clear();

    // O(1) lookup - returns nullptr if not found
    mapping* find_gpio(uint8_t port, uint8_t pin, EventType edge);
    mapping* find_midi(const MidiCommand& cmd, EventType edge);

    // Get mapping by index (for ButtonState association)
    mapping* at(size_t index);
    const mapping* at(size_t index) const;

    // Iteration (for init, debug, serialization)
    std::vector<mapping>& all() { return mappings_; }
    const std::vector<mapping>& all() const { return mappings_; }

    // Count
    size_t size() const { return mappings_.size(); }
    bool empty() const { return mappings_.empty(); }

private:
    std::vector<mapping> mappings_;
    std::unordered_map<GpioKey, size_t, GpioKeyHash> gpio_index_;
    std::unordered_map<MidiKey, size_t, MidiKeyHash> midi_index_;

    void index_mapping(size_t idx);
};

} // namespace control
} // namespace sc
