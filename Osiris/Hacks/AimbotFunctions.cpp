#include "../Config.h"
#include "../Interfaces.h"
#include "../Memory.h"

#include "AimbotFunctions.h"
#include "Animations.h"

#include "../SDK/Angle.h"
#include "../SDK/ConVar.h"
#include "../SDK/Entity.h"
#include "../SDK/UserCmd.h"
#include "../SDK/Vector.h"
#include "../SDK/WeaponId.h"
#include "../SDK/GlobalVars.h"
#include "../SDK/PhysicsSurfaceProps.h"
#include "../SDK/WeaponData.h"
#include "../SDK/ModelInfo.h"
#include <DirectXMath.h>
#include <algorithm>

#define RAD2DEG(x) DirectX::XMConvertToDegrees( x ) 
#define DEG2RAD(x) DirectX::XMConvertToRadians( x )
#define M_PI 3.14159265358979323846
#define PI_F ( ( float )( M_PI ) ) 
#define M_RADPI 57.295779513082f

constexpr auto degreesToRadians = [](float degrees) constexpr noexcept { return degrees * static_cast<float>(M_PI) / 180.0f; };
constexpr auto radiansToDegrees = [](float radians) constexpr noexcept { return radians * 180.0f / static_cast<float>(M_PI); };

Vector AimbotFunction::calculateRelativeAngle(const Vector& source, const Vector& destination, const Vector& viewAngles) noexcept
{
    Vector delta = destination - source;
    Vector angles{ radiansToDegrees(atan2f(-delta.z, std::hypotf(delta.x, delta.y))) - viewAngles.x,
                   radiansToDegrees(atan2f(delta.y, delta.x)) - viewAngles.y };
    angles.normalize();
    return angles;

    //return ((destination - source).toAngle() - viewAngles).normalize();
}

static bool traceToExit(const Trace& enterTrace, const Vector& start, const Vector& direction, Vector& end, Trace& exitTrace, float range = 90.f, float step = 4.0f)
{
    float distance{ 0.0f };
    int previousContents{ 0 };

    while (distance <= range)
    {
        distance += step;
        Vector origin{ start + direction * distance };

        if (!previousContents)
            previousContents = interfaces->engineTrace->getPointContents(origin, 0x4600400B);

        const int currentContents = interfaces->engineTrace->getPointContents(origin, 0x4600400B);
        if (!(currentContents & 0x600400B) || (currentContents & 0x40000000 && currentContents != previousContents))
        {
            const Vector destination{ origin - (direction * step) };

            if (interfaces->engineTrace->traceRay({ origin, destination }, 0x4600400B, nullptr, exitTrace); exitTrace.startSolid && exitTrace.surface.flags & 0x8000)
            {
                if (interfaces->engineTrace->traceRay({ origin, start }, 0x600400B, { exitTrace.entity }, exitTrace); exitTrace.didHit() && !exitTrace.startSolid)
                    return true;

                continue;
            }

            if (exitTrace.didHit() && !exitTrace.startSolid)
            {
                if (memory->isBreakableEntity(enterTrace.entity) && memory->isBreakableEntity(exitTrace.entity))
                    return true;

                if (enterTrace.surface.flags & 0x0080 || (!(exitTrace.surface.flags & 0x0080) && exitTrace.plane.normal.dotProduct(direction) <= 1.0f))
                    return true;

                continue;
            }
            else {
                if (enterTrace.entity && enterTrace.entity->index() != 0 && memory->isBreakableEntity(enterTrace.entity))
                    return true;

                continue;
            }
        }
    }
    return false;
}

