#pragma once

#include "FortGameModeAthena.h"
#include "OnlineReplStructs.h"
#include "FortAthenaAIBotController.h"
#include "BuildingContainer.h"
#include "botnames.h"
#include "globals.h"
#include "GameModeBase.h"
#include "GameplayStatics.h"
#include "FortWeaponItemDefinition.h"
#include "BotAI.h"

class BotPOI
{
	FVector CenterLocation;
	FVector Range; // this just has to be FVector2D
};

class BotPOIEncounter
{
public:
	int NumChestsSearched;
	int NumAmmoBoxesSearched;
	int NumPlayersEncountered;
};

// Where a match-filling bot is in its life cycle.
enum class EBotDropState : uint8_t
{
	Lobby,        // spawned, waiting in the lobby / warmup for the bus to fly
	WaitingDrop,  // aircraft phase started, counting down to its drop time
	Dropped       // dropped into the map, combat AI has taken over
};

class PlayerBot
{
public:
	static inline UClass* PawnClass = nullptr;
	static inline UClass* ControllerClass = nullptr;

	AController* Controller = nullptr; // This can be 1. AFortAthenaAIBotController OR AFortPlayerControllerAthena
	bool bIsAthenaController = false;
	AFortPlayerPawnAthena* Pawn = nullptr;
	AFortPlayerStateAthena* PlayerState = nullptr;
	BotPOIEncounter currentBotEncounter;
	int TotalPlayersEncountered;
	std::vector<BotPOI> POIsTraveled;
	float NextJumpTime = 1.0f;

	// Lobby -> bus -> drop life cycle (see Bots::Tick).
	EBotDropState DropState = EBotDropState::Lobby;
	float DropTime = 0.f;        // world time at which this bot leaves the bus
	float InvulnerableUntil = 0.f; // world time until which fall damage is ignored
	UFortWeaponItemDefinition* CombatWeapon = nullptr; // gun handed out so the bot can fight
	FGuid CombatWeaponGuid;

	// Finds a sensible automatic weapon to arm bots with. Version agnostic:
	// it scans the loaded ranged weapon definitions and prefers a standard
	// assault rifle, falling back to whatever ranged weapon exists.
	static UFortWeaponItemDefinition* FindBotWeapon()
	{
		static UFortWeaponItemDefinition* Cached = nullptr;

		if (Cached)
			return Cached;

		auto RangedClass = FindObject<UClass>(L"/Script/FortniteGame.FortWeaponRangedItemDefinition");

		if (!RangedClass)
			return nullptr;

		auto AllRanged = GetAllObjectsOfClass(RangedClass);
		UFortWeaponItemDefinition* Fallback = nullptr;

		for (int i = 0; i < AllRanged.size(); ++i)
		{
			auto Def = (UFortWeaponItemDefinition*)AllRanged.at(i);

			if (!Def)
				continue;

			auto Name = Def->GetName();

			// Skip ammo / default / blueprint base objects.
			if (Name.starts_with("Default__"))
				continue;

			if (!Fallback)
				Fallback = Def;

			if (Name.find("Assault_Auto") != std::string::npos && Name.find("Athena") != std::string::npos)
			{
				Cached = Def;
				break;
			}
		}

		if (!Cached)
			Cached = Fallback;

		return Cached;
	}

	void OnPlayerEncountered()
	{
		currentBotEncounter.NumPlayersEncountered++;
		TotalPlayersEncountered++;
	}

	void MoveToNewPOI()
	{

	}

	static bool ShouldUseAIBotController()
	{
		return false;
		return Fortnite_Version >= 11 && Engine_Version < 500;
	}

