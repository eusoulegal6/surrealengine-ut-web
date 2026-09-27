// Adapted from dpjudas/SurrealEngine 597a08d199edfbdf530cc03993e3d8ec68831756.
#include "Precomp.h"
#include "UActor.h"
#include "ULevel.h"
#include "UClass.h"
#include "VM/ScriptCall.h"
#include "Engine.h"
#include <unordered_map>

UNavigationPoint* UPawn::SetRouteCache(const std::vector<UNavigationPoint*>& points)
{
	if (engine->LaunchInfo.engineVersion > 219)
	{
		auto cache = &RouteCache();
		for (size_t i = 0; i < (size_t)GetMemberProperty("RouteCache")->ArrayDimension; i++)
			cache[i] = (i < points.size()) ? points[i] : nullptr;
	}
	return !points.empty() ? points.front() : nullptr;
}

UActor* UPawn::PathSpecialHandling(const std::vector<UNavigationPoint*>& bestPath)
{
	IsInPathSpecialHandling = true;
	UActor* oldBestPoint = SetRouteCache(bestPath);
	if (!oldBestPoint)
	{
		IsInPathSpecialHandling = false;
		return nullptr;
	}

	UActor* bestPoint = oldBestPoint;

	if (oldBestPoint->IsEventEnabled(EventName::SpecialHandling))
	{
		bestPoint = UObject::Cast<UActor>(CallEvent(oldBestPoint, EventName::SpecialHandling, { ExpressionValue::ObjectValue(this) }).ToObject());
		if (!bCanDoSpecial())
			bestPoint = nullptr;
		SpecialGoal() = bestPoint;

		if (bestPoint && bestPoint != oldBestPoint)
		{
			if (!ActorReachable(bestPoint))
			{
				bestPoint = UObject::Cast<UActor>(FindPathToward(bestPoint, false));
			}
		}
	}
	else
	{
		if (SpecialGoal() == oldBestPoint)
			SpecialGoal() = nullptr;
	}

	IsInPathSpecialHandling = false;
	return bestPoint;
}

std::pair<std::vector<UNavigationPoint*>, int32_t> UPawn::FindPathToEndPoint(UNavigationPoint* start, int maxNodes)
{
	if ((start->bPlayerOnly() && !bIsPlayer()))
		return { {}, 0 };

	// If we can already reach the end point, just go there directly
	if (start->bEndPoint())
		return { { start }, 0 };

	struct Step
	{
		UNavigationPoint* navpoint;
		int prev;
		int32_t distance;
	};

	std::unordered_map<UNavigationPoint*, int32_t> shortestDistance;
	std::vector<Step> steps;
	std::vector<size_t> stepEnds;
	const std::vector<LevelReachSpec>& reachSpecs = XLevel()->ReachSpecs;

	int radius = (int)CollisionRadius();
	int height = (int)CollisionHeight();

	// Search through the nav node links until we find an end point

	int prevStep = -1;
	UNavigationPoint* current = start;
	while (steps.size() < (size_t)maxNodes)
	{
		if (!current->bEndPoint())
		{
			for (int link = 0; link < current->GetMemberProperty("upstreamPaths")->ArrayDimension; link++)
			{
				int specIndex = (&current->upstreamPaths())[link];
				if (specIndex < 0 || (size_t)specIndex >= reachSpecs.size())
					break;
				const LevelReachSpec& reachSpec = reachSpecs[specIndex];

				// Note: startActor instead of endActor because upstreamPaths is the reverse travel direction
				UNavigationPoint* endActor = reachSpec.startActor;
				if (!endActor) continue;

				if (reachSpec.collisionRadius < radius || reachSpec.collisionHeight < height || reachSpec.bPruned)
					continue; // Skip nav node links that we can't pass through

				if (endActor->bPlayerOnly() && !bIsPlayer())
					continue; // Skip nav nodes only for the player if we aren't one

				// To do: check reachFlags

				// How far have we travelled so far?
				int32_t distance = reachSpec.distance;
				if (prevStep >= 0)
					distance += steps[prevStep].distance;

				// Is this distance shorter than last time we reached this point?
				int32_t& pointDistance = shortestDistance[endActor];
				if (pointDistance == 0 || distance < pointDistance)
				{
					// Yes. Track this path and reject any future paths going through here that are longer.
					pointDistance = distance;
					if (endActor->bEndPoint())
						stepEnds.push_back(steps.size());
					steps.push_back({ endActor, prevStep, distance });
				}
			}
		}

		prevStep++;
		if (prevStep == steps.size())
			break;

		current = steps[prevStep].navpoint;
	}

	if (stepEnds.empty())
		return { {}, 0 };

	std::sort(stepEnds.begin(), stepEnds.end(), [&](size_t a, size_t b) { return steps[a].distance < steps[b].distance; });

	// Extract the final path:
	std::vector<UNavigationPoint*> path;
	int currentStep = (int)stepEnds.front();
	while (currentStep >= 0)
	{
		const Step& step = steps[currentStep];

		// Skip path parts we already are touching
		float minDist = (float)radius;
		vec3 d = step.navpoint->Location() - Location();
		float heightDiff = step.navpoint->Location().z - Location().z;
		if (dot(d, d) < minDist * minDist && std::abs(heightDiff) < (float)height)
		{
			path.clear();
		}
		else
		{
			path.push_back(step.navpoint);
		}

		currentStep = step.prev;
	}
	path.push_back(start);

	return { path, steps[stepEnds.front()].distance };
}