static float handleBulletPenetration(SurfaceData* enterSurfaceData, const Trace& enterTrace, const Vector& direction, Vector& result, float penetration, float damage) noexcept
{
    Vector end;
    Trace exitTrace;

    if (!traceToExit(enterTrace, enterTrace.endpos, direction, end, exitTrace))
        return -1.0f;

    SurfaceData* exitSurfaceData = interfaces->physicsSurfaceProps->getSurfaceData(exitTrace.surface.surfaceProps);

    float damageModifier = 0.16f;
    float penetrationModifier = (enterSurfaceData->penetrationmodifier + exitSurfaceData->penetrationmodifier) / 2.0f;

    if (enterSurfaceData->material == 71 || enterSurfaceData->material == 89) {
        damageModifier = 0.05f;
        penetrationModifier = 3.0f;
    }
    else if (enterTrace.contents >> 3 & 1 || enterTrace.surface.flags >> 7 & 1) {
        penetrationModifier = 1.0f;
    }

    if (enterSurfaceData->material == exitSurfaceData->material) {
        if (exitSurfaceData->material == 85 || exitSurfaceData->material == 87)
            penetrationModifier = 3.0f;
        else if (exitSurfaceData->material == 76)
            penetrationModifier = 2.0f;
    }

    damage -= 11.25f / penetration / penetrationModifier + damage * damageModifier + (exitTrace.endpos - enterTrace.endpos).squareLength() / 24.0f / penetrationModifier;

    result = exitTrace.endpos;
    return damage;
}

void AimbotFunction::calculateArmorDamage(float armorRatio, int armorValue, bool hasHeavyArmor, float& damage) noexcept
{
    auto armorScale = 1.0f;
    auto armorBonusRatio = 0.5f;

    if (hasHeavyArmor)
    {
        armorRatio *= 0.5f;
        armorBonusRatio = 0.5f;
        armorScale = 0.25f;
    }

    auto newDamage = damage * armorRatio;
    const auto estiminated_damage = (damage - damage * armorRatio) * armorScale * armorBonusRatio;

    if (estiminated_damage > armorValue)
        newDamage = damage - armorValue / armorBonusRatio;

    damage = newDamage;
}

bool AimbotFunction::canScan(Entity* entity, const Vector& destination, const WeaponInfo* weaponData, int minDamage, bool allowFriendlyFire) noexcept
{
    if (!localPlayer)
        return false;

    float damage{ static_cast<float>(weaponData->damage) };

    Vector start{ localPlayer->getEyePosition() };
    Vector direction{ destination - start };
    float maxDistance{ direction.length() };
    float curDistance{ 0.0f };
    direction /= maxDistance;

    int hitsLeft = 4;

    while (damage >= 1.0f && hitsLeft) {
        Trace trace;
        interfaces->engineTrace->traceRay({ start, destination }, 0x4600400B, localPlayer.get(), trace);

        if (!allowFriendlyFire && trace.entity && trace.entity->isPlayer() && !localPlayer->isOtherEnemy(trace.entity))
            return false;

        if (trace.fraction == 1.0f)
            break;

        curDistance += trace.fraction * (maxDistance - curDistance);
        damage *= std::pow(weaponData->rangeModifier, curDistance / 500.0f);

        if (trace.entity == entity && trace.hitgroup > HitGroup::Generic && trace.hitgroup <= HitGroup::RightLeg) {
            damage *= HitGroup::getDamageMultiplier(trace.hitgroup, weaponData, trace.entity->hasHeavyArmor(), static_cast<int>(trace.entity->getTeamNumber()));

            if (float armorRatio{ weaponData->armorRatio / 2.0f }; HitGroup::isArmored(trace.hitgroup, trace.entity->hasHelmet(), trace.entity->armor(), trace.entity->hasHeavyArmor()))
                calculateArmorDamage(armorRatio, trace.entity->armor(), trace.entity->hasHeavyArmor(), damage);

            if (damage >= minDamage)
                return damage;
            return 0.f;
        }
        const auto surfaceData = interfaces->physicsSurfaceProps->getSurfaceData(trace.surface.surfaceProps);

        if (surfaceData->penetrationmodifier < 0.1f)
            break;

        damage = handleBulletPenetration(surfaceData, trace, direction, start, weaponData->penetration, damage);
        hitsLeft--;
    }
    return false;
}

