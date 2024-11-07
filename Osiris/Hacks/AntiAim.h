#pragma once

#include "../ConfigStructs.h"

struct UserCmd;
struct Vector;

namespace AntiAim
{
    float breakLC(bool& sendPacket) noexcept;
    float getLBYUpdate() noexcept;
    float getYawAtTargets( const Vector& viewangles) noexcept;
    float freestand(UserCmd* cmd) noexcept;

    void rage(UserCmd* cmd, const Vector& previousViewAngles, const Vector& currentViewAngles, bool& sendPacket) noexcept;
    void legit(UserCmd* cmd, const Vector& previousViewAngles, const Vector& currentViewAngles, bool& sendPacket) noexcept;

    void run(UserCmd* cmd, const Vector& previousViewAngles, const Vector& currentViewAngles, bool& sendPacket) noexcept;
    void updateInput() noexcept;
    bool canRun(UserCmd* cmd) noexcept;
}
