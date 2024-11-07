#include "AimbotFunctions.h"
#include "Animations.h"
#include "Resolver.h"

#include "../Logger.h"

#include "../SDK/GameEvent.h"

#include <numeric>

#define TIME_TO_TICKS( dt )		( (int)( 0.5 + (float)(dt) / GlobalVars->intervalPerTick ) )
#define TICKS_TO_TIME( t )		( GlobalVars->intervalPerTick *( t ) )

float desyncAng{ 0 };

float get_backward_side(Entity* entity) {
	if (!entity->isAlive())
		return -1.f;
	float result = Helpers::angleDiff(localPlayer->origin().y, entity->origin().y);
	return result;
}
float get_angle(Entity* entity) {
	return Helpers::angleNormalize(entity->eyeAngles().y);
}
float get_foword_yaw(Entity* entity) {
	return Helpers::angleNormalize(get_backward_side(entity) - 180.f);
}

float FindAvgYaw(Entity* current) {
	float sin_sum = 0.f;
	float cos_sum = 0.f;
	float eyeYaw = current->eyeAngles().y;

	sin_sum += std::sinf(DEG2RAD(eyeYaw));
	cos_sum += std::cosf(DEG2RAD(eyeYaw));

	return RAD2DEG(std::atan2f(sin_sum, cos_sum));
}

float angle_diff(float dst, float src) {
	float Delta = dst - src;

	if (Delta < -180)
		Delta += 360;
	else if (Delta > 180)
		Delta -= 360;

	return Delta;
}

float arithmetic_average(float value1, float value2) {
	float result;
	result = (value1 + value2) / 2;
	return result;

}
std::deque<Resolver::SnapShot> snapshots;
static std::array<Animations::Players, 65> players{};

bool resolver = true;

void Resolver::reset() noexcept
{
	snapshots.clear();
}

void Resolver::saveRecord(int playerIndex, float playerSimulationTime) noexcept
{
	const auto entity = interfaces->entityList->getEntity(playerIndex);
	const auto player = Animations::getPlayer(playerIndex);
	if (!player.gotMatrix || !entity)
		return;

	SnapShot snapshot;
	snapshot.player = player;
	snapshot.playerIndex = playerIndex;
	snapshot.eyePosition = localPlayer->getEyePosition();
	snapshot.model = entity->getModel();

	if (player.simulationTime == playerSimulationTime)
	{
		snapshots.push_back(snapshot);
		return;
	}

	for (int i = 0; i < static_cast<int>(player.backtrackRecords.size()); i++)
	{
		if (player.backtrackRecords.at(i).simulationTime == playerSimulationTime)
		{
			snapshot.backtrackRecord = i;
			snapshots.push_back(snapshot);
			return;
		}
	}
}

void Resolver::getEvent(GameEvent* event) noexcept
{
	if (!event || !localPlayer || interfaces->engine->isHLTV())
		return;

	switch (fnv::hashRuntime(event->getName())) {
	case fnv::hash("round_start"):
	{
		//Reset all
		auto players = Animations::setPlayers();
		if (players->empty())
			break;

		for (int i = 0; i < static_cast<int>(players->size()); i++)
		{
			players->at(i).misses = 0;
		}
		snapshots.clear();
		break;
	}
	case fnv::hash("player_death"):
	{
		//Reset player
		const auto playerId = event->getInt("userid");
		if (playerId == localPlayer->getUserId())
			break;

		const auto index = interfaces->engine->getPlayerForUserID(playerId);
		Animations::setPlayer(index)->misses = 0;
		break;
	}
	case fnv::hash("player_hurt"):
	{
		if (snapshots.empty())
			break;

		if (event->getInt("attacker") != localPlayer->getUserId())
			break;

		const auto hitgroup = event->getInt("hitgroup");
		if (hitgroup < HitGroup::Head || hitgroup > HitGroup::RightLeg)
			break;

		snapshots.pop_front(); //Hit somebody so dont calculate
		break;
	}
	case fnv::hash("bullet_impact"):
	{
		if (snapshots.empty())
			break;

		if (event->getInt("userid") != localPlayer->getUserId())
			break;

		auto& snapshot = snapshots.front();

		if (!snapshot.gotImpact)
		{
			snapshot.time = memory->globalVars->serverTime();
			snapshot.bulletImpact = Vector{ event->getFloat("x"), event->getFloat("y"), event->getFloat("z") };
			snapshot.gotImpact = true;
		}
		break;
	}
	default:
		break;
	}
	if (!resolver)
		snapshots.clear();
}