float AimbotFunction::getScanDamage(Entity* entity, const Vector& destination, const WeaponInfo* weaponData, int minDamage, bool allowFriendlyFire) noexcept
{
    if (!localPlayer)
        return 0.f;

    float damage{ static_cast<float>(weaponData->damage) };

    Vector start{ localPlayer->getEyePosition() };
    Vector direction{ destination - start };
    float maxDistance{ direction.length() };
    float curDistance{ 0.0f };
    direction /= maxDistance;

    int hitsLeft = 4;

    while (damage >= 1.0f && hitsLeft) {
        Trace trace;
        interfaces->engineTrace->traceRay({ start, destination }, 0x4600400B, localPlayer.get(), trace);

        if (!allowFriendlyFire && trace.entity && trace.entity->isPlayer() && !localPlayer->isOtherEnemy(trace.entity))
            return 0.f;

        if (trace.fraction == 1.0f)
            break;

        curDistance += trace.fraction * (maxDistance - curDistance);
        damage *= std::pow(weaponData->rangeModifier, curDistance / 500.0f);

        if (trace.entity == entity && trace.hitgroup > HitGroup::Generic && trace.hitgroup <= HitGroup::RightLeg) {
            damage *= HitGroup::getDamageMultiplier(trace.hitgroup, weaponData, trace.entity->hasHeavyArmor(), static_cast<int>(trace.entity->getTeamNumber()));

            if (float armorRatio{ weaponData->armorRatio / 2.0f }; HitGroup::isArmored(trace.hitgroup, trace.entity->hasHelmet(), trace.entity->armor(), trace.entity->hasHeavyArmor()))
                calculateArmorDamage(armorRatio, trace.entity->armor(), trace.entity->hasHeavyArmor(), damage);

            if (damage >= minDamage)
                return damage;
            return 0.f;
        }
        const auto surfaceData = interfaces->physicsSurfaceProps->getSurfaceData(trace.surface.surfaceProps);

        if (surfaceData->penetrationmodifier < 0.1f)
            break;

        damage = handleBulletPenetration(surfaceData, trace, direction, start, weaponData->penetration, damage);
        hitsLeft--;
    }
    return 0.f;
}

float segmentToSegment(const Vector& s1, const Vector& s2, const Vector& k1, const Vector& k2) noexcept
{
    static auto constexpr epsilon = 0.00000001f;

    auto u = s2 - s1;
    auto v = k2 - k1;
    auto w = s1 - k1;

    auto a = u.dotProduct(u); //-V525
    auto b = u.dotProduct(v);
    auto c = v.dotProduct(v);
    auto d = u.dotProduct(w);
    auto e = v.dotProduct(w);
    auto D = a * c - b * b;

    auto sn = 0.0f, sd = D;
    auto tn = 0.0f, td = D;

    if (D < epsilon)
    {
        sn = 0.0f;
        sd = 1.0f;
        tn = e;
        td = c;
    }
    else
    {
        sn = b * e - c * d;
        tn = a * e - b * d;

        if (sn < 0.0f)
        {
            sn = 0.0f;
            tn = e;
            td = c;
        }
        else if (sn > sd)
        {
            sn = sd;
            tn = e + b;
            td = c;
        }
    }

    if (tn < 0.0f)
    {
        tn = 0.0f;

        if (-d < 0.0f)
            sn = 0.0f;
        else if (-d > a)
            sn = sd;
        else
        {
            sn = -d;
            sd = a;
        }
    }
    else if (tn > td)
    {
        tn = td;

        if (-d + b < 0.0f)
            sn = 0.0f;
        else if (-d + b > a)
            sn = sd;
        else
        {
            sn = -d + b;
            sd = a;
        }
    }

    auto sc = fabs(sn) < epsilon ? 0.0f : sn / sd;
    auto tc = fabs(tn) < epsilon ? 0.0f : tn / td;

    auto dp = w + u * sc - v * tc;
    return dp.length();
}

