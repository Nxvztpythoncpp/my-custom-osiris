#include "../Config.h"

#include "AimbotFunctions.h"
#include "Animations.h"
#include "Backtrack.h"
#include "Tickbase.h"

#include "../SDK/ConVar.h"
#include "../SDK/Entity.h"
#include "../SDK/FrameStage.h"
#include "../SDK/LocalPlayer.h"
#include "../SDK/NetworkChannel.h"
#include "../SDK/UserCmd.h"

static std::deque<Backtrack::incomingSequence> sequences;

struct Cvars {
    ConVar* updateRate;
    ConVar* maxUpdateRate;
    ConVar* interp;
    ConVar* interpRatio;
    ConVar* minInterpRatio;
    ConVar* maxInterpRatio;
    ConVar* maxUnlag;
};

static Cvars cvars;

float Backtrack::getLerp() noexcept
{
    static auto cl_ud_rate = cvars.updateRate;
    static auto min_ud_rate = cvars.minInterpRatio;
    static auto max_ud_rate = cvars.maxUpdateRate;

    int ud_rate = 64;
    if (cl_ud_rate)
        ud_rate = cl_ud_rate->getInt();

    if (min_ud_rate && max_ud_rate)
        ud_rate = max_ud_rate->getInt();

    float ratio = 1.f;
    static auto cl_interp_ratio = cvars.interpRatio;
    if (cl_interp_ratio)
        ratio = cl_interp_ratio->getFloat();

    static auto cl_interp = cvars.interp;
    static auto c_min_ratio = cvars.minInterpRatio;
    static auto c_max_ratio = cvars.maxInterpRatio;

    float lerp = memory->globalVars->intervalPerTick;
    if (cl_interp)
        lerp = cl_interp->getFloat();

    if (c_min_ratio && c_max_ratio && c_min_ratio->getFloat() != 1)
        ratio = std::clamp(ratio, c_min_ratio->getFloat(), c_max_ratio->getFloat());

    return max(lerp, ratio / ud_rate);
}

void Backtrack::run(UserCmd* cmd) noexcept
{
    if (!config->backtrack.enabled)
        return;

    if (!(cmd->buttons & UserCmd::IN_ATTACK))
        return;

    if (!localPlayer || !localPlayer->isAlive())
        return;

    if (!config->backtrack.ignoreFlash && localPlayer->isFlashed())
        return;

    const auto activeWeapon = localPlayer->getActiveWeapon();
    if (!activeWeapon || !activeWeapon->clip())
        return;

    auto localPlayerEyePosition = localPlayer->getEyePosition();

    auto bestFov{ 255.f };
    int bestTargetIndex{ };
    int bestRecord{ };

    const auto aimPunch = activeWeapon->requiresRecoilControl() ? localPlayer->getAimPunch() : Vector{ };

    for (int i = 1; i <= interfaces->engine->getMaxClients(); i++) {
        auto entity = interfaces->entityList->getEntity(i);
        if (!entity || entity == localPlayer.get() || entity->isDormant() || !entity->isAlive()
            || !entity->isOtherEnemy(localPlayer.get()))
            continue;

        const auto player = Animations::getPlayer(i);
        if (!player.gotMatrix)
            continue;

        if (player.backtrackRecords.empty() || (!config->backtrack.ignoreSmoke && memory->lineGoesThroughSmoke(localPlayer->getEyePosition(), entity->getAbsOrigin(), 1)))
            continue;

        for (int j = static_cast<int>(player.backtrackRecords.size() - 1); j >= 0; j--)
        {
            if (Backtrack::valid(player.backtrackRecords.at(j).simulationTime))
            {
                for (auto position : player.backtrackRecords.at(j).positions) {
                    auto angle = AimbotFunction::calculateRelativeAngle(localPlayerEyePosition, position, cmd->viewangles + aimPunch);
                    auto fov = std::hypotf(angle.x, angle.y);
                    if (fov < bestFov) {
                        bestFov = fov;
                        bestRecord = j;
                        bestTargetIndex = i;
                    }
                }
            }
        }
    }

    const auto player = Animations::getPlayer(bestTargetIndex);
    if (!player.gotMatrix)
        return;


    if (bestRecord) {
        const auto& record = player.backtrackRecords[bestRecord];
        cmd->tickCount = timeToTicks(record.simulationTime + getLerp());// YES BETTER THAN DEF 
    }
}

void Backtrack::addLatencyToNetwork(NetworkChannel* network, float latency) noexcept
{
    for (auto& sequence : sequences)
    {
        if (memory->globalVars->serverTime() - sequence.servertime >= latency)
        {
            network->inReliableState = sequence.inreliablestate;
            network->inSequenceNr = sequence.sequencenr;
            break;
        }
    }
}

void Backtrack::updateIncomingSequences() noexcept
{
    static int lastIncomingSequenceNumber = 0;

    if (!config->backtrack.fakeLatency)
        return;

    if (!localPlayer)
        return;

    auto network = interfaces->engine->getNetworkChannel();
    if (!network)
        return;

    if (network->inSequenceNr != lastIncomingSequenceNumber)
    {
        lastIncomingSequenceNumber = network->inSequenceNr;

        incomingSequence sequence{ };
        sequence.inreliablestate = network->inReliableState;
        sequence.sequencenr = network->inSequenceNr;
        sequence.servertime = memory->globalVars->serverTime();
        sequences.push_front(sequence);
    }

    while (sequences.size() > 2048U)
        sequences.pop_back();
}

bool Backtrack::valid(float simtime) noexcept
{
    const auto channel_info = interfaces->engine->getNetworkChannel();
    if (!channel_info)
        return false;

    // True latency + Network latency + Interpolation latency (lerp time)
    float correct = channel_info->getLatency(0) + channel_info->getLatency(1) + getLerp();

    // Check to see if our correction is within 0 - sv_maxunlag
    std::clamp(correct, 0.f, cvars.maxUnlag->getFloat());

    // Use cur_time instead of localplayer tickbase because we are not using the 2013 sdk
    float delta_time = correct - (memory->globalVars->currenttime - simtime);

    // @todo: Add ping spike. See #68
    const float time_limit = 0.2f;
    return fabsf(delta_time) <= time_limit;
}

void Backtrack::init() noexcept
{
    cvars.updateRate = interfaces->cvar->findVar("cl_updaterate");
    cvars.maxUpdateRate = interfaces->cvar->findVar("sv_maxupdaterate");
    cvars.interp = interfaces->cvar->findVar("cl_interp");
    cvars.interpRatio = interfaces->cvar->findVar("cl_interp_ratio");
    cvars.minInterpRatio = interfaces->cvar->findVar("sv_client_min_interp_ratio");
    cvars.maxInterpRatio = interfaces->cvar->findVar("sv_client_max_interp_ratio");
    cvars.maxUnlag = interfaces->cvar->findVar("sv_maxunlag");
}

float Backtrack::getMaxUnlag() noexcept
{
    return cvars.maxUnlag->getFloat();
}