	static void InitializeBotClasses()
	{
		static auto BlueprintGeneratedClassClass = FindObject<UClass>(L"/Script/Engine.BlueprintGeneratedClass");

		if (!ShouldUseAIBotController())
		{
			PawnClass = FindObject<UClass>(L"/Game/Athena/PlayerPawn_Athena.PlayerPawn_Athena_C");
			ControllerClass = AFortPlayerControllerAthena::StaticClass();
		}
		else
		{
			PawnClass = LoadObject<UClass>(L"/Game/Athena/AI/Phoebe/BP_PlayerPawn_Athena_Phoebe.BP_PlayerPawn_Athena_Phoebe_C", BlueprintGeneratedClassClass);
			// ControllerClass = PawnClass->CreateDefaultObject()->GetAIControllerClass();
		}

		if (/* !ControllerClass
			|| */ !PawnClass
			)
		{
			LOG_ERROR(LogBots, "Failed to find a class for the bots!");
			return;
		}
	}

	static bool IsReadyToSpawnBot()
	{
		return PawnClass;
	}

	void SetupInventory()
	{
		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		if (!ShouldUseAIBotController()) // TODO REWRITE
		{
			AFortInventory** Inventory = nullptr;

			if (auto FortPlayerController = Cast<AFortPlayerController>(Controller))
			{
				Inventory = &FortPlayerController->GetWorldInventory();
			}
			else
			{
				if (auto FortAthenaAIBotController = Cast<AFortAthenaAIBotController>(Controller))
				{
					static auto InventoryOffset = Controller->GetOffset("Inventory");
					Inventory = Controller->GetPtr<AFortInventory*>(InventoryOffset);
				}
			}

			if (!Inventory)
			{
				LOG_ERROR(LogBots, "No inventory pointer!");

				Pawn->K2_DestroyActor();
				Controller->K2_DestroyActor();
				return;
			}

			static auto FortInventoryClass = FindObject<UClass>(L"/Script/FortniteGame.FortInventory"); // AFortInventory::StaticClass()
			*Inventory = GetWorld()->SpawnActor<AFortInventory>(FortInventoryClass, FTransform{}, CreateSpawnParameters(ESpawnActorCollisionHandlingMethod::AlwaysSpawn, false, Controller));

			if (!*Inventory)
			{
				LOG_ERROR(LogBots, "Failed to spawn Inventory!");

				Pawn->K2_DestroyActor();
				Controller->K2_DestroyActor();
				return;
			}

			(*Inventory)->GetInventoryType() = EFortInventoryType::World;

			if (auto FortPlayerController = Cast<AFortPlayerController>(Controller))
			{
				static auto bHasInitializedWorldInventoryOffset = FortPlayerController->GetOffset("bHasInitializedWorldInventory");
				FortPlayerController->Get<bool>(bHasInitializedWorldInventoryOffset) = true;
			}

			// if (false)
			{
				if (Inventory)
				{
					auto& StartingItems = GameMode->GetStartingItems();

					for (int i = 0; i < StartingItems.Num(); ++i)
					{
						auto& StartingItem = StartingItems.at(i, FItemAndCount::GetStructSize());

						// TODO: Check if it is FortSmartBuildingItemDefinition

						(*Inventory)->AddItem(StartingItem.GetItem(), nullptr, StartingItem.GetCount());
					}

					if (auto FortPlayerController = Cast<AFortPlayerController>(Controller))
					{
						UFortItem* PickaxeInstance = FortPlayerController->AddPickaxeToInventory();

						if (PickaxeInstance)
						{
							FortPlayerController->ServerExecuteInventoryItemHook(FortPlayerController, PickaxeInstance->GetItemEntry()->GetItemGuid());
						}
					}

					// Hand the bot a gun so it can actually fight once it lands.
					if (auto Weapon = FindBotWeapon())
					{
						auto Result = (*Inventory)->AddItem(Weapon, nullptr, 1, 999);

						if (Result.first.size() > 0 && Result.first.at(0))
						{
							CombatWeapon = Weapon;
							CombatWeaponGuid = Result.first.at(0)->GetItemEntry()->GetItemGuid();

							if (Pawn)
								Pawn->EquipWeaponDefinition(Weapon, CombatWeaponGuid);
						}
					}

					(*Inventory)->Update();
				}
			}
		}
	}