bool intersectLineWithBb(Vector& start, Vector& end, Vector& min, Vector& max) noexcept
{
    float d1, d2, f;
    auto start_solid = true;
    auto t1 = -1.0f, t2 = 1.0f;

    const float s[3] = { start.x, start.y, start.z };
    const float e[3] = { end.x, end.y, end.z };
    const float mi[3] = { min.x, min.y, min.z };
    const float ma[3] = { max.x, max.y, max.z };

    for (auto i = 0; i < 6; i++) {
        if (i >= 3) {
            const auto j = i - 3;

            d1 = s[j] - ma[j];
            d2 = d1 + e[j];
        }
        else {
            d1 = -s[i] + mi[i];
            d2 = d1 - e[i];
        }

        if (d1 > 0.0f && d2 > 0.0f)
            return false;

        if (d1 <= 0.0f && d2 <= 0.0f)
            continue;

        if (d1 > 0)
            start_solid = false;

        if (d1 > d2) {
            f = d1;
            if (f < 0.0f)
                f = 0.0f;

            f /= d1 - d2;
            if (f > t1)
                t1 = f;
        }
        else {
            f = d1 / (d1 - d2);
            if (f < t2)
                t2 = f;
        }
    }

    return start_solid || (t1 < t2 && t1 >= 0.0f);
}

void inline sinCos(float radians, float* sine, float* cosine)
{
    *sine = sin(radians);
    *cosine = cos(radians);
}

Vector vectorRotate(Vector& in1, Vector& in2) noexcept
{
    auto vector_rotate = [](const Vector& in1, const matrix3x4& in2)
        {
            return Vector(in1.dotProduct(in2[0]), in1.dotProduct(in2[1]), in1.dotProduct(in2[2]));
        };
    auto angleMatrix = [](const Vector& angles, matrix3x4& matrix)
        {
            float sr, sp, sy, cr, cp, cy;

            sinCos(Helpers::deg2rad(angles[1]), &sy, &cy);
            sinCos(Helpers::deg2rad(angles[0]), &sp, &cp);
            sinCos(Helpers::deg2rad(angles[2]), &sr, &cr);

            // matrix = (YAW * PITCH) * ROLL
            matrix[0][0] = cp * cy;
            matrix[1][0] = cp * sy;
            matrix[2][0] = -sp;

            float crcy = cr * cy;
            float crsy = cr * sy;
            float srcy = sr * cy;
            float srsy = sr * sy;
            matrix[0][1] = sp * srcy - crsy;
            matrix[1][1] = sp * srsy + crcy;
            matrix[2][1] = sr * cp;

            matrix[0][2] = (sp * crcy + srsy);
            matrix[1][2] = (sp * crsy - srcy);
            matrix[2][2] = cr * cp;

            matrix[0][3] = 0.0f;
            matrix[1][3] = 0.0f;
            matrix[2][3] = 0.0f;
        };
    matrix3x4 m;
    angleMatrix(in2, m);
    return vector_rotate(in1, m);
}

void vectorITransform(const Vector& in1, const matrix3x4& in2, Vector& out) noexcept
{
    out.x = (in1.x - in2[0][3]) * in2[0][0] + (in1.y - in2[1][3]) * in2[1][0] + (in1.z - in2[2][3]) * in2[2][0];
    out.y = (in1.x - in2[0][3]) * in2[0][1] + (in1.y - in2[1][3]) * in2[1][1] + (in1.z - in2[2][3]) * in2[2][1];
    out.z = (in1.x - in2[0][3]) * in2[0][2] + (in1.y - in2[1][3]) * in2[1][2] + (in1.z - in2[2][3]) * in2[2][2];
}

void vectorIRotate(Vector in1, matrix3x4 in2, Vector& out) noexcept
{
    out.x = in1.x * in2[0][0] + in1.y * in2[1][0] + in1.z * in2[2][0];
    out.y = in1.x * in2[0][1] + in1.y * in2[1][1] + in1.z * in2[2][1];
    out.z = in1.x * in2[0][2] + in1.y * in2[1][2] + in1.z * in2[2][2];
}

