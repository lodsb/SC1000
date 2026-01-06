#include "mapping_registry.h"

namespace sc {
namespace control {

void MappingRegistry::add(mapping m) {
    size_t idx = mappings_.size();
    mappings_.push_back(m);
    index_mapping(idx);
}

void MappingRegistry::clear() {
    mappings_.clear();
    gpio_index_.clear();
    midi_index_.clear();
}

mapping* MappingRegistry::find_gpio(uint8_t port, uint8_t pin, EventType edge) {
    GpioKey key{port, pin, edge};
    auto it = gpio_index_.find(key);
    if (it != gpio_index_.end()) {
        return &mappings_[it->second];
    }
    return nullptr;
}

mapping* MappingRegistry::find_midi(const MidiCommand& cmd, EventType edge) {
    // Normalize note-on with velocity 0 to note-off for lookup
    MidiCommand normalized = cmd;
    normalized.normalize();

    MidiKey key{normalized, edge};
    auto it = midi_index_.find(key);
    if (it != midi_index_.end()) {
        return &mappings_[it->second];
    }
    return nullptr;
}

mapping* MappingRegistry::at(size_t index) {
    return index < mappings_.size() ? &mappings_[index] : nullptr;
}

const mapping* MappingRegistry::at(size_t index) const {
    return index < mappings_.size() ? &mappings_[index] : nullptr;
}

void MappingRegistry::index_mapping(size_t idx) {
    const mapping& m = mappings_[idx];

    if (m.type == IO) {
        GpioKey key{m.gpio_port, m.pin, m.edge_type};
        gpio_index_[key] = idx;
    } else if (m.type == MIDI) {
        MidiCommand cmd;
        cmd.status = m.midi_command_bytes[0];
        cmd.data1 = m.midi_command_bytes[1];
        cmd.data2 = m.midi_command_bytes[2];

        MidiKey key{cmd, m.edge_type};
        midi_index_[key] = idx;
    }
}

} // namespace control
} // namespace sc
