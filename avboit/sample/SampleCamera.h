// SPDX-License-Identifier: MIT
#pragma once
#include <donut/app/Camera.h>
#include <array>
#include <cmath>

namespace avboit
{
// Donut's first-person basis is right-handed. The calibration renderer uses
// positive view Z; reflect world Z at this boundary, not in scene geometry.
class SampleCamera : private donut::app::FirstPersonCamera
{
  public:
    using Vector = std::array<float, 3>;
    void SetPose(Vector eye, float heading = 0, float pitch = 0)
    {
        Tick(0, true, true); // Release held input and end an active drag.
        SetMoveSpeed(2.f);
        SetRotateSpeed(.003f);
        const float ch = std::cos(heading), sh = std::sin(heading), cp = std::cos(pitch), sp = std::sin(pitch);
        // Preserve exact fixed-preset floats instead of normalizing them again.
        m_CameraPos = {eye[0], eye[1], -eye[2]};
        m_CameraRight = {ch, 0, sh};
        m_CameraUp = {-sh * sp, cp, ch * sp};
        m_CameraDir = {sh * cp, sp, -ch * cp};
        UpdateWorldToView();
    }
    Vector Eye() const { return Export(m_CameraPos); }
    Vector Right() const { return Export(m_CameraRight); }
    Vector Up() const { return Export(m_CameraUp); }
    Vector Forward() const { return Export(m_CameraDir); }
    void Key(int key, int scancode, int action, int mods) { KeyboardUpdate(key, scancode, action, mods); }
    void Mouse(double x, double y) { MousePosUpdate(x, y); }
    void Button(int button, int action, int mods)
    {
        // Keep the existing RMB-look interaction; Donut rotates on LMB.
        if (button == GLFW_MOUSE_BUTTON_RIGHT)
            MouseButtonUpdate(GLFW_MOUSE_BUTTON_LEFT, action, mods);
    }
    void Tick(float seconds, bool keyboardCaptured, bool mouseCaptured)
    {
        if (keyboardCaptured)
            for (int key : {GLFW_KEY_Q, GLFW_KEY_E, GLFW_KEY_A, GLFW_KEY_D, GLFW_KEY_W, GLFW_KEY_S, GLFW_KEY_LEFT,
                            GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_Z, GLFW_KEY_C, GLFW_KEY_LEFT_SHIFT,
                            GLFW_KEY_RIGHT_SHIFT, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL})
                KeyboardUpdate(key, 0, GLFW_RELEASE, 0);
        if (mouseCaptured)
            MouseButtonUpdate(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        Animate(std::min(seconds, .1f));
    }

  private:
    static Vector Export(const donut::math::float3 &value) { return {value.x, value.y, -value.z}; }
};
} // namespace avboit
