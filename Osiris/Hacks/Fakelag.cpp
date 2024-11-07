#include "EnginePrediction.h"
#include "Fakelag.h"
#include "Tickbase.h"

#include "../SDK/Entity.h"
#include "../SDK/UserCmd.h"
#include "../SDK/NetworkChannel.h"
#include "../SDK/Localplayer.h"
#include "../SDK/Vector.h"

void Fakelag::run(bool& sendPacket) noexcept
{
    if (!localPlayer || !localPlayer->isAlive())
        return;

    const auto netChannel = interfaces->engine->getNetworkChannel();
    if (!netChannel)
        return;

    auto chokedPackets = config->legitAntiAim.enabled || config->fakeAngle.enabled ? 2 : 0;
    int tick_to_choke = 1;
    if (config->fakelag.enabled)
    {
        const float speed = EnginePrediction::getVelocity().length2D() >= 15.0f ? EnginePrediction::getVelocity().length2D() : 0.0f;
        
        bool walking =localPlayer->velocity().length2D()>=1.2f;
        if (walking) {  //break lc
            auto velocity = localPlayer->velocity();
            auto currentSpeed = velocity.length2D();
            auto extrapolatePerTick = currentSpeed * memory->globalVars->intervalPerTick;
            int chokeReq = std::ceilf(64.f / extrapolatePerTick);

            if (chokeReq < 14) {
                chokedPackets = chokeReq;
            }
            else {
                chokedPackets = 14;
            }
            chokedPackets = min(chokedPackets, config->fakelag.limit);
        }
        else { //static
            chokedPackets = config->fakelag.limit;
        }

        /*
        switch (config->fakelag.mode) {
        case 0: //Static
            chokedPackets = config->fakelag.limit;
            break;
        case 1: //Adaptive
            chokedPackets = std::clamp(static_cast<int>(std::ceilf(64 / (speed * memory->globalVars->intervalPerTick))), 1, config->fakelag.limit);
            break;
        case 2: // Random
            srand(static_cast<unsigned int>(time(NULL)));
            chokedPackets = rand() % config->fakelag.limit + 1;
            break;
        case 3: { // breakLC
            auto velocity = localPlayer->velocity();
            auto currentSpeed = velocity.length2D();
            auto extrapolatePerTick = currentSpeed * memory->globalVars->intervalPerTick;
            int chokeReq = std::ceilf(64.f / extrapolatePerTick);

            if (chokeReq < 14) {
                chokedPackets = chokeReq;
            }
            else {
                chokedPackets = 14;
            }
            chokedPackets = min(chokedPackets, config->fakelag.limit);
            break;
        }

        case 4: {// automatic


            break;
        }
        }*/
    }

    chokedPackets = std::clamp(chokedPackets, 0, maxUserCmdProcessTicks - Tickbase::getTargetTickShift());

    sendPacket = netChannel->chokedPackets >= chokedPackets;
}