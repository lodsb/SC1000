#pragma once

namespace sc {
namespace control {

//
// InputState - Encapsulates global input modifier state
//
// Replaces the static ActionState members. Owned by sc1000.
// Only accessed from input thread, so no synchronization needed.
//

class InputState {
public:
    // Shift key state
    bool is_shifted() const { return shifted_; }
    void set_shifted(bool v) { shifted_ = v; }

    // Pitch mode: 0=off, 1=beat deck, 2=scratch deck
    int pitch_mode() const { return pitch_mode_; }
    void set_pitch_mode(int mode) { pitch_mode_ = mode; }

private:
    bool shifted_ = false;
    int pitch_mode_ = 0;
};

} // namespace control
} // namespace sc
