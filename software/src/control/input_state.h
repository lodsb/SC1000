/*
 * Copyright (C) 2024-2026 Niklas Klügel <lodsb@lodsb.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */


#pragma once

namespace sc {
namespace control {

//
// InputState - Encapsulates global input modifier state
//
// Replaces the static ActionState members. Owned by Sc1000.
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
