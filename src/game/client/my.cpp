#include "my.h"
#include <game/client/gameclient.h>
#include <game/client/components/controls.h>
#include <game/client/prediction/entities/character.h>
#include <engine/graphics.h>
#include <base/math.h>
#include <base/vmath.h>
#include <cmath>
#include <algorithm>
#include <limits>
#include <vector>
#include <game/mapitems.h>
#include <game/collision.h>

static int s_aTargetLostTicks[NUM_DUMMIES] = {0, 0};

void CMyComponent::OnReset()
{
	for(int i = 0; i < NUM_DUMMIES; i++)
	{
		m_aTargetId[i] = -1;
		m_aAimState[i] = STATE_IDLE;
		m_aCurrentAim[i] = vec2(0.0f, 0.0f);
		m_aHookOverride[i] = false;
		m_aEnabled[i] = false;
		m_aFov[i] = 50;

		m_aAvoidEnabled[i] = false;
		m_aAvoidDirection[i] = 0;
		m_aAvoidActive[i] = false;
		m_aAvoidTargetDirection[i] = 0;

		m_aAutoHammerEnabled[i] = false;
		m_aHammerOverride[i] = false;
		m_aHammerTarget[i] = vec2(0.0f, 0.0f);

		m_aAutoLaserEnabled[i] = false;
		m_aLaserOverride[i] = false;
		m_aLaserTarget[i] = vec2(0.0f, 0.0f);

		s_aTargetLostTicks[i] = 0;
	}
}

void CMyComponent::OnConsoleInit()
{
	Console()->Register("toggle_silentaim", "", CFGFLAG_CLIENT, ConToggleSilentAim, this, "Toggle Auto Aim");
	Console()->Register("+silentaim", "", CFGFLAG_CLIENT, ConKeySilentAim, this, "Hold Auto Aim");
	Console()->Register("cl_silentaim_fov", "i[fov]", CFGFLAG_CLIENT, ConFovSilentAim, this, "Set Auto Aim FOV");

	Console()->Register("toggle_avoidfreeze", "", CFGFLAG_CLIENT, ConToggleAvoidFreeze, this, "Toggle Avoid Freeze");
	Console()->Register("+avoidfreeze", "", CFGFLAG_CLIENT, ConKeyAvoidFreeze, this, "Hold Avoid Freeze");

	Console()->Register("toggle_autohammer", "", CFGFLAG_CLIENT, ConToggleAutoHammer, this, "Toggle Auto Hammer");
	Console()->Register("+autohammer", "", CFGFLAG_CLIENT, ConKeyAutoHammer, this, "Hold Auto Hammer");

	Console()->Register("toggle_autolaser", "", CFGFLAG_CLIENT, ConToggleAutoLaser, this, "Toggle Auto Laser");
	Console()->Register("+autolaser", "", CFGFLAG_CLIENT, ConKeyAutoLaser, this, "Hold Auto Laser");
}

void CMyComponent::ConToggleSilentAim(IConsole::IResult *pResult, void *pUserData)
{
	CMyComponent *pSelf = (CMyComponent *)pUserData;
	pSelf->m_aEnabled[g_Config.m_ClDummy] = !pSelf->m_aEnabled[g_Config.m_ClDummy];
}

void CMyComponent::ConKeySilentAim(IConsole::IResult *pResult, void *pUserData)
{
	CMyComponent *pSelf = (CMyComponent *)pUserData;
	pSelf->m_aEnabled[g_Config.m_ClDummy] = pResult->GetInteger(0) != 0;
}

void CMyComponent::ConFovSilentAim(IConsole::IResult *pResult, void *pUserData)
{
	CMyComponent *pSelf = (CMyComponent *)pUserData;
	pSelf->SetFov(g_Config.m_ClDummy, pResult->GetInteger(0));
}

void CMyComponent::ConToggleAvoidFreeze(IConsole::IResult *pResult, void *pUserData)
{
	CMyComponent *pSelf = (CMyComponent *)pUserData;
	pSelf->m_aAvoidEnabled[g_Config.m_ClDummy] = !pSelf->m_aAvoidEnabled[g_Config.m_ClDummy];
}

void CMyComponent::ConKeyAvoidFreeze(IConsole::IResult *pResult, void *pUserData)
{
	CMyComponent *pSelf = (CMyComponent *)pUserData;
	pSelf->m_aAvoidEnabled[g_Config.m_ClDummy] = pResult->GetInteger(0) != 0;
}

void CMyComponent::ConToggleAutoHammer(IConsole::IResult *pResult, void *pUserData)
{
	CMyComponent *pSelf = (CMyComponent *)pUserData;
	pSelf->m_aAutoHammerEnabled[g_Config.m_ClDummy] = !pSelf->m_aAutoHammerEnabled[g_Config.m_ClDummy];
}

void CMyComponent::ConKeyAutoHammer(IConsole::IResult *pResult, void *pUserData)
{
	CMyComponent *pSelf = (CMyComponent *)pUserData;
	pSelf->m_aAutoHammerEnabled[g_Config.m_ClDummy] = pResult->GetInteger(0) != 0;
}