float build_server_abs_yaw(Entity* entity)
{
	float m_fl_goal_feet_yaw = 0;
	int sidecheck = 0;
	Resolver::detect_side(entity, sidecheck);
	m_fl_goal_feet_yaw = entity->eyeAngles().y + (entity->getMaxDesyncAngle() * sidecheck);
	return Helpers::normalizeYaw(m_fl_goal_feet_yaw);

}

void Resolver::setup_detect(Animations::Players player, Entity* entity) {

	// detect if player is using maximum desync.
	if (player.layers[3].cycle == 0.f)
	{
		if (player.layers[3].weight = 0.f)
		{
			player.extended = true;
		}
	}
	/* calling detect side */
	detect_side(entity, player.side);
	int side = player.side;
	/* bruting vars */
	float resolve_value = 50.f;
	static float brute = 0.f;
	float fl_max_rotation = entity->getMaxDesyncAngle();
	float fl_eye_yaw = entity->getAnimstate()->eyeYaw;
	float perfect_resolve_yaw = resolve_value;
	bool fl_foword = fabsf(Helpers::angleNormalize(get_angle(entity) - get_foword_yaw(entity))) < 90.f;
	int fl_shots = player.misses;

	/* clamp angle */
	if (fl_max_rotation < resolve_value) {
		resolve_value = fl_max_rotation;
	}

	/* detect if entity is using max desync angle */
	if (player.extended) {
		resolve_value = fl_max_rotation;
	}
	/* setup brting */
	if (fl_shots == 0) {
		brute = perfect_resolve_yaw * (fl_foword ? -side : side);
	}
	else {
		switch (fl_shots % 3) {
		case 0: {
			brute = perfect_resolve_yaw * (fl_foword ? -side : side);
		} break;
		case 1: {
			brute = perfect_resolve_yaw * (fl_foword ? side : -side);
		} break;
		case 2: {
			brute = 0;
		} break;
		}
	}

	/* fix goalfeet yaw */
	entity->getAnimstate()->footYaw = fl_eye_yaw + brute;
}

