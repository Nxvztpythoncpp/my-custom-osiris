#include "../Interfaces.h"
#include "../Memory.h"

#include "EnginePrediction.h"

#include "../SDK/ClientState.h"
#include "../SDK/Engine.h"
#include "../SDK/Entity.h"
#include "../SDK/EntityList.h"
#include "../SDK/FrameStage.h"
#include "../SDK/GameMovement.h"
#include "../SDK/GlobalVars.h"
#include "../SDK/MoveHelper.h"
#include "../SDK/Prediction.h"
#include "../SDK/PredictionCopy.h"

static int localPlayerFlags;
static Vector localPlayerVelocity;
static bool inPrediction{ false };
static std::array<EnginePrediction::NetvarData, 150> netvarData;

void EnginePrediction::reset() noexcept
{
    localPlayerFlags = {};
    localPlayerVelocity = Vector{};
    netvarData = {};
    inPrediction = false;
}

void EnginePrediction::update() noexcept
{
    if (!localPlayer || !localPlayer->isAlive())
        return;

    if (memory->clientState->deltaTick > 0)
        return;

    return interfaces->prediction->update
    (
        memory->clientState->deltaTick,
        memory->clientState->deltaTick > 0,//memory->clientState->deltaTick > 0,
        memory->clientState->lastCommandAck,
        memory->clientState->chokedCommands + memory->clientState->lastOutgoingCommand
    );
}

void EnginePrediction::run(UserCmd* cmd) noexcept
{
    if (!localPlayer || !localPlayer->isAlive())
        return;

    inPrediction = true;

    localPlayerFlags = localPlayer->flags();
    localPlayerVelocity = localPlayer->velocity();

    *memory->predictionRandomSeed = 0;
    *memory->predictionPlayer = reinterpret_cast<int>(localPlayer.get());

    auto weapon = localPlayer->getActiveWeapon();

    const auto oldCurrenttime = memory->globalVars->currenttime;
    const auto oldFrametime = memory->globalVars->frametime;
    const auto oldTickCount = memory->globalVars->tickCount;
    const auto oldIsFirstTimePredicted = interfaces->prediction->isFirstTimePredicted;
    const auto oldInPrediction = interfaces->prediction->inPrediction;

    memory->globalVars->currenttime = memory->globalVars->serverTime();
    memory->globalVars->frametime = interfaces->prediction->enginePaused ? 0.0f : memory->globalVars->intervalPerTick;
    memory->globalVars->tickCount = localPlayer->tickBase();

    interfaces->prediction->isFirstTimePredicted = false;
    interfaces->prediction->inPrediction = true;

    if (cmd->impulse)
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(localPlayer.get()) + 0x320C) = cmd->impulse;

    cmd->buttons |= *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(localPlayer.get()) + 0x3344);
    cmd->buttons &= ~(*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(localPlayer.get()) + 0x3340));

    localPlayer->updateButtonState(cmd->buttons);

    interfaces->gameMovement->startTrackPredictionErrors(localPlayer.get());

    interfaces->prediction->checkMovingGround(localPlayer.get(), memory->globalVars->frametime);

    //localPlayer->runPreThink();
    //localPlayer->runThink();

    memory->moveHelper->setHost(localPlayer.get());
    interfaces->prediction->setupMove(localPlayer.get(), cmd, memory->moveHelper, memory->moveData);
    interfaces->gameMovement->processMovement(localPlayer.get(), memory->moveData);
    interfaces->prediction->finishMove(localPlayer.get(), cmd, memory->moveData);

    memory->moveHelper->processImpacts();

    //  localPlayer->runPostThink();

    interfaces->gameMovement->finishTrackPredictionErrors(localPlayer.get());
    memory->moveHelper->setHost(nullptr);
    interfaces->gameMovement->reset();

    *memory->predictionRandomSeed = -1;
    *memory->predictionPlayer = 0;

    memory->globalVars->currenttime = oldCurrenttime;
    memory->globalVars->frametime = oldFrametime;

    interfaces->prediction->isFirstTimePredicted = oldIsFirstTimePredicted;
    interfaces->prediction->inPrediction = oldInPrediction;

    if (weapon && !weapon->isGrenade() && !weapon->isKnife())
        weapon->updateAccuracyPenalty();

    inPrediction = false;
}