void CMyComponent::ConToggleAutoLaser(IConsole::IResult *pResult, void *pUserData)
{
	CMyComponent *pSelf = (CMyComponent *)pUserData;
	pSelf->m_aAutoLaserEnabled[g_Config.m_ClDummy] = !pSelf->m_aAutoLaserEnabled[g_Config.m_ClDummy];
}

void CMyComponent::ConKeyAutoLaser(IConsole::IResult *pResult, void *pUserData)
{
	CMyComponent *pSelf = (CMyComponent *)pUserData;
	pSelf->m_aAutoLaserEnabled[g_Config.m_ClDummy] = pResult->GetInteger(0) != 0;
}

float CMyComponent::GetJitterAngle(int DummyIdx) const
{
	return 0.0f;
}

bool CMyComponent::SimulateLaser(
	int ShooterId,
	vec2 From,
	vec2 Dir,
	float Energy,
	float BounceCost,
	int MaxBounces,
	int TargetId,
	vec2 TargetPos,
	float TargetRadius,
	vec2 *pOutHitPos)
{
	vec2 CurrentPos = From;
	vec2 CurrentDir = Dir;
	float RemainingEnergy = Energy;

	for(int Bounce = 0; Bounce <= MaxBounces; Bounce++)
	{
		vec2 To = CurrentPos + CurrentDir * RemainingEnergy;
		vec2 ColTile;
		vec2 HitPos;
		int Res = Collision()->IntersectLineTeleWeapon(CurrentPos, To, &ColTile, &HitPos);
		vec2 SegmentEnd = Res ? HitPos : To;

		if(Bounce > 0 || ShooterId != TargetId)
		{
			vec2 Closest;
			if(closest_point_on_line(CurrentPos, SegmentEnd, TargetPos, Closest))
			{
				if(distance(Closest, TargetPos) <= TargetRadius)
				{
					if(pOutHitPos)
						*pOutHitPos = Closest;
					return true;
				}
			}
		}

		if(!Res)
			break;

		vec2 TempPos = HitPos;
		vec2 TempDir = CurrentDir * 4.0f;
		int f = 0;
		if(Res == -1)
		{
			f = Collision()->GetTile(round_to_int(ColTile.x), round_to_int(ColTile.y));
			Collision()->SetCollisionAt(round_to_int(ColTile.x), round_to_int(ColTile.y), TILE_SOLID);
		}
		Collision()->MovePoint(&TempPos, &TempDir, 1.0f, nullptr);
		if(Res == -1)
		{
			Collision()->SetCollisionAt(round_to_int(ColTile.x), round_to_int(ColTile.y), f);
		}

		float StepDist = distance(CurrentPos, HitPos);
		RemainingEnergy -= (StepDist + BounceCost);
		if(RemainingEnergy <= 0.0f)
			break;

		CurrentPos = TempPos;
		if(length(TempDir) < 0.001f)
			break;
		CurrentDir = normalize(TempDir);
	}

	return false;
}