void Resolver::processMissedShots() noexcept
{
	if (!resolver)
	{
		snapshots.clear();
		return;
	}

	if (!localPlayer)
	{
		snapshots.clear();
		return;
	}

	if (snapshots.empty())
		return;

	if (snapshots.front().time == -1) //Didnt get data yet
		return;

	auto snapshot = snapshots.front();
	snapshots.pop_front(); //got the info no need for this
	const auto& time = localPlayer->isAlive() ? localPlayer->tickBase() * memory->globalVars->intervalPerTick : memory->globalVars->currenttime;
	if (fabs(time - snapshot.time) > 1.f)
	{
		if (snapshot.gotImpact)
			Logger::addLog("Missed shot due to ping");
		else
			Logger::addLog("Missed shot due to server rejection");
		snapshots.clear();
		return;
	}
	if (!snapshot.player.gotMatrix)
		return;

	const auto entity = interfaces->entityList->getEntity(snapshot.playerIndex);
	if (!entity)
		return;

	const Model* model = snapshot.model;
	if (!model)
		return;

	StudioHdr* hdr = interfaces->modelInfo->getStudioModel(model);
	if (!hdr)
		return;

	StudioHitboxSet* set = hdr->getHitboxSet(0);
	if (!set)
		return;

	const auto angle = AimbotFunction::calculateRelativeAngle(snapshot.eyePosition, snapshot.bulletImpact, Vector{ });
	const auto end = snapshot.bulletImpact + Vector::fromAngle(angle) * 2000.f;

	const auto matrix = snapshot.backtrackRecord <= -1 ? snapshot.player.matrix.data() : snapshot.player.backtrackRecords.at(snapshot.backtrackRecord).matrix;

	bool resolverMissed = false;
	for (int hitbox = 0; hitbox < Hitboxes::Max; hitbox++)
	{
		if (AimbotFunction::hitboxIntersection(matrix, hitbox, set, snapshot.eyePosition, end))
		{
			resolverMissed = true;
			std::string missed = std::string("Missed shot on ") + entity->getPlayerName() + std::string(" due to resolver");
			std::string missedBT = std::string("Missed shot on ") + entity->getPlayerName() + std::string(" due to invalid backtrack tick [") + std::to_string(snapshot.backtrackRecord) + "]";
			std::string missedPred = std::string("Missed shot on ") + entity->getPlayerName() + std::string(" due to prediction error");
			std::string missedJitter = std::string("Missed shot on ") + entity->getPlayerName() + std::string(" due to jitter");
			if (snapshot.backtrackRecord == 1 && config->backtrack.enabled)
				Logger::addLog(missedJitter);
			else if (snapshot.backtrackRecord > 1 && config->backtrack.enabled)
				Logger::addLog(missedBT);
			else
				Logger::addLog(missed);
			Animations::setPlayer(snapshot.playerIndex)->misses++;
			break;
		}
	}
	if (!resolverMissed)
		Logger::addLog(std::string("Missed shot due to spread"));
}

float ResolveShot(Animations::Players player, Entity* entity) {
	/* fix unrestricted shot */
	float flPseudoFireYaw = Helpers::angleNormalize(Helpers::angleDiff(localPlayer->origin().y, player.matrix[8].origin().y));
	if (player.extended) {
		float flLeftFireYawDelta = fabsf(Helpers::angleNormalize(flPseudoFireYaw - (entity->eyeAngles().y + 58.f)));
		float flRightFireYawDelta = fabsf(Helpers::angleNormalize(flPseudoFireYaw - (entity->eyeAngles().y - 58.f)));

		return flLeftFireYawDelta > flRightFireYawDelta ? -58.f : 58.f;
	}
	else {
		float flLeftFireYawDelta = fabsf(Helpers::angleNormalize(flPseudoFireYaw - (entity->eyeAngles().y + 29.f)));
		float flRightFireYawDelta = fabsf(Helpers::angleNormalize(flPseudoFireYaw - (entity->eyeAngles().y - 29.f)));

		return flLeftFireYawDelta > flRightFireYawDelta ? -29.f : 29.f;
	}
}

void Resolver::ResolveEntity(Animations::Players player, Entity* entity) {	// get the players max rotation.
	float max_rotation = entity->getMaxDesyncAngle();
	int index = 0;
	float eye_yaw = entity->getAnimstate()->eyeYaw;
	bool extended = player.extended;
	if (!extended && fabs(max_rotation) > 60.f)
	{
		max_rotation = max_rotation / 1.8f;
	}

	// resolve shooting players separately.
	if (player.shot) {
		entity->getAnimstate()->footYaw = eye_yaw + ResolveShot(player, entity);
		return;
	}
	else {
		if (entity->velocity().length2D() <= 0.1) {
			float angle_difference = Helpers::angleDiff(eye_yaw, entity->getAnimstate()->footYaw);
			index = 2 * angle_difference <= 0.0f ? 1 : -1;
		}
		else
		{
			if (!((int)player.layers[12].weight * 1000.f) && entity->velocity().length2D() > 0.1) {

				auto m_layer_delta1 = abs(player.layers[6].playbackRate - player.oldlayers[6].playbackRate);
				auto m_layer_delta2 = abs(player.layers[6].playbackRate - player.oldlayers[6].playbackRate);
				auto m_layer_delta3 = abs(player.layers[6].playbackRate - player.oldlayers[6].playbackRate);

				if (m_layer_delta1 < m_layer_delta2
					|| m_layer_delta3 <= m_layer_delta2
					|| (signed int)(float)(m_layer_delta2 * 1000.0))
				{
					if (m_layer_delta1 >= m_layer_delta3
						&& m_layer_delta2 > m_layer_delta3
						&& !(signed int)(float)(m_layer_delta3 * 1000.0))
					{
						index = 1;
					}
				}
				else
				{
					index = -1;
				}
			}
		}
	}

	switch (player.misses % 3) {
	case 0: //default
		entity->getAnimstate()->footYaw = build_server_abs_yaw(entity) + max_rotation * index;
		break;
	case 1: //reverse
		entity->getAnimstate()->footYaw = build_server_abs_yaw(entity)+ max_rotation * -index;
		break;
	case 2: //middle
		entity->getAnimstate()->footYaw = build_server_abs_yaw(entity);
		break;
	}
}

