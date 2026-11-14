// SPDX-License-Identifier: GPL-3.0-only
#include "controls.h"
#include "motion.h"
#include "key/input.h"
#include <SDL3/SDL.h>
#include <aurora/wpad.h>
#include <aurora/input.hpp>
#include <dolphin/pad.h>
#include <imgui.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

namespace key::controls {
namespace {
struct Binding { const char* label; uint16_t button; uint32_t extended; int native; };
constexpr std::array<Binding, 7> defaults{{
    {"Wii 2 / Jump / Confirm", PAD_BUTTON_Y, 0, SDL_GAMEPAD_BUTTON_SOUTH},
    {"Wii 1 / Yarn / Back", PAD_BUTTON_X, 0, SDL_GAMEPAD_BUTTON_WEST},
    {"Wii A", PAD_BUTTON_A, 0, SDL_GAMEPAD_BUTTON_EAST},
    {"Wii B", PAD_BUTTON_B, 0, SDL_GAMEPAD_BUTTON_NORTH},
    {"Wii + / Pause", PAD_BUTTON_START, 0, SDL_GAMEPAD_BUTTON_START},
    {"Wii -", 0, PAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_BACK},
    {"Wii Home", 0, PAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_GUIDE}
}};
struct Settings {
    bool fps = false, tilt = true, invert_tilt = false, invert_x = false, invert_y = false;
    int pointer = 1; // 0: mouse, 1: gyro, 2: right stick, 3: disabled
    float sensitivity = 1.5f, tilt_gain = 1.f, deadzone = .008f, stick_deadzone = .2f;
    std::array<Binding, 7> bindings = defaults;
    int recenter = SDL_GAMEPAD_BUTTON_RIGHT_STICK;
} settings;
std::filesystem::path config_path;
std::string message;
Motion motion;
std::atomic<bool> opened = false, focused = true;
std::mutex event_mutex;
struct KeyEvent { SDL_Scancode code; bool down, repeat; };
std::vector<KeyEvent> events;
std::array<bool, SDL_SCANCODE_COUNT> held{};
uint16_t keyboard = 0;
bool initialized = false, gyro_ok = false, accel_ok = false, recenter_down = false;
bool pad_armed = false;
aurora::input::LayerId input_layer = 0;
SDL_JoystickID device = 0;
int waiting_binding = -1;
bool capture_released = false;
auto last_update = std::chrono::steady_clock::time_point{};
auto fps_start = std::chrono::steady_clock::time_point{};
uint64_t fps_frames = 0;
float displayed_fps = 0;

void save() {
    try {
        const auto temporary = config_path.string() + ".tmp";
        std::ofstream out(temporary, std::ios::trunc);
        out << "version 1\n" << "fps " << settings.fps << "\ntilt " << settings.tilt
            << "\ninvert_tilt " << settings.invert_tilt << "\ninvert_x " << settings.invert_x
            << "\ninvert_y " << settings.invert_y << "\npointer " << settings.pointer
            << "\nsensitivity " << settings.sensitivity << "\ntilt_gain " << settings.tilt_gain
            << "\ndeadzone " << settings.deadzone << "\nstick_deadzone " << settings.stick_deadzone
            << "\nrecenter " << settings.recenter << '\n';
        for (unsigned i = 0; i < settings.bindings.size(); ++i) out << "button" << i << ' ' << settings.bindings[i].native << '\n';
        out.close();
        if (!out) throw std::runtime_error("Cannot write settings file");
        // SDL replaces the destination atomically on Windows without deleting the old file first.
        if (!SDL_RenamePath(temporary.c_str(), config_path.string().c_str())) throw std::runtime_error(SDL_GetError());
        message = "Settings saved.";
    } catch (const std::exception& e) {
        message = std::string("Settings save failed: ") + e.what();
        std::fprintf(stderr, "INPUT %s\n", message.c_str());
    }
}
void load() {
    std::ifstream in(config_path);
    if (!in) return;
    Settings candidate;
    std::string line, name; float value; bool version = false, valid = true;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream record(line);
        std::string trailing;
        if (!(record >> name >> value) || (record >> trailing)) { valid = false; break; }
        if (!std::isfinite(value)) { valid = false; break; }
        auto boolean = [&](bool& field) { if (value != 0 && value != 1) valid = false; else field = value != 0; };
        auto scalar = [&](float& field, float low, float high) { if (value < low || value > high) valid = false; else field = value; };
        auto integer = [&](int& field, int low, int high) { if (value != std::floor(value) || value < low || value > high) valid = false; else field = static_cast<int>(value); };
        if (name == "version") { version = value == 1; if (!version) valid = false; }
        else if (name == "fps") boolean(candidate.fps);
        else if (name == "tilt") boolean(candidate.tilt);
        else if (name == "invert_tilt") boolean(candidate.invert_tilt);
        else if (name == "invert_x") boolean(candidate.invert_x);
        else if (name == "invert_y") boolean(candidate.invert_y);
        else if (name == "pointer") integer(candidate.pointer, 0, 3);
        else if (name == "sensitivity") scalar(candidate.sensitivity, .2f, 5.f);
        else if (name == "tilt_gain") scalar(candidate.tilt_gain, .25f, 3.f);
        else if (name == "deadzone") scalar(candidate.deadzone, 0.f, .05f);
        else if (name == "stick_deadzone") scalar(candidate.stick_deadzone, .05f, .8f);
        else if (name == "recenter") integer(candidate.recenter, 0, SDL_GAMEPAD_BUTTON_COUNT - 1);
        else {
            bool known = false;
            for (unsigned i = 0; i < candidate.bindings.size(); ++i) if (name == "button" + std::to_string(i)) {
                integer(candidate.bindings[i].native, 0, SDL_GAMEPAD_BUTTON_COUNT - 1); known = true;
            }
            if (!known) valid = false;
        }
    }
    if (valid && version && in.eof()) settings = candidate;
    else { message = "Invalid settings file; using defaults. Save to replace it."; std::fprintf(stderr, "INPUT %s\n", message.c_str()); }
}
bool SDLCALL watch(void*, SDL_Event* e) {
    if (e->type == SDL_EVENT_WINDOW_FOCUS_LOST) focused = false;
    if (e->type == SDL_EVENT_WINDOW_FOCUS_GAINED) focused = true;
    if (e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_KEY_UP) {
        std::lock_guard lock(event_mutex);
        if (events.size() < 1024) events.push_back({e->key.scancode, e->type == SDL_EVENT_KEY_DOWN, e->key.repeat});
    }
    return true;
}
void set_open(bool value) {
    opened = value; waiting_binding = -1; held.fill(false); keyboard = 0;
    pad_armed = false;
    input::release_keyboard();
    PADBlockInput(captured());
}
SDL_Gamepad* gamepad() {
    const int index = PADGetIndexForPort(0);
    return index < 0 ? nullptr : PADGetSDLGamepadForIndex(index);
}
const char* button_name(int button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return "Cross / South";
    case SDL_GAMEPAD_BUTTON_WEST: return "Square / West";
    case SDL_GAMEPAD_BUTTON_EAST: return "Circle / East";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Triangle / North";
    case SDL_GAMEPAD_BUTTON_START: return "Options / Start";
    case SDL_GAMEPAD_BUTTON_BACK: return "Share / Back";
    case SDL_GAMEPAD_BUTTON_GUIDE: return "PS / Guide";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R3 / Right stick";
    default: { const auto* name = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(button)); return name ? name : "Unknown"; }
    }
}
float axis(SDL_Gamepad* pad, SDL_GamepadAxis a) {
    const float value = SDL_GetGamepadAxis(pad, a) / 32768.f;
    return std::abs(value) <= settings.stick_deadzone ? 0.f : std::copysign((std::abs(value)-settings.stick_deadzone)/(1-settings.stick_deadzone), value);
}
}

