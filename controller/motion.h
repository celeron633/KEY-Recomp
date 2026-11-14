// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace key::controls {
using Vector = std::array<float, 3>;
inline bool finite(const Vector& v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

// SDL uses m/s^2 and rad/s. Aurora's override uses KPAD axes and g.
// Rotate a normally held gamepad into a sideways remote (D-pad at the left).
inline Vector sideways_acceleration(Vector a, float gain, bool invert) {
    if (!finite(a)) return {0, -1, 0};
    constexpr float gravity = 9.80665f;
    const float angle = std::atan2(a[0], a[1]) * gain * (invert ? -1.f : 1.f);
    const float radius = std::hypot(a[0], a[1]) / gravity;
    return {-a[2] / gravity, -radius * std::cos(angle), -radius * std::sin(angle)};
}
inline Vector keyboard_tilt(float angle) { return {0, -std::cos(angle), -std::sin(angle)}; }

struct Motion {
    float x = 0, y = 0;
    Vector bias{}, sum{}, acceleration{0, 9.80665f, 0};
    float calibration_seconds = 0;
    unsigned calibration_samples = 0;
    bool calibrating = false, calibrated = false;

    void recenter() { x = y = 0; }
    void calibrate() {
        sum = {}; calibration_seconds = 0; calibration_samples = 0; calibrating = true;
    }
    void sample(Vector gyro, Vector accel, float dt, float sensitivity, float deadzone,
                bool invert_x, bool invert_y, bool integrate) {
        if (!finite(gyro) || !finite(accel) || !std::isfinite(dt) || dt <= 0 || dt > .1f) return;
        const float blend = 1 - std::exp(-dt / .035f);
        for (unsigned i = 0; i < 3; ++i) acceleration[i] += blend * (accel[i] - acceleration[i]);
        if (calibrating) {
            const float gravity = std::sqrt(accel[0]*accel[0]+accel[1]*accel[1]+accel[2]*accel[2]);
            const float speed = std::sqrt(gyro[0]*gyro[0]+gyro[1]*gyro[1]+gyro[2]*gyro[2]);
            if (speed > .15f || std::abs(gravity - 9.80665f) > .7f) {
                sum = {}; calibration_seconds = 0; calibration_samples = 0;
                return;
            }
            for (unsigned i = 0; i < 3; ++i) sum[i] += gyro[i] * dt;
            calibration_seconds += dt; ++calibration_samples;
            if (calibration_seconds >= 2.f && calibration_samples >= 50) {
                for (unsigned i = 0; i < 3; ++i) bias[i] = sum[i] / calibration_seconds;
                calibrated = true; calibrating = false; recenter();
            }
            return;
        }
        if (!integrate) return;
        const auto rate = [&](unsigned axis) {
            const float v = gyro[axis] - bias[axis];
            return std::abs(v) <= deadzone ? 0.f : v;
        };
        // Match the owner's observed DS4 aiming directions. The user inversion
        // switches remain relative to this baseline; mouse/stick axes are separate.
        x = std::clamp(x + rate(1) * sensitivity * dt * (invert_x ? -1.f : 1.f), -1.f, 1.f);
        y = std::clamp(y + rate(0) * sensitivity * dt * (invert_y ? -1.f : 1.f), -1.f, 1.f);
    }
};
}