void Resolver::runPreUpdate(Animations::Players player, Entity* entity) noexcept
{
	const auto misses = player.misses;
	if (!entity || !entity->isAlive())
		return;

	if (player.chokedPackets <= 0)
		return;
	if (!localPlayer || !localPlayer->isAlive())
		return;

	if (snapshots.empty())
		return;
	auto& snapshot = snapshots.front();
	Resolver::setup_detect(player, entity);
	Resolver::ResolveEntity(player, entity);
	desyncAng = entity->getAnimstate()->footYaw;
	auto animstate = entity->getAnimstate();
	animstate->footYaw = desyncAng;
	if (snapshot.player.workingangle != 0.f && fabs(desyncAng) > fabs(snapshot.player.workingangle))
	{
		if (snapshot.player.workingangle < 0.f && player.side == 1)
			snapshot.player.workingangle = fabs(snapshot.player.workingangle);
		else if (snapshot.player.workingangle > 0.f && player.side == -1)
			snapshot.player.workingangle = snapshot.player.workingangle * (-1.f);
		desyncAng = snapshot.player.workingangle;
		animstate->footYaw = desyncAng;
	}
}

void Resolver::runPostUpdate(Animations::Players player, Entity* entity) noexcept
{
	const auto misses = player.misses;

	if (!entity || !entity->isAlive())
		return;

	if (player.chokedPackets <= 0)
		return;
	if (!localPlayer || !localPlayer->isAlive())
		return;

	if (snapshots.empty())
		return;

	auto& snapshot = snapshots.front();
	auto animstate = entity->getAnimstate();
	Resolver::setup_detect(player, entity);
	Resolver::ResolveEntity(player, entity);
	desyncAng = animstate->footYaw;
	if (snapshot.player.workingangle != 0.f && fabs(desyncAng) > fabs(snapshot.player.workingangle))
	{
		if (snapshot.player.workingangle < 0.f && player.side == 1)
			snapshot.player.workingangle = fabs(snapshot.player.workingangle);
		else if (snapshot.player.workingangle > 0.f && player.side == -1)
			snapshot.player.workingangle = snapshot.player.workingangle * (-1.f);
		desyncAng = snapshot.player.workingangle;
		animstate->footYaw = desyncAng;
	}
}

void Resolver::detect_side(Entity* entity, int side) {  //ghetto and improper side detect
	
	if (!localPlayer) {
		side = 0;
	}
	if (!entity || !entity->isAlive()) {
		side = 0;
	}
	if (entity->team() == localPlayer->team()) {
		side = 0;
	}
	if (entity->moveType() == MoveType::NOCLIP || entity->moveType() == MoveType::LADDER) {
		side = 0;
	}
	const float prevEyeYaw = FindAvgYaw(entity);
	const float eyeYaw = entity->eyeAngles().y;
	const float lowerBodyYaw = entity->lby();
	float delta1 = Helpers::angleNormalize(eyeYaw - lowerBodyYaw);
	float delta2 = angle_diff(eyeYaw, prevEyeYaw);;
	float delta = arithmetic_average(delta1, delta2);
	if (delta > 0.f) {
		side = -1;
	}
	else if (delta < 0.f) {
		side = 1;
	}
	else
		side = 0;
}