bool captured() { return opened || !focused; }
bool menu_open() { return opened; }
void set_keyboard(uint16_t buttons) { keyboard = captured() ? 0 : buttons; }
void initialize(const std::string& private_root) {
    settings = {}; motion = {}; message.clear(); opened = false; focused = true;
    device = 0; keyboard = 0; held.fill(false); last_update = {}; fps_start = {}; fps_frames = 0;
    config_path = std::filesystem::path(private_root) / "user/controls.ini";
    std::filesystem::create_directories(config_path.parent_path());
    load();
    // Virtual PAD producers are additive. Capture the physical P1 source below
    // ImGui so Aurora's default GC mapping cannot also press unrelated Wii buttons.
    input_layer = aurora::input::register_layer({
        .label = "key.controls", .priority = 50,
        .capturesSource = [](const aurora::input::InputSource& source, void*) {
            return source.kind == aurora::input::InputSource::Kind::Keyboard ||
                (source.kind == aurora::input::InputSource::Kind::Controller && device &&
                 aurora::input::gamepad_for_source(source.id) == device);
        },
        .pointerMode = [](void*) { return opened ? aurora::input::PointerMode::Visible : aurora::input::PointerMode::None; }});
    if (!SDL_AddEventWatch(watch, nullptr)) throw std::runtime_error("Cannot watch settings hotkeys");
    initialized = true;
}
void shutdown() {
    if (!initialized) return;
    SDL_RemoveEventWatch(watch, nullptr);
    aurora::input::unregister_layer(input_layer);
    PADBlockInput(false); PADClearVirtualStatus(0);
    { std::lock_guard lock(event_mutex); events.clear(); }
    initialized = false;
}
void update() {
    if (!initialized) return;
    std::vector<KeyEvent> pending;
    { std::lock_guard lock(event_mutex); pending.swap(events); }
    for (const auto& e : pending) {
        if (e.code <= SDL_SCANCODE_UNKNOWN || e.code >= SDL_SCANCODE_COUNT) continue;
        held[e.code] = e.down;
        if (!e.down || e.repeat || !focused) continue;
        if (e.code == SDL_SCANCODE_F1) set_open(!opened);
        if (e.code == SDL_SCANCODE_F2) { settings.fps = !settings.fps; save(); }
        if (e.code == SDL_SCANCODE_ESCAPE && opened) set_open(false);
        if (e.code == SDL_SCANCODE_R) motion.recenter();
    }
    if (!focused) { held.fill(false); keyboard = 0; motion.calibrating = false; pad_armed = false; }
    PADBlockInput(captured());
    const auto now = std::chrono::steady_clock::now();
    const float dt = last_update.time_since_epoch().count() ? std::chrono::duration<float>(now - last_update).count() : 0;
    last_update = now;
    auto* pad = gamepad();
    const auto id = pad ? SDL_GetGamepadID(pad) : 0;
    if (id != device) {
        device = id; motion = {}; gyro_ok = accel_ok = false; recenter_down = false; waiting_binding = -1; pad_armed = false;
        if (pad) {
            gyro_ok = SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO) && SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, true);
            accel_ok = SDL_GamepadHasSensor(pad, SDL_SENSOR_ACCEL) && SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_ACCEL, true);
            std::fprintf(stderr, "INPUT selected gamepad: %s gyro=%d accelerometer=%d\n", SDL_GetGamepadName(pad), gyro_ok, accel_ok);
        }
    }
    Vector gyro{}, acceleration{0, 9.80665f, 0};
    const bool gyro_read = pad && gyro_ok && SDL_GetGamepadSensorData(pad, SDL_SENSOR_GYRO, gyro.data(), 3) && finite(gyro);
    const bool accel_read = pad && accel_ok && SDL_GetGamepadSensorData(pad, SDL_SENSOR_ACCEL, acceleration.data(), 3) && finite(acceleration);
    if (focused) motion.sample(gyro_read ? gyro : Vector{}, acceleration, dt, settings.sensitivity, settings.deadzone,
                               settings.invert_x, settings.invert_y, gyro_read && settings.pointer == 1);
    if ((!gyro_read || !accel_read) && motion.calibrating) { motion.calibrating = false; message = "Calibration needs working gyro and accelerometer data."; }
    const bool center = pad && SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(settings.recenter));
    if (center && !recenter_down && !captured()) motion.recenter();
    recenter_down = center;
    if (pad && !captured() && !pad_armed) {
        bool neutral = axis(pad, SDL_GAMEPAD_AXIS_LEFTX) == 0 && axis(pad, SDL_GAMEPAD_AXIS_LEFTY) == 0 &&
                       axis(pad, SDL_GAMEPAD_AXIS_RIGHTX) == 0 && axis(pad, SDL_GAMEPAD_AXIS_RIGHTY) == 0;
        for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) neutral &= !SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(i));
        pad_armed = neutral;
    }
    if (pad && opened && waiting_binding >= 0) {
        int pressed = -1;
        for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) if (SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(i))) { pressed = i; break; }
        if (pressed < 0) capture_released = true;
        else if (capture_released) {
            int& target = waiting_binding == 7 ? settings.recenter : settings.bindings[waiting_binding].native;
            const int previous = target;
            for (unsigned i = 0; i < settings.bindings.size(); ++i)
                if (int(i) != waiting_binding && settings.bindings[i].native == pressed) settings.bindings[i].native = previous;
            if (waiting_binding != 7 && settings.recenter == pressed) settings.recenter = previous;
            target = pressed;
            waiting_binding = -1;
        }
    }
    PADStatus status{}; status.err = PAD_ERR_NONE;
    if (!captured()) {
        status.button = keyboard;
        if (pad && pad_armed) {
            for (const auto& b : settings.bindings) if (SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(b.native))) { status.button |= b.button; status.extButton |= b.extended; }
            const float x = axis(pad, SDL_GAMEPAD_AXIS_LEFTX), y = axis(pad, SDL_GAMEPAD_AXIS_LEFTY);
            if (x < 0 || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) status.button |= PAD_BUTTON_UP;
            if (x > 0 || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) status.button |= PAD_BUTTON_DOWN;
            if (y < 0 || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) status.button |= PAD_BUTTON_RIGHT;
            if (y > 0 || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) status.button |= PAD_BUTTON_LEFT;
        }
    }
    PADSetVirtualStatus(0, &status);
    Vector tilt{0, -1, 0};
    if (!captured()) {
        if (settings.tilt && accel_read) tilt = sideways_acceleration(motion.acceleration, settings.tilt_gain, settings.invert_tilt);
        const int direction = int(held[SDL_SCANCODE_E]) - int(held[SDL_SCANCODE_Q]);
        if (direction) tilt = keyboard_tilt(direction * .65f * (settings.invert_tilt ? -1.f : 1.f));
    }
    const Vec remote{tilt[0], tilt[1], tilt[2]};
    aurora_wpad_set_acceleration(0, &remote, nullptr);
    if (settings.pointer == 0 && !captured()) aurora_wpad_set_pointer(0, nullptr);
    else {
        if (settings.pointer == 2 && pad && (!captured() ? pad_armed : opened.load()) && focused && dt > 0 && dt <= .1f) {
            motion.x = std::clamp(motion.x + axis(pad, SDL_GAMEPAD_AXIS_RIGHTX) * dt * settings.sensitivity * (settings.invert_x ? -1 : 1), -1.f, 1.f);
            motion.y = std::clamp(motion.y + axis(pad, SDL_GAMEPAD_AXIS_RIGHTY) * dt * settings.sensitivity * (settings.invert_y ? -1 : 1), -1.f, 1.f);
        }
        const AuroraWpadPointer pointer{motion.x, motion.y, !captured() && ((settings.pointer == 1 && gyro_read && !motion.calibrating) || (settings.pointer == 2 && pad))};
        aurora_wpad_set_pointer(0, &pointer);
    }
}

