#pragma once

// ---------------------------------------------------------------------------
// BotAI - combat AI for server bots (henchmen / bosses / NPCs).
//
// Ported from "Gameserver 12.41 with bots" (Bots.h: Bot2 / TickBots) into
// Project Reboot 3.0's reflection idiom. The original relied on a generated
// SDK and hard coded, build specific addresses. This version is version
// agnostic: every interaction with the game goes through a UFunction resolved
// at runtime (ProcessEvent) or an offset resolved at runtime, so it works on
// any build that exposes the Phoebe AI controller (Chapter 2 era).
//
// The bots themselves are still spawned by the real game bot manager
// (UFortServerBotManagerAthena::SpawnBotHook -> FortAthenaMutator_Bots::SpawnBot)
// which gives them a working AI controller, behaviour tree and inventory.
// This module simply tracks each spawned bot and drives the combat layer on
// top of that: acquire a target, aim at it, chase it and fire bursts.
// ---------------------------------------------------------------------------

#include <cmath>

#include "reboot.h"
#include "Actor.h"
#include "Controller.h"
#include "GameplayStatics.h"
#include "FortPlayerPawnAthena.h"
#include "FortPlayerControllerAthena.h"
#include "FortPlayerStateAthena.h"
#include "FortGameModeAthena.h"
#include "FortGameStateAthena.h"
#include "FortWeapon.h"
#include "KismetMathLibrary.h"

namespace BotAI
{
	// Toggle so the feature can be disabled at runtime without touching spawning.
	static inline bool bEnabled = true;

	// ---------------------------------------------------------------------
	// Reflected primitives. Each one null checks its UFunction so that on a
	// build where the function does not exist it simply no-ops instead of
	// crashing.
	// ---------------------------------------------------------------------

	static FRotator FindLookAtRotation(const FVector& Start, const FVector& Target)
	{
		static auto fn = FindObject<UFunction>(L"/Script/Engine.KismetMathLibrary.FindLookAtRotation");

		struct { FVector Start; FVector Target; FRotator ReturnValue; } params{ Start, Target };

		if (fn)
			UKismetMathLibrary::StaticClass()->ProcessEvent(fn, &params);

		return params.ReturnValue;
	}

	static bool LineOfSightTo(AController* PC, AActor* Other)
	{
		static auto fn = FindObject<UFunction>(L"/Script/Engine.Controller.LineOfSightTo");

		if (!fn || !PC || !Other)
			return false;

		struct { AActor* Other; FVector ViewPoint; bool bAlternateChecks; bool ReturnValue; } params{ Other, FVector{}, true, false };

		PC->ProcessEvent(fn, &params);

		return params.ReturnValue;
	}

	// Move the pawn directly via movement input. This works for both player
	// pawns (driven by an AFortPlayerControllerAthena) and AI pawns, so the
	// same combat code drives lobby-drop bots and henchmen alike. No navmesh
	// or AIController is required.
	static void AddMovementInput(AActor* Pawn, const FVector& WorldDirection, float Scale = 1.f)
	{
		static auto fn = FindObject<UFunction>(L"/Script/Engine.Pawn.AddMovementInput");

		if (!fn || !Pawn)
			return;

		struct { FVector WorldDirection; float ScaleValue; bool bForce; } params{ WorldDirection, Scale, true };
		Pawn->ProcessEvent(fn, &params);
	}

	static FVector Normalize(const FVector& V)
	{
		float LengthSq = V.X * V.X + V.Y * V.Y + V.Z * V.Z;

		if (LengthSq <= 0.0001f)
			return FVector{};

		float InvLength = 1.f / sqrtf(LengthSq);
		return FVector{ (FVector::VectorDataType)(V.X * InvLength), (FVector::VectorDataType)(V.Y * InvLength), (FVector::VectorDataType)(V.Z * InvLength) };
	}

	static void SetControlRotation(AController* PC, const FRotator& Rotation)
	{
		if (!PC)
			return;

		static auto ControlRotationOffset = PC->GetOffset("ControlRotation", false);

		if (ControlRotationOffset != -1)
			PC->Get<FRotator>(ControlRotationOffset) = Rotation;
	}

	static void PawnStartFire(AActor* Pawn, int FireModeNum = 0)
	{
		static auto fn = FindObject<UFunction>(L"/Script/FortniteGame.FortPawn.PawnStartFire");

		if (!fn || !Pawn)
			return;

		struct { int FireModeNum; } params{ FireModeNum };
		Pawn->ProcessEvent(fn, &params);
	}