void UPawn::ClearPaths()
{
	for (UNavigationPoint* cur = Level()->NavigationPointList(); cur; cur = cur->nextNavigationPoint())
	{
		cur->bEndPoint() = false;
		if (engine->LaunchInfo.engineVersion >= 400)
		{
			if (cur->bSpecialCost())
				cur->cost() = CallEvent(cur, "SpecialCost", { ExpressionValue::ObjectValue(this) }).ToInt();
			else
				cur->cost() = cur->ExtraCost();
		}
	}
}

UObject* UPawn::FindRandomDest()
{
	// Find initial navpoints reachable from our location
	int maxActorReachableCalls = 8; // upper bound for how expensive this can get
	vec3 eyePos = Location();
	eyePos.z += BaseEyeHeight();
	std::vector<UNavigationPoint*> reachablePoints;
	for (UNavigationPoint* navPoint = Level()->NavigationPointList(); navPoint && reachablePoints.size() < maxActorReachableCalls; navPoint = navPoint->nextNavigationPoint())
	{
		if (navPoint->bPlayerOnly() && !bIsPlayer())
			continue; // Skip nav nodes only for the player if we aren't one

		float maxDist = 1000.0;
		vec3 d = navPoint->Location() - Location();
		if (dot(d, d) > maxDist * maxDist)
			continue; // Ignore things too far away

		if (!ActorReachable(navPoint))
			continue;

		navPoint->bEndPoint() = true;
		reachablePoints.push_back(navPoint);
	}

	if (reachablePoints.empty())
		return nullptr;

	// Add all navpoints reachable via reachspecs from what we can already reached
	const std::vector<LevelReachSpec>& reachSpecs = XLevel()->ReachSpecs;
	int radius = (int)CollisionRadius();
	int height = (int)CollisionHeight();
	for (size_t i = 0; i < reachablePoints.size(); i++)
	{
		UNavigationPoint* navPoint = reachablePoints[i];

		for (int link = 0; link < navPoint->GetMemberProperty("Paths")->ArrayDimension; link++)
		{
			int specIndex = (&navPoint->Paths())[link];
			if (specIndex < 0 || (size_t)specIndex >= reachSpecs.size())
				break;
			const LevelReachSpec& reachSpec = reachSpecs[specIndex];

			if (!reachSpec.endActor || reachSpec.endActor->bEndPoint())
				continue; // Already processed

			if (reachSpec.collisionRadius < radius || reachSpec.collisionHeight < height || reachSpec.bPruned)
				continue; // Skip nav node links that we can't pass through

			if (reachSpec.endActor->bPlayerOnly() && !bIsPlayer())
				continue; // Skip nav nodes only for the player if we aren't one

			// To do: check reachFlags

			reachSpec.endActor->bEndPoint() = true;
			reachablePoints.push_back(reachSpec.endActor);
		}
	}

	// Pick a random point from our candidates
	float randomValue = rand() / (float)RAND_MAX;
	int index = (int)std::round(randomValue * (float)(reachablePoints.size() - 1));
	return reachablePoints[index];
}