bool AimbotFunction::hitboxIntersection(const matrix3x4 matrix[MAXSTUDIOBONES], int iHitbox, StudioHitboxSet* set, const Vector& start, const Vector& end) noexcept
{
    auto VectorTransform_Wrapper = [](const Vector& in1, const matrix3x4 in2, Vector& out)
        {
            auto VectorTransform = [](const float* in1, const matrix3x4 in2, float* out)
                {
                    auto DotProducts = [](const float* v1, const float* v2)
                        {
                            return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];
                        };
                    out[0] = DotProducts(in1, in2[0]) + in2[0][3];
                    out[1] = DotProducts(in1, in2[1]) + in2[1][3];
                    out[2] = DotProducts(in1, in2[2]) + in2[2][3];
                };
            VectorTransform(&in1.x, in2, &out.x);
        };

    StudioBbox* hitbox = set->getHitbox(iHitbox);
    if (!hitbox)
        return false;

    if (hitbox->capsuleRadius == -1.f)
        return false;

    Vector mins, maxs;
    const auto isCapsule = hitbox->capsuleRadius != -1.f;
    if (isCapsule)
    {
        VectorTransform_Wrapper(hitbox->bbMin, matrix[hitbox->bone], mins);
        VectorTransform_Wrapper(hitbox->bbMax, matrix[hitbox->bone], maxs);
        const auto dist = segmentToSegment(start, end, mins, maxs);

        if (dist < hitbox->capsuleRadius)
            return true;
    }
    else
    {
        VectorTransform_Wrapper(vectorRotate(hitbox->bbMin, hitbox->offsetOrientation), matrix[hitbox->bone], mins);
        VectorTransform_Wrapper(vectorRotate(hitbox->bbMax, hitbox->offsetOrientation), matrix[hitbox->bone], maxs);

        vectorITransform(start, matrix[hitbox->bone], mins);
        vectorIRotate(end, matrix[hitbox->bone], maxs);

        if (intersectLineWithBb(mins, maxs, hitbox->bbMin, hitbox->bbMax))
            return true;
    }
    return false;
}

