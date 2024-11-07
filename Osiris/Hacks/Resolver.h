
#pragma once

#include "Animations.h"

#include "../SDK/GameEvent.h"
#include "../SDK/Entity.h"

constexpr int CACHE_SIZE = 2;
constexpr int YAW_CACHE_SIZE = 8;
constexpr auto MAX_TICKS = 3;


#define RAD2DEG(radian) (float)radian * (180.f / (float)M_PI)
#define DEG2RAD(degree) (float)degree * ((float)M_PI / 180.f)

namespace Resolver
{
	void reset() noexcept;

	void processMissedShots() noexcept;
	void saveRecord(int playerIndex, float playerSimulationTime) noexcept;
	void getEvent(GameEvent* event) noexcept;

	void runPreUpdate(Animations::Players player, Entity* entity) noexcept;
	void runPostUpdate(Animations::Players player, Entity* entity) noexcept;

	void updateEventListeners(bool forceRemove = false) noexcept;

	void detect_side(Entity* e, int side);
	void setup_detect(Animations::Players player, Entity* entity);
	void ResolveEntity(Animations::Players player, Entity* entity);


	struct SnapShot
	{
		Animations::Players player;
		const Model* model{ };
		Vector eyePosition{};
		Vector bulletImpact{};
		bool gotImpact{ false };
		float time{ -1 };
		int playerIndex{ -1 };
		int backtrackRecord{ -1 };
	};
}

/*

#pragma once

#include "Animations.h"

#include "../SDK/GameEvent.h"
#include "../SDK/Entity.h"

constexpr int CACHE_SIZE = 2;
constexpr int YAW_CACHE_SIZE = 8;
constexpr auto MAX_TICKS = 3;


#define RAD2DEG(radian) (float)radian * (180.f / (float)M_PI)
#define DEG2RAD(degree) (float)degree * ((float)M_PI / 180.f)

namespace Resolver
{
	void reset() noexcept;

	void processMissedShots() noexcept;
	void saveRecord(int playerIndex, float playerSimulationTime) noexcept;
	void getEvent(GameEvent* event) noexcept;

	void runPreUpdate(Animations::Players player, Animations::Players prev_player, Entity* entity) noexcept;
	void runPostUpdate(Animations::Players player, Animations::Players prev_player, Entity* entity) noexcept;

	void updateEventListeners(bool forceRemove = false) noexcept;

	//void detect_side(Entity* entity, int side);
	void resolve_entity(const Animations::Players& player, Animations::Players prev_record, Entity* entity);


	struct SnapShot
	{
		Animations::Players player;
		const Model* model{ };
		Vector eyePosition{};
		Vector bulletImpact{};
		bool gotImpact{ false };
		float time{ -1 };
		int playerIndex{ -1 };
		int backtrackRecord{ -1 };
	};
}

*/