bool CMyComponent::FindLaserTrajectory(
	int ShooterId,
	vec2 ShooterPos,
	int TargetId,
	vec2 TargetPos,
	vec2 TargetVel,
	bool TargetOnGround,
	int DummyIdx,
	vec2 &OutDir)
{
	int TuneZone = Collision()->IsTune(Collision()->GetPureMapIndex(ShooterPos));
	const CTuningParams *pTuning = GameClient()->GetTuning(TuneZone);
	float LaserReach = pTuning->m_LaserReach;
	if(LaserReach < 10.0f)
		LaserReach = 800.0f;
	float LaserBounceCost = pTuning->m_LaserBounceCost;
	int LaserBounceNum = pTuning->m_LaserBounceNum;
	float LaserBounceDelay = pTuning->m_LaserBounceDelay;
	float Gravity = pTuning->m_Gravity;

	float TicksToHit = 1.0f;
	if(LaserBounceDelay > 0.0f)
		TicksToHit += (LaserBounceDelay / 1000.0f) * Client()->GameTickSpeed();

	vec2 PredictedTargetPos = TargetPos + TargetVel * TicksToHit;
	if(!TargetOnGround)
		PredictedTargetPos.y += 0.5f * Gravity * TicksToHit * TicksToHit;

	if(Collision()->CheckPoint(PredictedTargetPos))
		PredictedTargetPos = TargetPos;

	vec2 AimDirCurrent = GameClient()->m_Controls.m_aMousePos[DummyIdx];
	if(length(AimDirCurrent) < 0.001f)
		AimDirCurrent = vec2(1.0f, 0.0f);
	else
		AimDirCurrent = normalize(AimDirCurrent);

	if(ShooterId != TargetId)
	{
		vec2 DirDirect = PredictedTargetPos - ShooterPos;
		float DistDirect = length(DirDirect);
		if(DistDirect > 1.0f && DistDirect <= LaserReach)
		{
			DirDirect = normalize(DirDirect);
			vec2 ColTile;
			vec2 HitPos;
			int Res = Collision()->IntersectLineTeleWeapon(ShooterPos, ShooterPos + DirDirect * DistDirect, &ColTile, &HitPos);
			if(!Res)
			{
				OutDir = DirDirect;
				return true;
			}
		}
	}

	std::vector<vec2> vCandidateDirs;
	vCandidateDirs.reserve(256);

	int CenterTileX = round_to_int((ShooterPos.x + PredictedTargetPos.x) * 0.5f) / 32;
	int CenterTileY = round_to_int((ShooterPos.y + PredictedTargetPos.y) * 0.5f) / 32;
	int Range = std::clamp(round_to_int(LaserReach / 64.0f), 6, 16);

	int MinTX = std::max(0, CenterTileX - Range);
	int MaxTX = std::min(Collision()->GetWidth() - 1, CenterTileX + Range);
	int MinTY = std::max(0, CenterTileY - Range);
	int MaxTY = std::min(Collision()->GetHeight() - 1, CenterTileY + Range);

	for(int ty = MinTY; ty <= MaxTY; ty++)
	{
		for(int tx = MinTX; tx <= MaxTX; tx++)
		{
			int Tile = Collision()->GetTileIndex(Collision()->GetPureMapIndex(vec2(tx * 32.0f + 16.0f, ty * 32.0f + 16.0f)));
			int FTile = Collision()->GetFrontTileIndex(Collision()->GetPureMapIndex(vec2(tx * 32.0f + 16.0f, ty * 32.0f + 16.0f)));
			bool IsSolid = Collision()->CheckPoint(tx * 32.0f + 16.0f, ty * 32.0f + 16.0f) ||
				Tile == TILE_SOLID || Tile == TILE_NOHOOK || FTile == TILE_SOLID || FTile == TILE_NOHOOK;

			if(!IsSolid)
				continue;

			float LeftX = tx * 32.0f;
			if(ShooterPos.x < LeftX && PredictedTargetPos.x < LeftX)
			{
				vec2 Mirror = vec2(2.0f * LeftX - PredictedTargetPos.x, PredictedTargetPos.y);
				vec2 D = Mirror - ShooterPos;
				if(length(D) > 1.0f)
					vCandidateDirs.push_back(normalize(D));
			}

			float RightX = (tx + 1) * 32.0f;
			if(ShooterPos.x > RightX && PredictedTargetPos.x > RightX)
			{
				vec2 Mirror = vec2(2.0f * RightX - PredictedTargetPos.x, PredictedTargetPos.y);
				vec2 D = Mirror - ShooterPos;
				if(length(D) > 1.0f)
					vCandidateDirs.push_back(normalize(D));
			}

			float TopY = ty * 32.0f;
			if(ShooterPos.y < TopY && PredictedTargetPos.y < TopY)
			{
				vec2 Mirror = vec2(PredictedTargetPos.x, 2.0f * TopY - PredictedTargetPos.y);
				vec2 D = Mirror - ShooterPos;
				if(length(D) > 1.0f)
					vCandidateDirs.push_back(normalize(D));
			}

			float BottomY = (ty + 1) * 32.0f;
			if(ShooterPos.y > BottomY && PredictedTargetPos.y > BottomY)
			{
				vec2 Mirror = vec2(PredictedTargetPos.x, 2.0f * BottomY - PredictedTargetPos.y);
				vec2 D = Mirror - ShooterPos;
				if(length(D) > 1.0f)
					vCandidateDirs.push_back(normalize(D));
			}
		}
	}

	const int NUM_ANGULAR_STEPS = 180;
	for(int s = 0; s < NUM_ANGULAR_STEPS; s++)
	{
		float Angle = (float)s * (2.0f * pi / (float)NUM_ANGULAR_STEPS);
		vCandidateDirs.push_back(direction(Angle));
	}

	bool FoundBest = false;
	float BestScore = -1e9f;
	vec2 BestCandidateDir = vec2(0.0f, 0.0f);

	for(const vec2 &TestDir : vCandidateDirs)
	{
		vec2 HitPoint;
		if(SimulateLaser(ShooterId, ShooterPos, TestDir, LaserReach, LaserBounceCost, std::max(1, LaserBounceNum), TargetId, PredictedTargetPos, 24.0f, &HitPoint))
		{
			float DistToCenter = distance(HitPoint, PredictedTargetPos);
			float AlignScore = dot(TestDir, AimDirCurrent);
			float Score = (24.0f - DistToCenter) * 10.0f + AlignScore * 5.0f;
			if(!FoundBest || Score > BestScore)
			{
				BestScore = Score;
				BestCandidateDir = TestDir;
				FoundBest = true;
			}
		}
	}

	if(!FoundBest)
		return false;

	float BaseAngle = angle(BestCandidateDir);
	float StepRad = 2.0f * pi / (float)NUM_ANGULAR_STEPS;
	for(int iter = 0; iter < 8; iter++)
	{
		StepRad *= 0.5f;
		vec2 DirPlus = direction(BaseAngle + StepRad);
		vec2 DirMinus = direction(BaseAngle - StepRad);
		vec2 HitP, HitM, HitB;

		bool OkB = SimulateLaser(ShooterId, ShooterPos, direction(BaseAngle), LaserReach, LaserBounceCost, std::max(1, LaserBounceNum), TargetId, PredictedTargetPos, 24.0f, &HitB);
		bool OkP = SimulateLaser(ShooterId, ShooterPos, DirPlus, LaserReach, LaserBounceCost, std::max(1, LaserBounceNum), TargetId, PredictedTargetPos, 24.0f, &HitP);
		bool OkM = SimulateLaser(ShooterId, ShooterPos, DirMinus, LaserReach, LaserBounceCost, std::max(1, LaserBounceNum), TargetId, PredictedTargetPos, 24.0f, &HitM);

		float DistBase = OkB ? distance(HitB, PredictedTargetPos) : 999.0f;
		float DistP = OkP ? distance(HitP, PredictedTargetPos) : 999.0f;
		float DistM = OkM ? distance(HitM, PredictedTargetPos) : 999.0f;

		if(DistP < DistBase && DistP <= DistM)
			BaseAngle += StepRad;
		else if(DistM < DistBase && DistM < DistP)
			BaseAngle -= StepRad;
	}

	OutDir = direction(BaseAngle);
	return true;
}