void Resolver::updateEventListeners(bool forceRemove) noexcept
{
	class ImpactEventListener : public GameEventListener {
	public:
		void fireGameEvent(GameEvent* event) {
			getEvent(event);
		}
	};

	static ImpactEventListener listener;
	static bool listenerRegistered = false;

	if (resolver && !listenerRegistered) {
		interfaces->gameEventManager->addListener(&listener, "bullet_impact");
		listenerRegistered = true;
	}
	else if ((!resolver || forceRemove) && listenerRegistered) {
		interfaces->gameEventManager->removeListener(&listener);
		listenerRegistered = false;
	}
}




/*  made by lambada team

#include "AimbotFunctions.h"
#include "Animations.h"
#include "Resolver.h"

#include "../Logger.h"

#include "../SDK/GameEvent.h"

#include <numeric>
#include <DirectXMath.h>
#include <algorithm>

#define TIME_TO_TICKS( dt )		( (int)( 0.5 + (float)(dt) / GlobalVars->intervalPerTick ) )
#define TICKS_TO_TIME( t )		( GlobalVars->intervalPerTick *( t ) )

#define RAD2DEG(x) DirectX::XMConvertToDegrees( x )
#define DEG2RAD(x) DirectX::XMConvertToRadians( x )
#define M_PI 3.14159265358979323846
#define PI_F ( ( float )( M_PI ) )
#define M_RADPI 57.295779513082f

const float m_flAimYawMin = -58.f;
const float m_flAimYawMax = 58.f;

float FindAvgYaw(Entity* current) {
	float sin_sum = 0.f;
	float cos_sum = 0.f;
	float eyeYaw = current->eyeAngles().y;

	sin_sum += std::sinf(DEG2RAD(eyeYaw));
	cos_sum += std::cosf(DEG2RAD(eyeYaw));

	return RAD2DEG(std::atan2f(sin_sum, cos_sum));
}

float angle_diff(float dst, float src) {
	float Delta = dst - src;

	if (Delta < -180)
		Delta += 360;
	else if (Delta > 180)
		Delta -= 360;

	return Delta;
}

float arithmetic_average(float value1, float value2) {
	float result;
	result = (value1 + value2) / 2;
	return result;

}
std::deque<Resolver::SnapShot> snapshots;
static std::array<Animations::Players, 65> players{};

bool resolver = true;

void Resolver::reset() noexcept
{
	snapshots.clear();
}

void Resolver::saveRecord(int playerIndex, float playerSimulationTime) noexcept
{
	const auto entity = interfaces->entityList->getEntity(playerIndex);
	const auto player = Animations::getPlayer(playerIndex);
	if (!player.gotMatrix || !entity)
		return;

	SnapShot snapshot;
	snapshot.player = player;
	snapshot.playerIndex = playerIndex;
	snapshot.eyePosition = localPlayer->getEyePosition();
	snapshot.model = entity->getModel();

	if (player.simulationTime == playerSimulationTime)
	{
		snapshots.push_back(snapshot);
		return;
	}

	for (int i = 0; i < static_cast<int>(player.backtrackRecords.size()); i++)
	{
		if (player.backtrackRecords.at(i).simulationTime == playerSimulationTime)
		{
			snapshot.backtrackRecord = i;
			snapshots.push_back(snapshot);
			return;
		}
	}
}

void Resolver::getEvent(GameEvent* event) noexcept
{
	if (!event || !localPlayer || interfaces->engine->isHLTV())
		return;

	switch (fnv::hashRuntime(event->getName())) {
	case fnv::hash("round_start"):
	{
		//Reset all
		auto players = Animations::setPlayers();
		if (players->empty())
			break;

		for (int i = 0; i < static_cast<int>(players->size()); i++)
		{
			players->at(i).misses = 0;
		}
		snapshots.clear();
		break;
	}
	case fnv::hash("player_death"):
	{
		//Reset player
		const auto playerId = event->getInt("userid");
		if (playerId == localPlayer->getUserId())
			break;

		const auto index = interfaces->engine->getPlayerForUserID(playerId);
		Animations::setPlayer(index)->misses = 0;
		break;
	}
	case fnv::hash("player_hurt"):
	{
		if (snapshots.empty())
			break;

		if (event->getInt("attacker") != localPlayer->getUserId())
			break;

		const auto hitgroup = event->getInt("hitgroup");
		if (hitgroup < HitGroup::Head || hitgroup > HitGroup::RightLeg)
			break;

		snapshots.pop_front(); //Hit somebody so dont calculate
		break;
	}
	case fnv::hash("bullet_impact"):
	{
		if (snapshots.empty())
			break;

		if (event->getInt("userid") != localPlayer->getUserId())
			break;

		auto& snapshot = snapshots.front();

		if (!snapshot.gotImpact)
		{
			snapshot.time = memory->globalVars->serverTime();
			snapshot.bulletImpact = Vector{ event->getFloat("x"), event->getFloat("y"), event->getFloat("z") };
			snapshot.gotImpact = true;
		}
		break;
	}
	default:
		break;
	}
	if (!resolver)
		snapshots.clear();
}

void Resolver::processMissedShots() noexcept
{
	if (!resolver)
	{
		snapshots.clear();
		return;
	}

	if (!localPlayer)
	{
		snapshots.clear();
		return;
	}

	if (snapshots.empty())
		return;

	if (snapshots.front().time == -1) //Didnt get data yet
		return;

	auto snapshot = snapshots.front();
	snapshots.pop_front(); //got the info no need for this
	const auto& time = localPlayer->isAlive() ? localPlayer->tickBase() * memory->globalVars->intervalPerTick : memory->globalVars->currenttime;
	if (fabs(time - snapshot.time) > 1.f)
	{
		if (snapshot.gotImpact)
			Logger::addLog("Missed shot due to ping");
		else
			Logger::addLog("Missed shot due to server rejection");
		snapshots.clear();
		return;
	}
	if (!snapshot.player.gotMatrix)
		return;

	const auto entity = interfaces->entityList->getEntity(snapshot.playerIndex);
	if (!entity)
		return;

	const Model* model = snapshot.model;
	if (!model)
		return;

	StudioHdr* hdr = interfaces->modelInfo->getStudioModel(model);
	if (!hdr)
		return;

	StudioHitboxSet* set = hdr->getHitboxSet(0);
	if (!set)
		return;

	const auto angle = AimbotFunction::calculateRelativeAngle(snapshot.eyePosition, snapshot.bulletImpact, Vector{ });
	const auto end = snapshot.bulletImpact + Vector::fromAngle(angle) * 2000.f;

	const auto matrix = snapshot.backtrackRecord <= -1 ? snapshot.player.matrix.data() : snapshot.player.backtrackRecords.at(snapshot.backtrackRecord).matrix;

	bool resolverMissed = false;
	for (int hitbox = 0; hitbox < Hitboxes::Max; hitbox++)
	{
		if (AimbotFunction::hitboxIntersection(matrix, hitbox, set, snapshot.eyePosition, end))
		{
			resolverMissed = true;
			std::string missed = std::string("Missed shot on ") + entity->getPlayerName() + std::string(" due to resolver");
			std::string missedBT = std::string("Missed shot on ") + entity->getPlayerName() + std::string(" due to invalid backtrack tick [") + std::to_string(snapshot.backtrackRecord) + "]";
			std::string missedPred = std::string("Missed shot on ") + entity->getPlayerName() + std::string(" due to prediction error");
			std::string missedJitter = std::string("Missed shot on ") + entity->getPlayerName() + std::string(" due to jitter");
			if (snapshot.backtrackRecord == 1 && config->backtrack.enabled)
				Logger::addLog(missedJitter);
			else if (snapshot.backtrackRecord > 1 && config->backtrack.enabled)
				Logger::addLog(missedBT);
			else
				Logger::addLog(missed);
			Animations::setPlayer(snapshot.playerIndex)->misses++;
			break;
		}
	}
	if (!resolverMissed)
		Logger::addLog(std::string("Missed shot due to spread"));
}



void Resolver::runPreUpdate(Animations::Players player, Animations::Players prev_player, Entity* entity) noexcept
{
	if (!resolver)
		return;

	if (!entity || !entity->isAlive())
		return;

	if (player.chokedPackets <= 0)
		return;
	//resolve_entity(player, entity);
}

void Resolver::runPostUpdate(Animations::Players player, Animations::Players prev_record, Entity* entity) noexcept
{
	if (!resolver)
		return;

	if (!entity || !entity->isAlive())
		return;

	if (player.chokedPackets <= 0)
		return;

	resolve_entity(player, prev_record, entity);
}


//  attemtped fixes by lambdahook team
void Resolver::resolve_entity(const Animations::Players& player, Animations::Players prev_record, Entity* entity) {
	// get the players max rotation.
	if (entity->isBot())
		return;

	bool previous_valid = true;

	if (prev_record.simulationTime == -1 ) {
		previous_valid = false;
	}


	auto current_record = player.backtrackRecords.front();
	float finalFoolYaw = 0.f;
	float max_delta = std::clamp(entity->getMaxDesyncAngle(), 0.f, m_flAimYawMax);

	int missed_shots = player.misses;

	float m_flFootYaw = player.m_flLowerBodyYawTarget;
	float eye_yaw = player.eye_yaw;

	int side = 0;

	if (current_record.velocity.length2D() < 1.f || !previous_valid) {
		//player aint moving on this record, better do some funny math (can be improved using some logic getting the onshot desync angle with onshot detected, this and that)
		if (Helpers::angleDiff(eye_yaw, m_flFootYaw) > 35.0f)
		{
			side = 1;
		}
		else if (Helpers::angleDiff(eye_yaw, m_flFootYaw) < -35.0f)
		{
			side = -1;
		}
	}
	else {
		//player is moving so we can prob dome him, todo: animlayer and pose magic. substituted by standing routine (for now)
		if (Helpers::angleDiff(eye_yaw, m_flFootYaw) > 0.f)
		{
			side = 1;
		}
		else if (Helpers::angleDiff(eye_yaw, m_flFootYaw) < 0.f)
		{
			side = -1;
		}
	}

	float final_yaw = entity->getAnimstate()->footYaw;

	switch (player.misses % 3) {
	case 0: //default
		final_yaw = entity->getAnimstate()->footYaw + (side * max_delta);
		break;
	case 1: //reverse
		final_yaw = entity->getAnimstate()->footYaw - (side * max_delta);
		break;
	case 2: //middle;
		final_yaw = eye_yaw;
		break;
	default: break;
	}

	entity->getAnimstate()->footYaw = std::clamp(entity->getAnimstate()->footYaw, m_flAimYawMin, m_flAimYawMax);
}

void Resolver::updateEventListeners(bool forceRemove) noexcept
{
	class ImpactEventListener : public GameEventListener {
	public:
		void fireGameEvent(GameEvent* event) {
			getEvent(event);
		}
	};

	static ImpactEventListener listener;
	static bool listenerRegistered = false;

	if (resolver && !listenerRegistered) {
		interfaces->gameEventManager->addListener(&listener, "bullet_impact");
		listenerRegistered = true;
	}
	else if ((!resolver || forceRemove) && listenerRegistered) {
		interfaces->gameEventManager->removeListener(&listener);
		listenerRegistered = false;
	}
}



*/