void EnginePrediction::store() noexcept
{
    if (!localPlayer || !localPlayer->isAlive())
        return;

    const int tickbase = localPlayer->tickBase();

    NetvarData netvars{ };

    netvars.tickbase = tickbase;
    netvars.aimPunchAngle = localPlayer->aimPunchAngle();
    netvars.aimPunchAngleVelocity = localPlayer->aimPunchAngleVelocity();
    netvars.viewPunchAngle = localPlayer->viewPunchAngle();
    netvars.viewOffset = localPlayer->viewOffset();
    netvars.velocity = localPlayer->velocity();
    netvars.velocityModifier = localPlayer->velocityModifier();
    netvars.duckAmount = localPlayer->duckAmount();
    netvars.thirdPersonRecoil = localPlayer->thirdPersonRecoil();
    netvars.duckSpeed = localPlayer->duckSpeed();
    netvars.fallVelocity = localPlayer->fallVelocity();

    netvarData.at(tickbase % 150) = netvars;
}

void EnginePrediction::apply(FrameStage stage) noexcept
{
    if (stage != FrameStage::NET_UPDATE_END)
        return;

    if (!localPlayer || !localPlayer->isAlive())
        return;

    if (netvarData.empty())
        return;

    const int tickbase = localPlayer->tickBase();

    const auto netvars = netvarData.at(tickbase % 150);

    if (!&netvars)
        return;

    if (tickbase != netvars.tickbase)
        return;

    const auto aim_punch_vel_diff = netvars.aimPunchAngleVelocity - localPlayer->aimPunchAngleVelocity();
    const auto aim_punch_diff = netvars.aimPunchAngle - localPlayer->aimPunchAngle();
    const auto viewpunch_diff = netvars.viewPunchAngle.x - localPlayer->viewPunchAngle().x;
    const auto velocity_diff = netvars.velocity - localPlayer->velocity();
    const auto origin_diff = netvars.origin - localPlayer->origin();

    if (std::abs(aim_punch_diff.x) <= 0.03125f && std::abs(aim_punch_diff.y) <= 0.03125f && std::abs(aim_punch_diff.z) <= 0.03125f)
        localPlayer->aimPunchAngle() = netvars.aimPunchAngle;

    if (std::abs(aim_punch_vel_diff.x) <= 0.03125f && std::abs(aim_punch_vel_diff.y) <= 0.03125f && std::abs(aim_punch_vel_diff.z) <= 0.03125f)
        localPlayer->aimPunchAngleVelocity() = netvars.aimPunchAngleVelocity;

    if (std::abs(localPlayer->viewOffset().z - netvars.viewOffset.z) <= 0.25f)
        localPlayer->viewOffset().z = netvars.viewOffset.z;

    if (std::abs(viewpunch_diff) <= 0.03125f)
        localPlayer->viewPunchAngle().x = netvars.viewPunchAngle.x;

    if (abs(localPlayer->duckAmount() - netvars.duckAmount) <= 0.03125f)
        localPlayer->duckAmount() = netvars.duckAmount;

    if (std::abs(velocity_diff.x) <= 0.03125f && std::abs(velocity_diff.y) <= 0.03125f && std::abs(velocity_diff.z) <= 0.03125f)
        localPlayer->velocity() = netvars.velocity;

    if (abs(localPlayer->thirdPersonRecoil() - netvars.thirdPersonRecoil) <= 0.03125f)
        localPlayer->thirdPersonRecoil() = netvars.thirdPersonRecoil;

    if (abs(localPlayer->duckSpeed() - netvars.duckSpeed) <= 0.03125f)
        localPlayer->duckSpeed() = netvars.duckSpeed;

    if (abs(localPlayer->fallVelocity() - netvars.fallVelocity) <= 0.03125f)
        localPlayer->fallVelocity() = netvars.fallVelocity;

    if (std::abs(localPlayer->velocityModifier() - netvars.velocityModifier) <= 0.00625f)
        localPlayer->velocityModifier() = netvars.velocityModifier;
}

int EnginePrediction::getFlags() noexcept
{
    return localPlayerFlags;
}

Vector EnginePrediction::getVelocity() noexcept
{
    return localPlayerVelocity;
}

bool EnginePrediction::isInPrediction() noexcept
{
    return inPrediction;
}