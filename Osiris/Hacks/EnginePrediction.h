#pragma once

#include "../SDK/FrameStage.h"
#include "../SDK/Vector.h"

#include <array>

struct UserCmd;
struct Vector;

namespace EnginePrediction
{
	void reset() noexcept;

	void update() noexcept;
	void run(UserCmd* cmd) noexcept;

	void store() noexcept;
	void apply(FrameStage) noexcept;

	int getFlags() noexcept;
	Vector getVelocity() noexcept;
	bool isInPrediction() noexcept;

	struct NetvarData
	{
		int tickbase = -1;

		Vector aimPunchAngle{ };
		Vector aimPunchAngleVelocity{ };
		Vector baseVelocity{ };
		float duckAmount{ -1.f };
		float duckSpeed{ -1.f };
		float fallVelocity{ -1.f };
		float thirdPersonRecoil{ -1.f };
		Vector velocity{ };
		float velocityModifier{ -1.f };
		Vector viewPunchAngle{ };
		Vector viewOffset{ };
		Vector origin{ };
	};
}