void CMyComponent::OnUpdate()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		return;
	}

	const int DummyIdx = g_Config.m_ClDummy;

	m_aAvoidDirection[DummyIdx] = 0;
	m_aAvoidActive[DummyIdx] = false;
	m_aHammerOverride[DummyIdx] = false;
	m_aLaserOverride[DummyIdx] = false;

	if(m_aAvoidEnabled[DummyIdx])
	{
		int LocalId = GameClient()->m_Snap.m_LocalClientId;
		if(LocalId >= 0 && GameClient()->m_Snap.m_aCharacters[LocalId].m_Active)
		{
			CCharacterCore StartCore = GameClient()->m_aClients[LocalId].m_Predicted;
			CNetObj_PlayerInput CurrentInput = GameClient()->m_Controls.m_aInputData[DummyIdx];

			int InputDir = 0;
			if(GameClient()->m_Controls.m_aInputDirectionLeft[DummyIdx] && !GameClient()->m_Controls.m_aInputDirectionRight[DummyIdx])
				InputDir = -1;
			else if(!GameClient()->m_Controls.m_aInputDirectionLeft[DummyIdx] && GameClient()->m_Controls.m_aInputDirectionRight[DummyIdx])
				InputDir = 1;

			CurrentInput.m_Direction = InputDir;

			int MoveDir = CurrentInput.m_Direction;
			if(MoveDir == 0 && std::abs(StartCore.m_Vel.x) > 0.5f)
			{
				MoveDir = StartCore.m_Vel.x > 0.0f ? 1 : -1;
			}

			if(m_aAvoidTargetDirection[DummyIdx] != 0)
			{
				if(CurrentInput.m_Direction != 0 && CurrentInput.m_Direction != m_aAvoidTargetDirection[DummyIdx])
				{
					m_aAvoidTargetDirection[DummyIdx] = 0;
				}
			}

			if(MoveDir != 0)
			{
				auto IsTileFreeze = [&](int Index) {
					if(Index < 0) return false;
					int Tile = Collision()->GetTileIndex(Index);
					int FTile = Collision()->GetFrontTileIndex(Index);
					int Switch = Collision()->GetSwitchType(Index);
					return Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE ||
					       FTile == TILE_FREEZE || FTile == TILE_DFREEZE || FTile == TILE_LFREEZE ||
					       Switch == TILE_FREEZE || Switch == TILE_DFREEZE || Switch == TILE_LFREEZE;
				};

				auto IsTileDeath = [&](int Index) {
					if(Index < 0) return false;
					int Tile = Collision()->GetTileIndex(Index);
					int FTile = Collision()->GetFrontTileIndex(Index);
					int Switch = Collision()->GetSwitchType(Index);
					return Tile == TILE_DEATH || FTile == TILE_DEATH || Switch == TILE_DEATH;
				};

				auto IsDangerPos = [&](vec2 Pos, int Direction) {
					int Index = Collision()->GetPureMapIndex(Pos);
					if(IsTileFreeze(Index))
						return true;

					int ProbeIndex = Collision()->GetPureMapIndex(Pos + vec2((float)Direction * 1.0f, 0.0f));
					if(IsTileFreeze(ProbeIndex))
						return true;

					const float rDeath = 28.0f / 3.0f;
					const vec2 aCheckPoints[5] = {
						Pos,
						Pos + vec2(-rDeath, -rDeath),
						Pos + vec2(rDeath, -rDeath),
						Pos + vec2(-rDeath, rDeath),
						Pos + vec2(rDeath, rDeath)
					};
					for(const auto &p : aCheckPoints)
					{
						int DeathIndex = Collision()->GetPureMapIndex(p);
						if(IsTileDeath(DeathIndex))
							return true;
					}
					return false;
				};

				auto IsDangerSegment = [&](vec2 From, vec2 To, int Direction) {
					if(IsDangerPos(To, Direction))
						return true;
					std::vector<int> vIndices = Collision()->GetMapIndices(From, To);
					for(int Idx : vIndices)
					{
						if(IsTileFreeze(Idx) || IsTileDeath(Idx))
							return true;
					}
					return false;
				};

				int CurrentZone = Collision()->IsTune(Collision()->GetPureMapIndex(StartCore.m_Pos));
				const CTuningParams *pTuning = GameClient()->GetTuning(CurrentZone);

				CWorldCore TempWorld;
				CCharacterCore TempCore;
				TempCore.Init(&TempWorld, Collision());

				bool OnGround = Collision()->IsOnGround(StartCore.m_Pos, 28.0f);
				int SimTicks = (OnGround && std::abs(StartCore.m_Vel.y) < 0.1f) ? 60 : 90;

				bool PathCDanger = false;
				CCharacterCore CoreC = StartCore;
				CoreC.Init(&TempWorld, Collision());
				CoreC.m_Tuning = *pTuning;
				CNetObj_PlayerInput SimInputC = CurrentInput;
				SimInputC.m_Direction = MoveDir;

				for(int i = 0; i < SimTicks; i++)
				{
					vec2 StepFrom = CoreC.m_Pos;
					CoreC.m_Input = SimInputC;
					CoreC.Tick(true, !GameClient()->m_GameWorld.m_WorldConfig.m_NoWeakHookAndBounce);
					CoreC.Move();
					CoreC.Quantize();
					if(IsDangerSegment(StepFrom, CoreC.m_Pos, MoveDir))
					{
						PathCDanger = true;
						break;
					}
				}

				if(!PathCDanger)
				{
					m_aAvoidTargetDirection[DummyIdx] = 0;
				}
				else
				{
					if(m_aAvoidTargetDirection[DummyIdx] == 0)
					{
						bool PathBDanger = false;
						CCharacterCore CoreB = StartCore;
						CoreB.Init(&TempWorld, Collision());
						CoreB.m_Tuning = *pTuning;

						vec2 PrevPos = CoreB.m_Pos;
						CoreB.m_Input = SimInputC;
						CoreB.Tick(true, !GameClient()->m_GameWorld.m_WorldConfig.m_NoWeakHookAndBounce);
						CoreB.Move();
						CoreB.Quantize();

						if(IsDangerSegment(PrevPos, CoreB.m_Pos, MoveDir))
						{
							PathBDanger = true;
						}
						else
						{
							for(int i = 1; i < SimTicks; i++)
							{
								PrevPos = CoreB.m_Pos;
								CoreB.m_Input.m_Direction = -MoveDir;
								CoreB.m_Input.m_Jump = 0;
								CoreB.m_Input.m_Hook = 0;
								CoreB.Tick(true, !GameClient()->m_GameWorld.m_WorldConfig.m_NoWeakHookAndBounce);
								CoreB.Move();
								CoreB.Quantize();

								if(IsDangerSegment(PrevPos, CoreB.m_Pos, MoveDir))
								{
									PathBDanger = true;
									break;
								}

								if(CoreB.m_Vel.x * (float)MoveDir <= 0.1f)
								{
									break;
								}
							}
						}

						if(PathBDanger)
						{
							m_aAvoidTargetDirection[DummyIdx] = MoveDir;
						}
					}
				}

				if(m_aAvoidTargetDirection[DummyIdx] != 0)
				{
					int TargetDir = m_aAvoidTargetDirection[DummyIdx];
					int BrakeDir = 0;
					if(StartCore.m_Vel.x * (float)TargetDir > 0.1f)
						BrakeDir = -TargetDir;

					m_aAvoidActive[DummyIdx] = true;
					m_aAvoidDirection[DummyIdx] = BrakeDir;
				}
			}
			else
			{
				m_aAvoidTargetDirection[DummyIdx] = 0;
			}
		}
	}

	if(m_aAutoHammerEnabled[DummyIdx])
	{
		int LocalId = GameClient()->m_Snap.m_LocalClientId;
		if(LocalId >= 0 && GameClient()->m_Snap.m_aCharacters[LocalId].m_Active)
		{
			vec2 LocalPos = GameClient()->m_aClients[LocalId].m_Predicted.m_Pos;
			CCharacter *pLocalChar = GameClient()->m_GameWorld.GetCharacterById(LocalId);
			if(pLocalChar && pLocalChar->GetActiveWeapon() == WEAPON_HAMMER)
			{
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(i == LocalId || !GameClient()->m_aClients[i].m_Active || !GameClient()->m_Snap.m_aCharacters[i].m_Active)
						continue;

					CCharacter *pTargetChar = GameClient()->m_GameWorld.GetCharacterById(i);
					if(!pTargetChar || !pLocalChar->CanCollide(i))
						continue;

					vec2 TargetPos = GameClient()->m_aClients[i].m_Predicted.m_Pos;
					if(length(TargetPos) < 1.0f)
						TargetPos = GameClient()->m_aClients[i].m_RenderPos;

					float Dist = distance(LocalPos, TargetPos);

					if(Dist < 62.9f)
					{
						m_aHammerOverride[DummyIdx] = true;
						vec2 Dir = TargetPos - LocalPos;
						if(length(Dir) < 0.001f)
							Dir = vec2(1.0f, 0.0f);
						m_aHammerTarget[DummyIdx] = normalize(Dir) * 100.0f;
						break;
					}
				}
			}
		}
	}

	if(m_aAutoLaserEnabled[DummyIdx])
	{
		int LocalId = GameClient()->m_Snap.m_LocalClientId;
		if(LocalId >= 0 && GameClient()->m_Snap.m_aCharacters[LocalId].m_Active)
		{
			CCharacter *pLocalChar = GameClient()->m_GameWorld.GetCharacterById(LocalId);
			if(pLocalChar && pLocalChar->GetWeaponGot(WEAPON_LASER))
			{
				if(pLocalChar->GetActiveWeapon() != WEAPON_LASER)
				{
					GameClient()->m_Controls.m_aInputData[DummyIdx].m_WantedWeapon = WEAPON_LASER + 1;
				}

				int TargetId = LocalId;
				if(DummyIdx == 1 && GameClient()->m_aLocalIds[0] >= 0)
				{
					TargetId = GameClient()->m_aLocalIds[0];
				}

				CCharacter *pTargetChar = GameClient()->m_GameWorld.GetCharacterById(TargetId);
				if(pTargetChar)
				{
					vec2 ShooterPos = GameClient()->m_aClients[LocalId].m_Predicted.m_Pos;
					if(length(ShooterPos) < 1.0f)
						ShooterPos = GameClient()->m_aClients[LocalId].m_RenderPos;

					vec2 TargetPos = GameClient()->m_aClients[TargetId].m_Predicted.m_Pos;
					if(length(TargetPos) < 1.0f)
						TargetPos = GameClient()->m_aClients[TargetId].m_RenderPos;

					vec2 TargetVel = GameClient()->m_aClients[TargetId].m_Predicted.m_Vel;
					bool TargetOnGround = Collision()->IsOnGround(TargetPos, 28.0f);

					vec2 BestDir;
					if(FindLaserTrajectory(LocalId, ShooterPos, TargetId, TargetPos, TargetVel, TargetOnGround, DummyIdx, BestDir))
					{
						m_aLaserOverride[DummyIdx] = true;
						m_aLaserTarget[DummyIdx] = normalize(BestDir) * 100.0f;
					}
				}
			}
		}
	}

	if(!m_aEnabled[DummyIdx])
	{
		m_aTargetId[DummyIdx] = -1;
		m_aAimState[DummyIdx] = STATE_IDLE;
		m_aHookOverride[DummyIdx] = false;
		return;
	}

	int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || !GameClient()->m_Snap.m_aCharacters[LocalId].m_Active)
	{
		m_aTargetId[DummyIdx] = -1;
		m_aAimState[DummyIdx] = STATE_IDLE;
		m_aHookOverride[DummyIdx] = false;
		return;
	}

	CCharacter *pLocalChar = GameClient()->m_GameWorld.GetCharacterById(LocalId);
	if(!pLocalChar) return;

	vec2 LocalPos = GameClient()->m_aClients[LocalId].m_RenderPos;
	vec2 MousePosVec = GameClient()->m_Controls.m_aMousePos[DummyIdx];
	float MouseLen = length(MousePosVec);
	vec2 AimDir = MouseLen < 0.001f ? vec2(1.0f, 0.0f) : normalize(MousePosVec);
	if(MouseLen < 0.001f) MouseLen = 200.0f;

	float HookLength = GameClient()->m_aTuning[DummyIdx].m_HookLength;
	if(HookLength < 1.0f) HookLength = 380.0f;
	float HookSpeed = GameClient()->m_aTuning[DummyIdx].m_HookFireSpeed;
	if(HookSpeed <= 0.0f) HookSpeed = 80.0f;
	float Gravity = GameClient()->m_aTuning[DummyIdx].m_Gravity;

	float HalfAngleRad = (m_aFov[DummyIdx] / 2.0f) * (pi / 180.0f);
	float MinCos = std::cos(HalfAngleRad);

	float BestDistToMouse = std::numeric_limits<float>::max();
	int BestId = -1;
	vec2 MouseWorldPos = LocalPos + MousePosVec;
	vec2 BestAimDir = AimDir;

	int CurrentHooked = pLocalChar->Core()->HookedPlayer();
	bool RetainLock = false;

	if(m_aTargetId[DummyIdx] >= 0 && m_aTargetId[DummyIdx] < MAX_CLIENTS)
	{
		int PrevTargetId = m_aTargetId[DummyIdx];
		if(CurrentHooked == PrevTargetId &&
		   GameClient()->m_aClients[PrevTargetId].m_Active &&
		   GameClient()->m_Snap.m_aCharacters[PrevTargetId].m_Active)
		{
			vec2 TargetPos = GameClient()->m_aClients[PrevTargetId].m_RenderPos;
			vec2 ToTarget = normalize(TargetPos - LocalPos);
			float CosTheta = dot(AimDir, ToTarget);
			if(CosTheta >= MinCos)
			{
				RetainLock = true;
			}
		}
	}

	if(RetainLock)
	{
		BestId = m_aTargetId[DummyIdx];
		vec2 TargetPos = GameClient()->m_aClients[BestId].m_RenderPos;
		vec2 TargetVel = vec2(0.0f, 0.0f);
		if(GameClient()->m_aClients[BestId].m_IsPredicted)
			TargetVel = GameClient()->m_aClients[BestId].m_Predicted.m_Vel;
		else if(GameClient()->m_Snap.m_aCharacters[BestId].m_Active)
			TargetVel = vec2(GameClient()->m_Snap.m_aCharacters[BestId].m_Cur.m_VelX, GameClient()->m_Snap.m_aCharacters[BestId].m_Cur.m_VelY) / 256.0f;

		if(length(TargetVel) > 100.0f)
			TargetVel = vec2(0.0f, 0.0f);

		float DistToTarget = distance(LocalPos, TargetPos);
		float TicksToHit = DistToTarget / HookSpeed;

		float decay = 0.92f;
		float time_factor = (1.0f - std::pow(decay, TicksToHit)) / (1.0f - decay);

		vec2 FuturePos = TargetPos + TargetVel * time_factor;
		FuturePos.y += 0.5f * Gravity * TicksToHit * TicksToHit;

		vec2 ToFutureTarget = FuturePos - LocalPos;
		float FutureDist = length(ToFutureTarget);

		if(CurrentHooked == BestId)
		{
			BestAimDir = normalize(TargetPos - LocalPos);
		}
		else if(FutureDist > 0.001f)
			BestAimDir = normalize(ToFutureTarget);
	}
	else
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(i == LocalId) continue;
			if(!GameClient()->m_aClients[i].m_Active || !GameClient()->m_Snap.m_aCharacters[i].m_Active) continue;
			if(!pLocalChar->CanCollide(i)) continue;

			vec2 TargetPos = GameClient()->m_aClients[i].m_RenderPos;
			vec2 TargetVel = vec2(0.0f, 0.0f);
			if(GameClient()->m_aClients[i].m_IsPredicted)
				TargetVel = GameClient()->m_aClients[i].m_Predicted.m_Vel;
			else
				TargetVel = vec2(GameClient()->m_Snap.m_aCharacters[i].m_Cur.m_VelX, GameClient()->m_Snap.m_aCharacters[i].m_Cur.m_VelY) / 256.0f;

			if(length(TargetVel) > 100.0f)
				TargetVel = vec2(0.0f, 0.0f);

			vec2 D = TargetPos - LocalPos;
			float a = dot(TargetVel, TargetVel) - HookSpeed * HookSpeed;
			float b = 2.0f * dot(D, TargetVel);
			float c = dot(D, D);

			float t = -1.0f;
			if(std::abs(a) < 1e-4f)
			{
				if(std::abs(b) > 1e-4f)
					t = -c / b;
			}
			else
			{
				float Discriminant = b * b - 4.0f * a * c;
				if(Discriminant >= 0.0f)
				{
					float t1 = (-b - std::sqrt(Discriminant)) / (2.0f * a);
					float t2 = (-b + std::sqrt(Discriminant)) / (2.0f * a);
					if(t1 > 0.0f && t2 > 0.0f) t = std::min(t1, t2);
					else if(t1 > 0.0f) t = t1;
					else if(t2 > 0.0f) t = t2;
				}
			}

			if(t < 0.0f || t > 100.0f)
			{
				t = distance(LocalPos, TargetPos) / HookSpeed;
			}

			float decay = 0.92f;
			float time_factor = (1.0f - std::pow(decay, t)) / (1.0f - decay);

			vec2 PredictedPos = TargetPos + TargetVel * time_factor;
			PredictedPos.y += 0.5f * Gravity * t * t;

			float DistToPredicted = distance(LocalPos, PredictedPos);

			if(DistToPredicted > HookLength + 28.0f) continue;

			vec2 DirToPredicted = normalize(PredictedPos - LocalPos);
			float CosTheta = dot(AimDir, DirToPredicted);
			if(CosTheta < MinCos && DistToPredicted > 42.0f) continue;

			bool Hookable = false;
			vec2 BestTargetAimDir = DirToPredicted;
			float BestTargetScore = -1.0f;

			float HalfSpan = std::asin(std::clamp(28.0f / DistToPredicted, 0.0f, 1.0f));
			float AngleCenter = angle(PredictedPos - LocalPos);

			const int NUM_SAMPLES = 61;
			for(int step = 0; step < NUM_SAMPLES; ++step)
			{
				float t_ratio = -1.0f + (2.0f * step) / (NUM_SAMPLES - 1);
				float OffsetAngle = t_ratio * HalfSpan;
				float TestAngle = AngleCenter + OffsetAngle;
				vec2 TestDir = direction(TestAngle);

				vec2 StartPos = LocalPos + TestDir * 42.0f;
				vec2 v = StartPos - PredictedPos;
				float b_ray = dot(v, TestDir);
				float c_ray = dot(v, v) - 28.0f * 28.0f;
				float D_ray = b_ray * b_ray - c_ray;

				if(D_ray >= 0.0f)
				{
					float t1_ray = -b_ray - std::sqrt(D_ray);
					float t2_ray = -b_ray + std::sqrt(D_ray);

					float t_entry = t1_ray;
					if(t1_ray < 0.0f && t2_ray > 0.0f)
					{
						t_entry = 0.0f;
					}

					if(t_entry >= 0.0f && t_entry <= (HookLength - 42.0f))
					{
						vec2 HitPos;
						int WallHit = Collision()->IntersectLineTeleHook(StartPos, StartPos + TestDir * t_entry, &HitPos, nullptr, nullptr);

						if(!WallHit)
						{
							Hookable = true;
							float Score = dot(AimDir, TestDir);

							if(Score > BestTargetScore)
							{
								BestTargetScore = Score;
								BestTargetAimDir = TestDir;
							}
						}
					}
				}
			}

			if(Hookable)
			{
				float DistToMouse = distance(PredictedPos, MouseWorldPos);
				if(DistToMouse < BestDistToMouse)
				{
					BestDistToMouse = DistToMouse;
					BestId = i;
					BestAimDir = BestTargetAimDir;
				}
			}
		}
	}

	if(BestId != -1)
	{
		m_aTargetId[DummyIdx] = BestId;
		m_aCurrentAim[DummyIdx] = BestAimDir * MouseLen;
		bool UserWantsHook = GameClient()->m_Controls.m_aInputData[DummyIdx].m_Hook != 0;

		if(UserWantsHook)
		{
			m_aAimState[DummyIdx] = STATE_HOOKING;
			m_aHookOverride[DummyIdx] = true;
		}
		else
		{
			m_aAimState[DummyIdx] = STATE_AIMING_IN;
			m_aHookOverride[DummyIdx] = false;
		}
	}
	else
	{
		m_aTargetId[DummyIdx] = -1;
		m_aHookOverride[DummyIdx] = false;
		m_aAimState[DummyIdx] = STATE_IDLE;
		m_aCurrentAim[DummyIdx] = GameClient()->m_Controls.m_aMousePos[DummyIdx];
	}
}