std::vector<Vector> AimbotFunction::multiPoint(Entity* entity, const matrix3x4 matrix[MAXSTUDIOBONES], StudioBbox* hitbox, Vector localEyePos, int _hitbox, int _multiPoint)
{
    auto VectorTransformWrapper = [](const Vector& in1, const matrix3x4 in2, Vector& out)
        {
            auto VectorTransform = [](const float* in1, const matrix3x4 in2, float* out)
                {
                    auto dotProducts = [](const float* v1, const float* v2)
                        {
                            return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];
                        };
                    out[0] = dotProducts(in1, in2[0]) + in2[0][3];
                    out[1] = dotProducts(in1, in2[1]) + in2[1][3];
                    out[2] = dotProducts(in1, in2[2]) + in2[2][3];
                };
            VectorTransform(&in1.x, in2, &out.x);
        };

    auto CalcAngle = [](const Vector& src, const Vector& dst)
        {
            Vector vAngle;
            Vector delta((src.x - dst.x), (src.y - dst.y), (src.z - dst.z));
            double hyp = sqrt(delta.x * delta.x + delta.y * delta.y);

            vAngle.x = float(atanf(float(delta.z / hyp)) * 57.295779513082f);
            vAngle.y = float(atanf(float(delta.y / delta.x)) * 57.295779513082f);
            vAngle.z = 0.0f;

            if (delta.x >= 0.0)
                vAngle.y += 180.0f;

            return vAngle;
        };

    auto AngleVectors = [](const Vector& vecAngles, Vector& vecForward)
        {
            float sp, sy, cp, cy;

            DirectX::XMScalarSinCos(&sp, &cp, DEG2RAD(vecAngles[0]));
            DirectX::XMScalarSinCos(&sy, &cy, DEG2RAD(vecAngles[1]));

            vecForward.x = cp * cy;
            vecForward.y = cp * sy;
            vecForward.z = -sp;
        };

    float flModifier = fmaxf(hitbox->capsuleRadius, 0.f);

    Vector vecMax;
    Vector vecMin;

    VectorTransformWrapper(Vector(hitbox->bbMax.x, hitbox->bbMax.y, hitbox->bbMax.z), matrix[hitbox->bone], vecMax);
    VectorTransformWrapper(Vector(hitbox->bbMin.x, hitbox->bbMin.y, hitbox->bbMin.z), matrix[hitbox->bone], vecMin);

    Vector vecCenter = (vecMin + vecMax) * 0.5f;

    Vector angAngle = CalcAngle(localEyePos, vecCenter);

    Vector vecForward;
    AngleVectors(angAngle, vecForward);

    Vector vecRight = vecForward.cross(Vector(0, 0, 2.33f));
    Vector vecLeft = Vector(-vecRight.x, -vecRight.y, vecRight.z);

    Vector vecTop = Vector(0, 0, 3.25f);
    Vector vecBottom = Vector(0, 0, -3.25f);

    std::vector < Vector > aMultipoints = { };
    if (_hitbox == Hitboxes::Head)
    {
        aMultipoints.emplace_back(vecCenter);
        aMultipoints.emplace_back(vecCenter + ((vecTop + vecRight)));
        aMultipoints.emplace_back(vecCenter + ((vecTop + vecLeft)));
    }
    else if (_hitbox == Hitboxes::UpperChest || _hitbox == Hitboxes::LowerChest)
    {
        aMultipoints.emplace_back(vecCenter + Vector(0, 0, 3));
        aMultipoints.emplace_back(vecCenter + vecRight * flModifier + Vector(0, 0, 3));
        aMultipoints.emplace_back(vecCenter + vecLeft * flModifier + Vector(0, 0, 3));
    }
    else if (_hitbox == Hitboxes::Belly)
    {
        aMultipoints.emplace_back(vecCenter + Vector(0, 0, 3));
        aMultipoints.emplace_back(vecCenter + vecRight * flModifier + Vector(0, 0, 3));
        aMultipoints.emplace_back(vecCenter + vecLeft * flModifier + Vector(0, 0, 3));
    }
    else if (_hitbox == Hitboxes::Pelvis)
    {
        aMultipoints.emplace_back(vecCenter + Vector(0, 0, 2));
        aMultipoints.emplace_back(vecCenter + vecRight * flModifier + Vector(0, 0, 2));
        aMultipoints.emplace_back(vecCenter + vecLeft * flModifier + Vector(0, 0, 2));
    }
    else if (_hitbox == Hitboxes::LeftFoot || _hitbox == Hitboxes::RightFoot || _hitbox == Hitboxes::LeftThigh || _hitbox == Hitboxes::RightThigh)
    {
        Vector vecAddition = vecLeft;
        if (_hitbox == Hitboxes::LeftFoot || _hitbox == Hitboxes::LeftThigh)
            vecAddition = vecRight;
        else if (_hitbox == Hitboxes::RightFoot || _hitbox == Hitboxes::RightThigh)
            vecAddition = vecLeft;

        if (_hitbox == Hitboxes::LeftThigh || _hitbox == Hitboxes::RightThigh)
            vecCenter -= Vector(0.0f, 0.0f, 2.5f);

        aMultipoints.emplace_back(vecCenter - (vecAddition * 0.90f));
    }
    else if (_hitbox == Hitboxes::LeftUpperArm)
        aMultipoints.emplace_back(vecCenter - vecLeft);
    else if (_hitbox == Hitboxes::RightForearm)
        aMultipoints.emplace_back(vecCenter + vecLeft);

    return aMultipoints;

}