UObject* UPawn::FindPathTo(const vec3& aPoint, bool bSinglePath)
{
	return FindPathToward(FindClosestNavPoint(aPoint), bSinglePath);
}

bool UPawn::MarkReachableNavEndPoints()
{
	int maxActorReachableCalls = 8; // upper bound for how expensive this can get
	int endPointsFound = 0;
	for (UNavigationPoint* navPoint = Level()->NavigationPointList(); navPoint; navPoint = navPoint->nextNavigationPoint())
	{
		navPoint->bEndPoint() = false;

		if (endPointsFound < maxActorReachableCalls)
		{
			if (navPoint->bPlayerOnly() && !bIsPlayer())
				continue; // Skip nav nodes only for the player if we aren't one

			float maxDist = 1000.0;
			vec3 d = navPoint->Location() - Location();
			if (dot(d, d) > maxDist * maxDist)
				continue; // Ignore things too far away

			if (!ActorReachable(navPoint))
				continue;

			navPoint->bEndPoint() = true;
			endPointsFound++;
		}
	}

	return endPointsFound > 0;
}

UObject* UPawn::FindPathToward(UObject* anActor, bool singlePath)
{
	if (auto aNavPoint = UObject::TryCast<UNavigationPoint>(anActor))
	{
		if (!MarkReachableNavEndPoints())
			return SetRouteCache({});
		auto path = FindPathToEndPoint(aNavPoint, 1000).first;
		// SpecialHandling may request a route to a lift trigger or marker.
		// Suppress recursive script handling, but still find that route.
		if (IsInPathSpecialHandling)
			return SetRouteCache(path);
		return PathSpecialHandling(path);
	}
	else if (auto actor = UObject::TryCast<UActor>(anActor))
	{
		return FindPathToward(FindClosestNavPoint(actor->Location()), singlePath);
	}
	else
	{
		return SetRouteCache({});
	}
}

UNavigationPoint* UPawn::FindClosestNavPoint(vec3 location)
{
	// Order nav points by distance
	std::vector<std::pair<UNavigationPoint*, float>> navPoints;
	for (UNavigationPoint* navPoint = Level()->NavigationPointList(); navPoint; navPoint = navPoint->nextNavigationPoint())
	{
		if (navPoint->bPlayerOnly() && !bIsPlayer())
			continue; // Skip nav nodes only for the player if we aren't one

		float maxDist = 500;
		vec3 d = navPoint->Location() - location;
		float distsqr = dot(d, d);
		if (distsqr > maxDist * maxDist)
			continue; // Ignore things too far away

		navPoints.push_back({ navPoint, distsqr });
	}

	std::sort(navPoints.begin(), navPoints.end(), [](const auto& a, const auto& b) { return a.second < b.second; });

	size_t maxTraces = 4; // upper bound for how expensive this can get
	navPoints.resize(std::min(navPoints.size(), maxTraces));

	// Find the first reachable nav point
	for (auto& p : navPoints)
	{
		vec3 eyePos = p.first->Location();
		eyePos.z += BaseEyeHeight();
		if (FastTrace(location, eyePos))
			return p.first;
	}
	return nullptr;
}