void CMyComponent::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		return;
	}

	const int DummyIdx = g_Config.m_ClDummy;
	float Height = 300.0f;
	float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);

	float DisplayY = 150.0f;

	if(m_aEnabled[DummyIdx])
	{
		TextRender()->TextColor(0.0f, 1.0f, 0.0f, 1.0f);
		TextRender()->Text(Width - 110.0f, DisplayY, 8.0f, "AUTO AIM: ACTIVE");
		DisplayY += 10.0f;
	}

	if(m_aAvoidEnabled[DummyIdx])
	{
		TextRender()->TextColor(0.0f, 1.0f, 0.0f, 1.0f);
		TextRender()->Text(Width - 110.0f, DisplayY, 8.0f, "AUTO AVOID: ACTIVE");
		DisplayY += 10.0f;
	}

	if(m_aAutoHammerEnabled[DummyIdx])
	{
		TextRender()->TextColor(1.0f, 0.5f, 0.0f, 1.0f);
		TextRender()->Text(Width - 110.0f, DisplayY, 8.0f, "AUTO HAMMER: ACTIVE");
		DisplayY += 10.0f;
	}

	if(m_aAutoLaserEnabled[DummyIdx])
	{
		TextRender()->TextColor(0.0f, 0.8f, 1.0f, 1.0f);
		TextRender()->Text(Width - 110.0f, DisplayY, 8.0f, "AUTO LASER: ACTIVE");
		DisplayY += 10.0f;
	}

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

bool CMyComponent::IsSilentAimActive(int DummyIdx) const
{
	return m_aEnabled[DummyIdx] && m_aAimState[DummyIdx] != STATE_IDLE;
}

vec2 CMyComponent::GetSilentAimVector(int DummyIdx) const
{
	return m_aCurrentAim[DummyIdx];
}

bool CMyComponent::GetHookOverride(int DummyIdx) const
{
	return m_aEnabled[DummyIdx] && m_aHookOverride[DummyIdx];
}

ColorRGBA CMyComponent::GetTargetColor(int ClientId, int DummyIdx) const
{
	if(!m_aEnabled[DummyIdx])
	{
		return ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	}

	if(m_aTargetId[DummyIdx] == ClientId)
	{
		return ColorRGBA(0.0f, 1.0f, 0.0f, 1.0f);
	}

	return ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
}

bool CMyComponent::IsAvoidActive(int DummyIdx) const
{
	return m_aAvoidEnabled[DummyIdx] && m_aAvoidActive[DummyIdx];
}

int CMyComponent::GetAvoidDirection(int DummyIdx) const
{
	return m_aAvoidDirection[DummyIdx];
}