bool AimbotFunction::hitChance(Entity* localPlayer, Entity* entity, StudioHitboxSet* set, const matrix3x4 matrix[MAXSTUDIOBONES], Entity* activeWeapon, const Vector& destination, const UserCmd* cmd, const int hitChance) noexcept
{
    static auto isSpreadEnabled = interfaces->cvar->findVar("weapon_accuracy_nospread");
    if (!hitChance || isSpreadEnabled->getInt() >= 1)
        return true;

    constexpr int maxSeed = 255;

    const Angle angles(destination + cmd->viewangles);

    int hits = 0;
    const int hitsNeed = static_cast<int>(static_cast<float>(maxSeed) * (static_cast<float>(hitChance) / 100.f));

    const auto weapSpread = activeWeapon->getSpread();
    const auto weapInaccuracy = activeWeapon->getInaccuracy();
    const auto localEyePosition = localPlayer->getEyePosition();
    const auto range = activeWeapon->getWeaponData()->range;

    for (int i = 0; i < maxSeed; i++)
    {
        memory->randomSeed(i + 1);
        const float spreadX = memory->randomFloat(0.f, 2.f * static_cast<float>(M_PI));
        const float spreadY = memory->randomFloat(0.f, 2.f * static_cast<float>(M_PI));
        auto inaccuracy = weapInaccuracy * memory->randomFloat(0.f, 1.f);
        auto spread = weapSpread * memory->randomFloat(0.f, 1.f);

        Vector spreadView{ (cosf(spreadX) * inaccuracy) + (cosf(spreadY) * spread),
                           (sinf(spreadX) * inaccuracy) + (sinf(spreadY) * spread) };
        Vector direction{ (angles.forward + (angles.right * spreadView.x) + (angles.up * spreadView.y)) * range };

        for (int hitbox = 0; hitbox < Hitboxes::Max; hitbox++)
        {
            if (hitboxIntersection(matrix, hitbox, set, localEyePosition, localEyePosition + direction))
            {
                hits++;
                break;
            }
        }

        if (hits >= hitsNeed)
            return true;

        if ((maxSeed - i + hits) < hitsNeed)
            return false;
    }
    return false;

}


/*
bool AimbotFunction::hitChance(Entity* localPlayer, Entity* entity, StudioHitboxSet* set, const matrix3x4 matrix[MAXSTUDIOBONES], Entity* activeWeapon, const Vector& destination, const UserCmd* cmd, const int hitChance) noexcept
{
    static auto isSpreadEnabled = interfaces->cvar->findVar("weapon_accuracy_nospread");
    if (!hitChance || isSpreadEnabled->getInt() == 1)
        return true;

    constexpr int maxSeed = 255;

    const Angle angles(destination + cmd->viewangles);

    int hits = 0;
    const int hitsNeed = static_cast<int>(static_cast<float>(maxSeed) * (static_cast<float>(hitChance) / 100.f));

    const auto weapSpread = entity->getSpread(); //weapon data
    const auto weapInaccuracy = entity->getInaccuracy(); //weapon data
    const auto localEyePosition = localPlayer->getEyePosition();
    const auto range = entity->getWeaponData()->range; //weapon data

    for (int i = 0; i < maxSeed; ++i)
    {
        memory->randomSeed(i + 1);
        float inaccuracy = memory->randomFloat(0.f, 1.f);
        float spread = memory->randomFloat(0.f, 1.f);
        const float spreadX = memory->randomFloat(0.f, 2.f * static_cast<float>(M_PI));
        const float spreadY = memory->randomFloat(0.f, 2.f * static_cast<float>(M_PI));

        const auto weaponIndex = entity->itemDefinitionIndex2(); //weapon data
        if (weaponIndex == WeaponId::Revolver)
        {
            if (cmd->buttons & UserCmd::IN_ATTACK2)
            {
                inaccuracy = 1.f - inaccuracy * inaccuracy;
                spread = 1.f - spread * spread;
            }
        }

        inaccuracy *= weapInaccuracy;
        spread *= weapSpread;

        Vector spreadView{ (cosf(spreadX) * inaccuracy) + (cosf(spreadY) * spread),
                           (sinf(spreadX) * inaccuracy) + (sinf(spreadY) * spread) };
        Vector direction{ (angles.forward + (angles.right * spreadView.x) + (angles.up * spreadView.y)) * range };

        static Trace trace;
        interfaces->engineTrace->clipRayToEntity({ localEyePosition, localEyePosition + direction }, 0x4600400B, entity, trace);
        if (trace.entity == entity)
            ++hits;

        if (hits >= hitsNeed)
            return true;

        if ((maxSeed - i + hits) < hitsNeed)
            return false;
    }
    return false;
}
*/