UObject* UPawn::FindBestInventoryPath(bool predictRespawns, float& outBestWeight)
{
	if (!MarkReachableNavEndPoints())
	{
		outBestWeight = 0.0f;
		return SetRouteCache({});
	}

	float bestWeight = 0.0f;
	UInventorySpot* bestSpot = nullptr;
	std::vector<UNavigationPoint*> bestPath;

	for (UNavigationPoint* navPoint = Level()->NavigationPointList(); navPoint; navPoint = navPoint->nextNavigationPoint())
	{
		auto invSpot = UObject::TryCast<UInventorySpot>(navPoint);
		if (!invSpot)
			continue;
		auto inv = invSpot->markedItem();
		if (!inv)
			continue;

		if (inv->GetStateName() != "PickUp")
			continue;

		float desire = CallEvent(inv, "BotDesireability", { ExpressionValue::ObjectValue(this) }).ToFloat();
		if (desire > 0.0f)
		{
			auto [path, pathDist] = FindPathToEndPoint(invSpot, 1000);
			if (path.empty()) continue;

			// To do: how to take path costs into account?
			//int cost = 0;
			//for (auto nav : path)
			//	cost += nav->cost();

			float distance = std::max((float)pathDist, 1.0f);
			float weight = desire / distance;

			if (!bestSpot || weight > bestWeight)
			{
				bestSpot = invSpot;
				bestWeight = weight;
				bestPath = std::move(path);
			}
		}
	}

	if (bestSpot)
	{
		outBestWeight = bestWeight;
		return PathSpecialHandling(bestPath);
	}
	else
	{
		outBestWeight = 0.0f;
		return SetRouteCache({});
	}
}

// Native support for the original Bot falling/jump states.
vec3 UPawn::EAdjustJump()
{
	vec3 delta = Focus() - Location();
	if (dot(delta, delta) < 0.001f) delta = Destination() - Location();
	float distance = length(delta.xy());
	vec3 velocity(0.0f, 0.0f, JumpZ());
	if (distance < 0.001f) return velocity;
	auto* zone = FootRegion().Zone;
	float gravity = zone ? -zone->ZoneGravity().z : 980.0f;
	float discriminant = JumpZ() * JumpZ() - 2.0f * gravity * delta.z;
	float flightTime = (gravity > 0.001f && discriminant >= 0.0f)
		? (JumpZ() + std::sqrt(discriminant)) / gravity : 0.0f;
	float speed = flightTime > 0.001f ? std::min(distance / flightTime, GroundSpeed()) : GroundSpeed();
	velocity.x = delta.x * (speed / distance);
	velocity.y = delta.y * (speed / distance);
	return velocity;
}

// Based on the step/settle approach in SurrealEngine UPawn_ReachWalking.cpp
// (597a08d1). Probe the collision cylinder without moving the real pawn or
// sending touch/zone events. Visibility alone cannot prove a walkable route.
bool UPawn::WalkReachable(const vec3& goal, float goalRadius, float maxDrop)
{
	struct RestoreLocation
	{
		vec3& location;
		vec3 saved;
		~RestoreLocation() { location = saved; }
	} restore{ Location(), Location() };

	const float gravitySign = Region().Zone && Region().Zone->ZoneGravity().z > 0.0f ? 1.0f : -1.0f;
	const float stepLength = std::max(CollisionRadius() * 2.0f, 8.0f);
	const float stepHeight = std::max(MaxStepHeight(), 0.0f);
	const float radius = std::max(goalRadius, 1.0f);
	const vec3 up(0.0f, 0.0f, -gravitySign);

	for (int iteration = 0; iteration < 64; iteration++)
	{
		vec3 toGoal = goal - Location();
		toGoal.z = 0.0f;
		float distance = length(toGoal);
		if (distance <= radius)
			return std::abs(goal.z - Location().z) <= CollisionHeight();

		vec3 start = Location();
		vec3 move = toGoal * (std::min(stepLength, distance) / distance);
		CollisionHit hit = TryMove(move, true);
		Location() += move * hit.Fraction;
		float raised = 0.0f;
		if (hit.Fraction < 1.0f)
		{
			vec3 remaining = move * (1.0f - hit.Fraction);
			CollisionHit ceiling = TryMove(up * stepHeight, true);
			raised = stepHeight * ceiling.Fraction;
			Location() += up * raised;
			hit = TryMove(remaining, true);
			Location() += remaining * hit.Fraction;
		}

		// Sample the floor after every cylinder-width step, so a gap cannot be
		// mistaken for a bridge merely because the far endpoint is visible.
		vec3 down = -up * (raised + std::max(maxDrop, stepHeight) + 2.0f);
		CollisionHit floor = TryMove(down, true);
		if (floor.Fraction >= 1.0f || dot(floor.Normal, up) < 0.7071f)
			return false;
		Location() += down * floor.Fraction;

		auto* zone = FindRegion(-up * CollisionHeight()).Zone;
		if (zone && ((zone->bPainZone() && zone->DamageType() != ReducedDamageType()) || (zone->bWaterZone() && !bCanSwim())))
			return false;

		vec2 progress = Location().xy() - start.xy();
		if (dot(progress, progress) < 0.01f)
			return false;
	}
	return false;
}