	static void PawnStopFire(AActor* Pawn, int FireModeNum = 0)
	{
		static auto fn = FindObject<UFunction>(L"/Script/FortniteGame.FortPawn.PawnStopFire");

		if (!fn || !Pawn)
			return;

		struct { int FireModeNum; } params{ FireModeNum };
		Pawn->ProcessEvent(fn, &params);
	}

	static void Jump(AActor* Pawn)
	{
		static auto fn = FindObject<UFunction>(L"/Script/Engine.Character.Jump");
		if (fn && Pawn) Pawn->ProcessEvent(fn);
	}

	static void StopJumping(AActor* Pawn)
	{
		static auto fn = FindObject<UFunction>(L"/Script/Engine.Character.StopJumping");
		if (fn && Pawn) Pawn->ProcessEvent(fn);
	}

	static void Crouch(AActor* Pawn)
	{
		static auto fn = FindObject<UFunction>(L"/Script/Engine.Character.Crouch");
		if (!fn || !Pawn) return;
		struct { bool bClientSimulation; } params{ false };
		Pawn->ProcessEvent(fn, &params);
	}

	static void UnCrouch(AActor* Pawn)
	{
		static auto fn = FindObject<UFunction>(L"/Script/Engine.Character.UnCrouch");
		if (!fn || !Pawn) return;
		struct { bool bClientSimulation; } params{ false };
		Pawn->ProcessEvent(fn, &params);
	}

	// ---------------------------------------------------------------------
	// Per-bot state.
	// ---------------------------------------------------------------------

	class Bot
	{
	public:
		AController*            PC = nullptr;   // really an AFortAthenaAIBotController
		AFortPlayerPawnAthena*  Pawn = nullptr;
		AActor*                 CurrentTarget = nullptr;
		uint64_t                tick_counter = 0;
		bool                    bIsFiring = false;
		bool                    bJumping = false;
	};

	static inline std::vector<Bot*> Bots{};

	// Called from the bot spawn hook once the game has created the pawn,
	// controller, inventory and behaviour tree for the NPC.
	static void Register(AFortPlayerPawnAthena* Pawn)
	{
		if (!bEnabled || !Pawn)
			return;

		auto PC = (AController*)Pawn->GetController();

		if (!PC)
			return;

		// Avoid double registration.
		for (auto existing : Bots)
		{
			if (existing->Pawn == Pawn)
				return;
		}

		auto bot = new Bot();
		bot->Pawn = Pawn;
		bot->PC = PC;
		Bots.push_back(bot);

		LOG_INFO(LogBots, "Registered combat bot ({} total).", (int)Bots.size());
	}

	// ---------------------------------------------------------------------
	// Target acquisition. Version agnostic: walk the alive players, skip
	// downed / dead / same-team pawns, pick the closest one we can see.
	// ---------------------------------------------------------------------

	static AActor* FindBestTarget(Bot* bot)
	{
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		if (!GameMode)
			return nullptr;

		auto BotPS = Cast<AFortPlayerStateAthena>(bot->PC->GetPlayerState());
		const uint8 BotTeam = BotPS ? BotPS->GetTeamIndex() : 0;

		AActor* Best = nullptr;
		float   BestDistance = 0.f;
		bool    bSet = false;

		auto& Alive = GameMode->GetAlivePlayers();

		for (int i = 0; i < Alive.Num(); ++i)
		{
			auto Controller = Alive.at(i);

			if (!Controller)
				continue;

			auto TargetPawn = Controller->GetMyFortPawn();

			if (!TargetPawn || TargetPawn->IsActorBeingDestroyed() || TargetPawn->IsDBNO())
				continue;

			if (TargetPawn == bot->Pawn)
				continue;

			auto TargetPS = Cast<AFortPlayerStateAthena>(Controller->GetPlayerState());

			if (BotPS && TargetPS && TargetPS->GetTeamIndex() == BotTeam)
				continue; // never shoot a team mate

			float Distance = bot->Pawn->GetDistanceTo(TargetPawn);

			if (Distance > 6000.f) // ~60m aggro range
				continue;

			if (!LineOfSightTo(bot->PC, TargetPawn))
				continue;

			if (!bSet || Distance < BestDistance)
			{
				bSet = true;
				BestDistance = Distance;
				Best = TargetPawn;
			}
		}

		return Best;
	}

	// ---------------------------------------------------------------------
	// Combat tick for a single bot.
	// ---------------------------------------------------------------------