	void PickRandomLoadout()
	{
		auto AllHeroTypes = GetAllObjectsOfClass(FindObject<UClass>(L"/Script/FortniteGame.FortHeroType"));
		std::vector<UFortItemDefinition*> AthenaHeroTypes;

		UFortItemDefinition* HeroType = FindObject<UFortItemDefinition>(L"/Game/Athena/Heroes/HID_030_Athena_Commando_M_Halloween.HID_030_Athena_Commando_M_Halloween");

		for (int i = 0; i < AllHeroTypes.size(); ++i)
		{
			auto CurrentHeroType = (UFortItemDefinition*)AllHeroTypes.at(i);

			if (CurrentHeroType->GetPathName().starts_with("/Game/Athena/Heroes/"))
				AthenaHeroTypes.push_back(CurrentHeroType);
		}

		if (AthenaHeroTypes.size())
		{
			HeroType = AthenaHeroTypes.at(std::rand() % AthenaHeroTypes.size());
		}

		static auto HeroTypeOffset = PlayerState->GetOffset("HeroType");
		PlayerState->Get(HeroTypeOffset) = HeroType;
	}

	void ApplyCosmeticLoadout()
	{
		static auto HeroTypeOffset = PlayerState->GetOffset("HeroType");
		const auto CurrentHeroType = PlayerState->Get(HeroTypeOffset);

		if (!CurrentHeroType)
		{
			LOG_WARN(LogBots, "CurrentHeroType called with an invalid HeroType!");
			return;
		}

		ApplyHID(Pawn, CurrentHeroType, true);
	}

	void SetName(const FString& NewName)
	{
		if (// true ||
			Fortnite_Version < 9
			)
		{
			if (auto PlayerController = Cast<APlayerController>(Controller))
			{
				PlayerController->ServerChangeName(NewName);
			}
		}
		else
		{
			auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
			GameMode->ChangeName(Controller, NewName, true);
		}

		PlayerState->OnRep_PlayerName(); // ?
	}

	FString GetRandomName()
	{
		static int CurrentBotNum = 1;
		std::wstring BotNumWStr;
		FString NewName;

		if (Fortnite_Version < 9)
		{
			BotNumWStr = std::to_wstring(CurrentBotNum++);
			NewName = (L"RebootBot" + BotNumWStr).c_str();
		}
		else
		{
			if (Fortnite_Version < 11 || PlayerBotNames.empty())
			{
				BotNumWStr = std::to_wstring(CurrentBotNum++ + 200);
				NewName = (std::format(L"Anonymous[{}]", BotNumWStr)).c_str();
			}
			else
			{
				NewName = PlayerBotNames.back();
				PlayerBotNames.pop_back();
			}
		}

		return NewName;
	}

