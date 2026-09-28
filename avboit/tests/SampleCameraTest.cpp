// SPDX-License-Identifier: MIT
#include "../sample/SampleCamera.h"
#include <cmath>
#include <stdexcept>

int main()
{
    avboit::SampleCamera camera;
    camera.SetPose({0, 0, 0});
    camera.Key(GLFW_KEY_W, 0, GLFW_PRESS, 0);
    camera.Tick(.1f, false, false);
    if (camera.Eye()[2] < .19f)
        throw std::runtime_error("W must move into positive world Z");
    camera.Key(GLFW_KEY_W, 0, GLFW_RELEASE, 0);
    camera.Key(GLFW_KEY_D, 0, GLFW_PRESS, 0);
    camera.Tick(.1f, false, false);
    if (camera.Eye()[0] < .19f)
        throw std::runtime_error("D must move right after camera update");
    auto before = camera.Eye();
    camera.Tick(.1f, true, false);
    camera.Tick(.1f, false, false);
    if (camera.Eye() != before)
        throw std::runtime_error("Captured keyboard left a held key");
    camera.SetPose({0, 0, 0});
    camera.Mouse(100, 100);
    camera.Button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    camera.Tick(0, false, false);
    camera.Mouse(120, 100);
    camera.Tick(0, false, false);
    if (camera.Forward()[0] <= 0)
        throw std::runtime_error("RMB right drag must turn right");
    auto direction = camera.Forward();
    camera.Tick(0, false, true);
    camera.Mouse(200, 100);
    camera.Tick(0, false, false);
    if (camera.Forward() != direction)
        throw std::runtime_error("UI capture left mouse rotation active");
    camera.SetPose({1, 2, 3}, .4f);
    if (camera.Eye() != avboit::SampleCamera::Vector{1, 2, 3})
        throw std::runtime_error("Preset eye changed");
    if (std::abs(camera.Forward()[0] - std::sin(.4f)) > 1e-7f)
        throw std::runtime_error("Preset direction changed");
}