	static void TickBot(Bot* bot)
	{
		bot->tick_counter++;

		// Drop a stale target.
		if (bot->CurrentTarget)
		{
			auto TargetPawn = Cast<AFortPlayerPawnAthena>(bot->CurrentTarget);

			if (!TargetPawn || TargetPawn->IsActorBeingDestroyed() || TargetPawn->IsDBNO()
				|| bot->Pawn->GetDistanceTo(bot->CurrentTarget) > 7000.f)
			{
				bot->CurrentTarget = nullptr;
			}
		}

		// Re-acquire periodically (and whenever we have nothing).
		if (!bot->CurrentTarget || (bot->tick_counter % 30 == 0))
		{
			if (auto NewTarget = FindBestTarget(bot))
				bot->CurrentTarget = NewTarget;
		}

		// Nothing to fight: let the behaviour tree keep patrolling.
		if (!bot->CurrentTarget)
		{
			if (bot->bIsFiring)
			{
				PawnStopFire(bot->Pawn);
				bot->bIsFiring = false;
			}
			return;
		}

		auto BotPos = bot->Pawn->GetActorLocation();
		auto TargetPos = bot->CurrentTarget->GetActorLocation();
		float Distance = bot->Pawn->GetDistanceTo(bot->CurrentTarget);
		bool  bHasLOS = LineOfSightTo(bot->PC, bot->CurrentTarget);

		// Aim, refreshing a little spread every so often so it is not pixel
		// perfect (mirrors the original's randomised aim offset).
		FVector AimPos = TargetPos;

		if (bot->tick_counter % 15 == 0)
		{
			const float Spread = 2200.f;
			AimPos.X += (rand() % (int)(Spread * 2)) - Spread;
			AimPos.Y += (rand() % (int)(Spread * 2)) - Spread;
			AimPos.Z += (rand() % (int)(Spread * 2)) - Spread;
		}

		auto LookAt = FindLookAtRotation(BotPos, AimPos);
		SetControlRotation(bot->PC, LookAt);

		auto MoveDir = Normalize(TargetPos - BotPos);

		// No line of sight: stop firing and chase toward the target.
		if (!bHasLOS)
		{
			if (bot->bIsFiring)
			{
				PawnStopFire(bot->Pawn);
				bot->bIsFiring = false;
			}

			AddMovementInput(bot->Pawn, MoveDir, 1.f);
			return;
		}

		// In sight: close the distance, otherwise hold and strafe.
		if (Distance > 750.f)
		{
			AddMovementInput(bot->Pawn, MoveDir, 1.f);
		}
		else
		{
			// Occasional crouch / jump flavour while engaging up close.
			if ((bot->tick_counter % 90) == 0)
			{
				Crouch(bot->Pawn);
			}
			else if ((bot->tick_counter % 90) == 45)
			{
				UnCrouch(bot->Pawn);
			}
		}

		// Burst fire: ~40 ticks firing, ~25 ticks resting.
		if (bot->Pawn->GetCurrentWeapon())
		{
			const bool bShouldFire = (bot->tick_counter % 65) < 40;

			if (bShouldFire && !bot->bIsFiring)
			{
				PawnStartFire(bot->Pawn);
				bot->bIsFiring = true;
			}
			else if (!bShouldFire && bot->bIsFiring)
			{
				PawnStopFire(bot->Pawn);
				bot->bIsFiring = false;
			}
		}
		else if (bot->bIsFiring)
		{
			PawnStopFire(bot->Pawn);
			bot->bIsFiring = false;
		}
	}

	// ---------------------------------------------------------------------
	// Global tick, called from UNetDriver::TickFlushHook. Cleans up bots
	// whose pawn has died and ticks the rest.
	// ---------------------------------------------------------------------

	static void TickAll()
	{
		if (!bEnabled || Bots.empty())
			return;

		if (!GetWorld() || !GetWorld()->GetGameMode())
			return;

		for (size_t i = 0; i < Bots.size(); )
		{
			auto bot = Bots[i];

			const bool bDead = !bot->PC || bot->PC->IsActorBeingDestroyed()
				|| !bot->Pawn || bot->Pawn->IsActorBeingDestroyed()
				|| bot->Pawn->GetHealth() <= 0.f;

			if (bDead)
			{
				// Native henchman / boss death handling drops their loot for
				// us because the pawn was spawned through the real bot manager.
				delete bot;
				Bots.erase(Bots.begin() + i);
				continue;
			}

			// While downed do nothing but keep tracking.
			if (!bot->Pawn->IsDBNO())
				TickBot(bot);

			++i;
		}
	}
}