	void Initialize(const FTransform& SpawnTransform, AActor* InSpawnLocator)
	{
		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		if (!IsReadyToSpawnBot())
		{
			LOG_ERROR(LogBots, "We are not prepared to spawn a bot!");
			return;
		}

		if (!ShouldUseAIBotController())
		{
			Controller = GetWorld()->SpawnActor<AController>(ControllerClass);
			Pawn = GetWorld()->SpawnActor<AFortPlayerPawnAthena>(PawnClass, SpawnTransform, CreateSpawnParameters(ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn));
			PlayerState = Cast<AFortPlayerStateAthena>(Controller->GetPlayerState());
		}
		else
		{
			Pawn = GameMode->GetServerBotManager()->GetCachedBotMutator()->SpawnBot(PawnClass, InSpawnLocator, SpawnTransform.Translation, SpawnTransform.Rotation.Rotator(), false);

			if (Fortnite_Version < 17)
				Controller = Cast<AFortAthenaAIBotController>(Pawn->GetController());
			else
				Controller = GetWorld()->SpawnActor<AFortAthenaAIBotController>(Pawn->GetAIControllerClass());

			PlayerState = Cast<AFortPlayerStateAthena>(Controller->GetPlayerState());
		}

		if (!Controller || !Pawn || !PlayerState)
		{
			LOG_ERROR(LogBots, "Failed to spawn controller, pawn or playerstate ({} {})!", bool(__int64(Controller)), bool(__int64(Pawn)), bool(__int64(Controller->GetPlayerState())));
			return;
		}

		PlayerState->SetIsBot(true);

		if (Controller->GetPawn() != Pawn)
		{
			Controller->Possess(Pawn);
		}

		FString BotNewName = GetRandomName();
		
		LOG_INFO(LogBots, "BotNewName: {}", BotNewName.ToString());
		SetName(BotNewName);

		PlayerState->GetTeamIndex() = GameMode->Athena_PickTeamHook(GameMode, 0, Controller);

		static auto SquadIdOffset = PlayerState->GetOffset("SquadId", false);

		if (SquadIdOffset != -1)
			PlayerState->GetSquadId() = PlayerState->GetTeamIndex() - NumToSubtractFromSquadId;

		GameState->AddPlayerStateToGameMemberInfo(PlayerState);

		Pawn->SetHealth(100);
		Pawn->SetMaxHealth(100);

		auto PlayerAbilitySet = GetPlayerAbilitySet();
		auto AbilitySystemComponent = PlayerState->GetAbilitySystemComponent();

		if (PlayerAbilitySet && AbilitySystemComponent)
		{
			PlayerAbilitySet->GiveToAbilitySystem(AbilitySystemComponent);
		}

		SetupInventory();
		PickRandomLoadout();
		ApplyCosmeticLoadout();

		if (!ShouldUseAIBotController())
		{
			++GameState->GetPlayersLeft();
			GameState->OnRep_PlayersLeft();
		}

		if (auto FortPlayerControllerAthena = Cast<AFortPlayerControllerAthena>(Controller))
		{
			GameMode->GetAlivePlayers().Add(FortPlayerControllerAthena);
		}

		LOG_INFO(LogDev, "Finished spawning bot!")
	}
};

static inline std::vector<PlayerBot> AllPlayerBotsToTick;

namespace Bots
{
	static AController* SpawnBot(FTransform SpawnTransform, AActor* InSpawnLocator)
	{
		auto playerBot = PlayerBot();
		playerBot.Initialize(SpawnTransform, InSpawnLocator);
		AllPlayerBotsToTick.push_back(playerBot);
		return playerBot.Controller;
	}

	// Spawns the lobby bots. They are created as real player-controller bots
	// at the warmup spawn island (the "lobby"); Bots::Tick then drops them
	// into the map once the bus is flying. Called from
	// AFortGameModeAthena::Athena_ReadyToStartMatchHook.
	static void SpawnBotsAtPlayerStarts(int AmountOfBots)
	{
		if (!Globals::bEnableBots || AmountOfBots <= 0)
			return;

		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		if (!GameState || !GameMode)
			return;

		PlayerBot::InitializeBotClasses();

		if (!PlayerBot::IsReadyToSpawnBot())
		{
			LOG_ERROR(LogBots, "Bot classes not ready, cannot spawn bots!");
			return;
		}

		static auto FortPlayerStartCreativeClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerStartCreative");
		static auto FortPlayerStartWarmupClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerStartWarmup");
		TArray<AActor*> PlayerStarts = UGameplayStatics::GetAllActorsOfClass(GetWorld(), Globals::bCreative ? FortPlayerStartCreativeClass : FortPlayerStartWarmupClass);

		int ActorsNum = PlayerStarts.Num();

		if (ActorsNum == 0)
		{
			LOG_WARN(LogBots, "No player starts to spawn bots at!");
			PlayerStarts.Free();
			return;
		}

		for (int i = 0; i < AmountOfBots; ++i)
		{
			AActor* PlayerStart = PlayerStarts.at(std::rand() % ActorsNum);

			if (!PlayerStart)
				continue;

			SpawnBot(PlayerStart->GetTransform(), PlayerStart);
		}

		PlayerStarts.Free();

		LOG_INFO(LogBots, "Spawned {} lobby bots.", (int)AllPlayerBotsToTick.size());
	}