// Implements Engine.Pawn's native contract: jump a low obstruction if the
// cylinder clears it, otherwise try perpendicular routes with floor checks.
bool UPawn::PickWallAdjust()
{
	if (Physics() != PHYS_Walking)
		return false;

	vec2 direction = Acceleration().xy();
	if (dot(direction, direction) < 0.001f)
		direction = (Destination() - Location()).xy();
	if (dot(direction, direction) < 0.001f)
		return false;
	vec3 forward(normalize(direction), 0.0f);
	const float distance = std::max(CollisionRadius() * 2.0f, 32.0f);
	vec3 forwardStep = forward * distance;

	if (bCanJump() && JumpZ() > 0.0f && TryMove(forwardStep, true).Fraction < 1.0f)
	{
		const float gravity = Region().Zone ? -Region().Zone->ZoneGravity().z : 980.0f;
		const float jumpHeight = gravity > 0.0f ? std::min(CollisionHeight(), JumpZ() * JumpZ() / (2.0f * gravity)) : 0.0f;
		bool canJump = false;
		{
			struct RestoreLocation
			{
				vec3& location;
				vec3 saved;
				~RestoreLocation() { location = saved; }
			} restore{ Location(), Location() };
			vec3 up(0.0f, 0.0f, jumpHeight);
			if (jumpHeight > MaxStepHeight() && TryMove(up, true).Fraction == 1.0f)
			{
				Location() += up;
				if (TryMove(forwardStep, true).Fraction == 1.0f)
				{
					Location() += forwardStep;
					vec3 down(0.0f, 0.0f, -jumpHeight - MaxStepHeight() - 2.0f);
					CollisionHit floor = TryMove(down, true);
					if (floor.Fraction < 1.0f && floor.Normal.z >= 0.7071f)
					{
						Location() += down * floor.Fraction;
						auto* zone = FindRegion(vec3(0.0f, 0.0f, -CollisionHeight())).Zone;
						canJump = !zone || ((!zone->bPainZone() || zone->DamageType() == ReducedDamageType()) && (!zone->bWaterZone() || bCanSwim()));
					}
				}
			}
		}
		if (canJump)
		{
			bFromWall() = false;
			Velocity().z = JumpZ();
			SetPhysics(PHYS_Falling);
			// The original script still needs its destination after landing.
			return true;
		}
	}

	vec3 side(forward.y, -forward.x, 0.0f);
	for (float sign : { 1.0f, -1.0f })
	{
		vec3 candidate = Location() + side * (distance * sign);
		if (WalkReachable(candidate, 1.0f, MaxStepHeight()))
		{
			bFromWall() = true;
			Destination() = candidate;
			return true;
		}
	}
	return false;
}