void draw_overlay(uint64_t frames) {
    if (!initialized) return;
    const auto now = std::chrono::steady_clock::now();
    if (!fps_start.time_since_epoch().count()) { fps_start = now; fps_frames = frames; }
    const float elapsed = std::chrono::duration<float>(now-fps_start).count();
    if (elapsed >= .5f) { displayed_fps = float(frames-fps_frames)/elapsed; fps_frames = frames; fps_start = now; }
    if (settings.fps) {
        ImGui::SetNextWindowPos(ImVec2(14, 14), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(.7f);
        if (ImGui::Begin("Performance", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Text("FPS %.1f   %.1f ms", displayed_fps, displayed_fps > 0 ? 1000.f/displayed_fps : 0.f);
        }
        ImGui::End();
    }
    if (!opened) return;
    bool show = true;
    const auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(std::min(620.f, display.x-24), std::min(670.f, display.y-24)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(display.x*.5f, display.y*.5f), ImGuiCond_FirstUseEver, ImVec2(.5f,.5f));
    if (ImGui::Begin("KEY-Recomp Settings###key_settings", &show)) {
        ImGui::TextDisabled("F1: settings   F2: FPS   R: recenter");
        ImGui::TextWrapped("Game input is blocked while this menu is open. Pause the game with Plus / Options first if needed.");
        ImGui::Checkbox("Show FPS", &settings.fps);
        ImGui::Separator();
        ImGui::TextUnformatted("Player 1 controller");
        auto* pad = gamepad();
        if (ImGui::BeginCombo("Device", pad ? SDL_GetGamepadName(pad) : "Keyboard only / no assigned controller")) {
            if (ImGui::Selectable("Keyboard only", !pad)) { PADClearPort(0); }
            for (unsigned i = 0; i < PADCount(); ++i) {
                ImGui::PushID(int(i));
                if (ImGui::Selectable(PADGetNameForControllerIndex(i), int(i) == PADGetIndexForPort(0))) PADSetPortForIndex(i, 0);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::Text("Gyroscope: %s    Accelerometer: %s", gyro_ok ? "available" : "unavailable", accel_ok ? "available" : "unavailable");
        if (pad && (!gyro_ok || !accel_ok)) ImGui::TextWrapped("Motion needs the physical controller exposed to SDL. An Xbox-emulated device may not expose sensors.");
        if (ImGui::CollapsingHeader("Buttons", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("D-pad / left stick: move (sideways Wii Remote)");
            for (unsigned i = 0; i < settings.bindings.size(); ++i) {
                ImGui::PushID(int(i));
                ImGui::TextUnformatted(settings.bindings[i].label); ImGui::SameLine(270);
                if (ImGui::Button(waiting_binding == int(i) ? "Press a controller button..." : button_name(settings.bindings[i].native))) { waiting_binding = i; capture_released = false; }
                ImGui::PopID();
            }
            ImGui::TextUnformatted("Recenter pointer"); ImGui::SameLine(270);
            if (ImGui::Button(waiting_binding == 7 ? "Press a controller button..." : button_name(settings.recenter))) { waiting_binding = 7; capture_released = false; }
            if (waiting_binding >= 0 && ImGui::Button("Cancel binding")) waiting_binding = -1;
        }
        if (ImGui::CollapsingHeader("Motion and pointer", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Use controller tilt", &settings.tilt);
            ImGui::SameLine(); ImGui::Checkbox("Reverse tilt", &settings.invert_tilt);
            ImGui::SliderFloat("Tilt sensitivity", &settings.tilt_gain, .25f, 3.f, "%.2fx");
            ImGui::Combo("Pointer source", &settings.pointer, "Mouse\0Gyroscope\0Right stick\0Disabled\0");
            ImGui::SliderFloat("Pointer sensitivity", &settings.sensitivity, .2f, 5.f, "%.2f");
            ImGui::SliderFloat("Gyro deadzone (rad/s)", &settings.deadzone, 0.f, .05f, "%.3f");
            ImGui::Checkbox("Reverse pointer X", &settings.invert_x); ImGui::SameLine(); ImGui::Checkbox("Reverse pointer Y", &settings.invert_y);
            ImGui::SliderFloat("Stick deadzone", &settings.stick_deadzone, .05f, .8f, "%.2f");
            if (ImGui::Button("Recenter now")) motion.recenter(); ImGui::SameLine();
            ImGui::BeginDisabled(!gyro_ok || !accel_ok);
            if (ImGui::Button("Calibrate gyro (hold still 2s)")) motion.calibrate();
            ImGui::EndDisabled();
            if (motion.calibrating) ImGui::ProgressBar(motion.calibration_seconds / 2.f, ImVec2(-1,0), "Hold still; movement restarts calibration");
            else ImGui::TextDisabled("Gyro bias: %s (calibration lasts for this connection)", motion.calibrated ? "calibrated" : "default");
            const auto tilt = sideways_acceleration(motion.acceleration, settings.tilt_gain, settings.invert_tilt);
            ImGui::Text("Tilt: %+.1f deg   Pointer: %+.2f, %+.2f", std::atan2(-tilt[2], -tilt[1])*57.29578f, motion.x, motion.y);
            ImGui::TextWrapped("Keyboard: Q / E tilt, R recenter. Choose Mouse for keyboard pointer control. Gyro aiming is relative; recenter whenever needed.");
        }
        ImGui::Separator();
        if (ImGui::Button("Save settings")) save(); ImGui::SameLine();
        if (ImGui::Button("Restore defaults")) { settings = {}; motion = {}; waiting_binding = -1; message = "Defaults restored. Save to keep them."; }
        ImGui::SameLine(); if (ImGui::Button("Close")) show = false;
        if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
    }
    ImGui::End();
    if (!show) set_open(false);
}
}