	// Drops a single bot out of the bus and into the map. The bot is restarted
	// high above a random in-map player start so it falls in like a real
	// player, is made briefly invulnerable so the landing doesn't kill it, and
	// is then handed to the combat AI.
	static void Drop(AFortGameModeAthena* GameMode, PlayerBot& Bot, float Now)
	{
		static auto FortPlayerStartClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerStart");
		auto Starts = UGameplayStatics::GetAllActorsOfClass(GetWorld(), FortPlayerStartClass);

		FTransform DropTransform{};
		DropTransform.Scale3D = FVector(1, 1, 1);
		DropTransform.Rotation = FQuat(0, 0, 0, 1); // identity, overwritten below if we find a start

		if (Starts.Num() > 0)
		{
			auto Start = Starts.at(std::rand() % Starts.Num());

			if (Start)
				DropTransform = Start->GetTransform();
		}
		else if (Bot.Controller->GetPawn())
		{
			DropTransform = Bot.Controller->GetPawn()->GetTransform();
		}

		Starts.Free();

		DropTransform.Translation.Z += Globals::BotDropHeight;

		GameMode->RestartPlayerAtTransform(Bot.Controller, DropTransform);

		Bot.Pawn = Cast<AFortPlayerPawnAthena>(Bot.Controller->GetPawn());

		if (Bot.Pawn)
		{
			Bot.Pawn->SetCanBeDamaged(false); // survive the fall
			Bot.InvulnerableUntil = Now + Globals::BotDropInvulnerableTime;

			// Re-equip the gun on the freshly spawned pawn.
			if (Bot.CombatWeapon)
				Bot.Pawn->EquipWeaponDefinition(Bot.CombatWeapon, Bot.CombatWeaponGuid);

			if (Globals::bBotsFight)
				BotAI::Register(Bot.Pawn);
		}

		Bot.DropState = EBotDropState::Dropped;

		LOG_INFO(LogBots, "Bot dropped into the map.");
	}

	static void Tick()
	{
		if (!Globals::bEnableBots || AllPlayerBotsToTick.size() == 0)
			return;

		if (!GetWorld())
			return;

		auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

		if (!GameState || !GameMode)
			return;

		const float Now = UGameplayStatics::GetTimeSeconds(GetWorld());
		const bool bBusFlying = GameState->GetGamePhase() >= EAthenaGamePhase::Aircraft;

		for (auto& Bot : AllPlayerBotsToTick)
		{
			if (!Bot.Controller || Bot.Controller->IsActorBeingDestroyed())
				continue;

			switch (Bot.DropState)
			{
			case EBotDropState::Lobby:
			{
				// As soon as the bus is flying, schedule a randomised drop time
				// so bots leave the bus at different points like real players.
				if (bBusFlying)
				{
					float Spread = Globals::BotMaxDropDelay - Globals::BotMinDropDelay;

					if (Spread < 0.f)
						Spread = 0.f;

					float RandomDelay = Spread > 0.f ? (float)(std::rand() % (int)(Spread * 100)) / 100.f : 0.f;
					Bot.DropTime = Now + Globals::BotMinDropDelay + RandomDelay;
					Bot.DropState = EBotDropState::WaitingDrop;
				}
				break;
			}

			case EBotDropState::WaitingDrop:
			{
				if (Now >= Bot.DropTime)
					Drop(GameMode, Bot, Now);
				break;
			}

			case EBotDropState::Dropped:
			{
				// Once the fall is over make the bot a normal, damageable target.
				if (Bot.InvulnerableUntil != 0.f && Now >= Bot.InvulnerableUntil)
				{
					if (auto Pawn = Bot.Controller->GetPawn())
						Pawn->SetCanBeDamaged(true);

					Bot.InvulnerableUntil = 0.f;
				}
				break;
			}
			}
		}
	}
}

namespace Bosses
{

}
