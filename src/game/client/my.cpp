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
#include <game/mapitems.h>
#include <game/collision.h>

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

float CMyComponent::GetJitterAngle(int DummyIdx) const
{
	float Time = Client()->GlobalTime();
	if(m_aAimState[DummyIdx] == STATE_AIMING_IN)
	{
		return std::sin(Time * 25.0f) * 0.025f + random_float(-0.01f, 0.01f);
	}
	else if(m_aAimState[DummyIdx] == STATE_HOOKING)
	{
		return std::sin(Time * 40.0f) * 0.012f + random_float(-0.005f, 0.005f);
	}
	return 0.0f;
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

			bool OnGround = Collision()->IsOnGround(StartCore.m_Pos, 28.0f);

			if(m_aAvoidTargetDirection[DummyIdx] != 0)
			{
				if(CurrentInput.m_Direction != m_aAvoidTargetDirection[DummyIdx] || CurrentInput.m_Jump != 0 || !OnGround)
				{
					m_aAvoidTargetDirection[DummyIdx] = 0;
				}
			}

			if(OnGround && CurrentInput.m_Jump == 0 && CurrentInput.m_Direction != 0)
			{
				auto IsDanger = [&](vec2 Pos) {
					float r = 14.0f;
					vec2 CheckPoints[4] = {
						Pos + vec2(-r, -r),
						Pos + vec2(r, -r),
						Pos + vec2(-r, r),
						Pos + vec2(r, r)
					};
					for(auto p : CheckPoints) {
						int Index = Collision()->GetPureMapIndex(p);
						int Tile = Collision()->GetTileIndex(Index);
						int FTile = Collision()->GetFrontTileIndex(Index);
						if(Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE || Tile == TILE_DEATH ||
						   FTile == TILE_FREEZE || FTile == TILE_DFREEZE || FTile == TILE_LFREEZE || FTile == TILE_DEATH)
							return true;
					}
					return false;
				};

				if(m_aAvoidTargetDirection[DummyIdx] == 0)
				{
					CWorldCore TempWorld;

					bool PathCDanger = false;
					CCharacterCore CoreC = StartCore;
					CoreC.Init(&TempWorld, Collision());
					for(int i = 0; i < 60; i++)
					{
						CoreC.m_Input = CurrentInput;
						CoreC.Tick(true, !GameClient()->m_GameWorld.m_WorldConfig.m_NoWeakHookAndBounce);
						CoreC.Move();
						CoreC.Quantize();
						if(IsDanger(CoreC.m_Pos)) { PathCDanger = true; break; }
					}

					if(PathCDanger)
					{
						bool PathBDanger = false;
						CCharacterCore CoreB = StartCore;
						CoreB.Init(&TempWorld, Collision());
						for(int i = 0; i < 60; i++)
						{
							CoreB.m_Input = CurrentInput;
							if(i > 0)
							{
								int BrakeDir = 0;
								if(CoreB.m_Vel.x > 0.1f) BrakeDir = -1;
								else if(CoreB.m_Vel.x < -0.1f) BrakeDir = 1;
								CoreB.m_Input.m_Direction = BrakeDir;
							}
							CoreB.Tick(true, !GameClient()->m_GameWorld.m_WorldConfig.m_NoWeakHookAndBounce);
							CoreB.Move();
							CoreB.Quantize();
							if(IsDanger(CoreB.m_Pos)) { PathBDanger = true; break; }
							if(std::abs(CoreB.m_Vel.x) < 0.1f && Collision()->IsOnGround(CoreB.m_Pos, 28.0f)) break;
						}

						if(PathBDanger)
						{
							bool PathADanger = false;
							CCharacterCore CoreA = StartCore;
							CoreA.Init(&TempWorld, Collision());
							for(int i = 0; i < 60; i++)
							{
								int BrakeDir = 0;
								if(CoreA.m_Vel.x > 0.1f) BrakeDir = -1;
								else if(CoreA.m_Vel.x < -0.1f) BrakeDir = 1;
								CoreA.m_Input = CurrentInput;
								CoreA.m_Input.m_Direction = BrakeDir;
								CoreA.Tick(true, !GameClient()->m_GameWorld.m_WorldConfig.m_NoWeakHookAndBounce);
								CoreA.Move();
								CoreA.Quantize();
								if(IsDanger(CoreA.m_Pos)) { PathADanger = true; break; }
								if(std::abs(CoreA.m_Vel.x) < 0.1f && Collision()->IsOnGround(CoreA.m_Pos, 28.0f)) break;
							}

							if(!PathADanger)
							{
								m_aAvoidTargetDirection[DummyIdx] = CurrentInput.m_Direction;
							}
						}
					}
				}

				if(m_aAvoidTargetDirection[DummyIdx] != 0)
				{
					int BrakeDir = 0;
					if(StartCore.m_Vel.x > 0.1f) BrakeDir = -1;
					else if(StartCore.m_Vel.x < -0.1f) BrakeDir = 1;

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
			// 使用预测坐标确保无延迟响应
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
					if (length(TargetPos) < 1.0f) 
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

	if(!m_aEnabled[DummyIdx])
	{
		m_aTargetId[DummyIdx] = -1;
		return;
	}

	int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0)
	{
		return;
	}

	if(!GameClient()->m_Snap.m_aCharacters[LocalId].m_Active)
	{
		return;
	}

	vec2 LocalPos = GameClient()->m_aClients[LocalId].m_RenderPos;
	vec2 AimDir = GameClient()->m_Controls.m_aMousePos[DummyIdx];
	if(length(AimDir) < 0.001f)
	{
		return;
	}
	AimDir = normalize(AimDir);

	float HookLength = GameClient()->m_aTuning[DummyIdx].m_HookLength;
	if(HookLength < 1.0f)
	{
		HookLength = 380.0f;
	}
	float HookSpeed = GameClient()->m_aTuning[DummyIdx].m_HookFireSpeed;
	if(HookSpeed <= 0.0f)
	{
		HookSpeed = 80.0f;
	}
	float Gravity = GameClient()->m_aTuning[DummyIdx].m_Gravity;

	float HalfAngleRad = (m_aFov[DummyIdx] / 2.0f) * (pi / 180.0f);
	float MinCos = std::cos(HalfAngleRad);

	float BestDistToMouse = std::numeric_limits<float>::max();
	int BestId = -1;
	vec2 MouseWorldPos = LocalPos + GameClient()->m_Controls.m_aMousePos[DummyIdx];
	vec2 BestAimDir = AimDir;

	CCharacter *pLocalChar = GameClient()->m_GameWorld.GetCharacterById(LocalId);
	if(!pLocalChar)
	{
		return;
	}

	bool LockedOn = (m_aAimState[DummyIdx] == STATE_AIMING_IN || m_aAimState[DummyIdx] == STATE_HOOKING);

	if(LockedOn)
	{
		BestId = m_aTargetId[DummyIdx];
		if(BestId != -1 && GameClient()->m_aClients[BestId].m_Active && GameClient()->m_Snap.m_aCharacters[BestId].m_Active)
		{
			vec2 TargetPos = GameClient()->m_aClients[BestId].m_RenderPos;
			vec2 TargetVel = GameClient()->m_aClients[BestId].m_Predicted.m_Vel;

			float DistToTarget = distance(LocalPos, TargetPos);
			float TicksToHit = DistToTarget / HookSpeed;

			vec2 FuturePos = TargetPos + TargetVel * TicksToHit;
			FuturePos.y += 0.5f * Gravity * TicksToHit * TicksToHit;

			BestAimDir = normalize(FuturePos - LocalPos);
		}
		else
		{
			BestId = -1;
			LockedOn = false;
		}
	}

	if(!LockedOn)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(i == LocalId)
			{
				continue;
			}

			if(!GameClient()->m_aClients[i].m_Active)
			{
				continue;
			}

			if(!GameClient()->m_Snap.m_aCharacters[i].m_Active)
			{
				continue;
			}

			vec2 TargetPos = GameClient()->m_aClients[i].m_RenderPos;
			vec2 TargetVel = GameClient()->m_aClients[i].m_Predicted.m_Vel;

			float DistToTarget = distance(LocalPos, TargetPos);
			float TicksToHit = DistToTarget / HookSpeed;

			vec2 FuturePos = TargetPos + TargetVel * TicksToHit;
			FuturePos.y += 0.5f * Gravity * TicksToHit * TicksToHit;

			vec2 ToFutureTarget = FuturePos - LocalPos;
			float FutureDist = length(ToFutureTarget);

			if(FutureDist > HookLength + 28.0f || FutureDist < 0.001f)
			{
				continue;
			}

			vec2 DirToFutureTarget = normalize(ToFutureTarget);

			float CosTheta = dot(AimDir, DirToFutureTarget);
			if(CosTheta < MinCos)
			{
				continue;
			}

			float DistToMouse = distance(FuturePos, MouseWorldPos);
			if(DistToMouse >= BestDistToMouse)
			{
				continue;
			}

			float BaseAngle = angle(ToFutureTarget);
			float MaxAngle = std::asin(std::clamp(28.0f / FutureDist, 0.0f, 1.0f));

			bool Hookable = false;
			vec2 BestTargetAimDir = DirToFutureTarget;
			float BestTargetScore = std::numeric_limits<float>::max();

			const int NUM_STEPS = 15;
			for(int step = 0; step <= NUM_STEPS; step++)
			{
				float Offset = -MaxAngle + (MaxAngle * 2.0f * step / NUM_STEPS);
				float TestAngle = BaseAngle + Offset;
				vec2 TestAimDir = direction(TestAngle);

				vec2 ClosestPoint;
				closest_point_on_line(LocalPos, LocalPos + TestAimDir * HookLength, FuturePos, ClosestPoint);

				if(distance(FuturePos, ClosestPoint) < 28.0f)
				{
					vec2 StartPos = LocalPos + TestAimDir * 42.0f;
					vec2 HitPos;
					int WallHit = Collision()->IntersectLineTeleHook(StartPos, ClosestPoint, &HitPos, nullptr, nullptr);
					
					if(!WallHit)
					{
						Hookable = true;
						float AngleDiff = std::abs(angle(TestAimDir) - angle(AimDir));
						while(AngleDiff > pi) AngleDiff -= 2.0f * pi;
						while(AngleDiff < -pi) AngleDiff += 2.0f * pi;
						AngleDiff = std::abs(AngleDiff);

						if(AngleDiff < BestTargetScore)
						{
							BestTargetScore = AngleDiff;
							BestTargetAimDir = TestAimDir;
						}
					}
				}
			}

			if(Hookable)
			{
				BestDistToMouse = DistToMouse;
				BestId = i;
				BestAimDir = BestTargetAimDir;
			}
		}
	}

	m_aTargetId[DummyIdx] = BestId;
	bool UserWantsHook = GameClient()->m_Controls.m_aInputData[DummyIdx].m_Hook;

	bool PhysicalAimCanHook = false;
	if(!LockedOn && BestId != -1)
	{
		vec2 StartPos = LocalPos + AimDir * 42.0f;
		vec2 EndPos = LocalPos + AimDir * HookLength;
		vec2 HitPos;
		int WallHit = Collision()->IntersectLineTeleHook(StartPos, EndPos, &HitPos, nullptr, nullptr);
		vec2 RealEnd = WallHit ? HitPos : EndPos;

		vec2 IntersectPos;
		CCharacter *pHitChar = GameClient()->m_GameWorld.IntersectCharacter(LocalPos, RealEnd, 0.0f, IntersectPos, pLocalChar, LocalId);

		if(pHitChar && pHitChar->GetCid() == BestId)
		{
			PhysicalAimCanHook = true;
		}
	}

	if(PhysicalAimCanHook && m_aAimState[DummyIdx] == STATE_IDLE)
	{
		m_aAimState[DummyIdx] = STATE_IDLE;
		m_aHookOverride[DummyIdx] = false;
		m_aCurrentAim[DummyIdx] = GameClient()->m_Controls.m_aMousePos[DummyIdx];
	}
	else
	{
		float CurrentLen = length(m_aCurrentAim[DummyIdx]);
		if(CurrentLen < 0.001f)
		{
			CurrentLen = 200.0f;
		}

		switch(m_aAimState[DummyIdx])
		{
		case STATE_IDLE:
			m_aHookOverride[DummyIdx] = false;
			if(UserWantsHook && m_aTargetId[DummyIdx] != -1 && pLocalChar->Core()->m_HookState == 0)
			{
				m_aAimState[DummyIdx] = STATE_AIMING_IN;
				m_aCurrentAim[DummyIdx] = GameClient()->m_Controls.m_aMousePos[DummyIdx];
			}
			break;

		case STATE_AIMING_IN:
			m_aHookOverride[DummyIdx] = false;
			if(!UserWantsHook || m_aTargetId[DummyIdx] == -1)
			{
				m_aAimState[DummyIdx] = STATE_AIMING_OUT;
			}
			else
			{
				float Jitter = GetJitterAngle(DummyIdx);
				float TargetAngle = angle(BestAimDir) + Jitter;
				vec2 JitteredTarget = direction(TargetAngle) * CurrentLen;

				m_aCurrentAim[DummyIdx] = mix(m_aCurrentAim[DummyIdx], JitteredTarget, 0.40f);

				if(dot(normalize(m_aCurrentAim[DummyIdx]), normalize(BestAimDir)) > 0.990f)
				{
					m_aAimState[DummyIdx] = STATE_HOOKING;
				}
			}
			break;

		case STATE_HOOKING:
			m_aHookOverride[DummyIdx] = true;
			if(!UserWantsHook || m_aTargetId[DummyIdx] == -1)
			{
				m_aHookOverride[DummyIdx] = false;
				m_aAimState[DummyIdx] = STATE_AIMING_OUT;
			}
			else
			{
				float Jitter = GetJitterAngle(DummyIdx);
				float TargetAngle = angle(BestAimDir) + Jitter;
				vec2 JitteredTarget = direction(TargetAngle) * CurrentLen;

				m_aCurrentAim[DummyIdx] = mix(m_aCurrentAim[DummyIdx], JitteredTarget, 0.60f);
			}
			break;

		case STATE_AIMING_OUT:
			m_aHookOverride[DummyIdx] = false;
			{
				vec2 PhysMouse = GameClient()->m_Controls.m_aMousePos[DummyIdx];
				m_aCurrentAim[DummyIdx] = mix(m_aCurrentAim[DummyIdx], PhysMouse, 0.20f);

				if(distance(m_aCurrentAim[DummyIdx], PhysMouse) < 8.0f)
				{
					m_aAimState[DummyIdx] = STATE_IDLE;
				}
			}
			break;
		}
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
		TextRender()->TextColor(0.0f, 0.8f, 1.0f, 1.0f);
		TextRender()->Text(Width - 110.0f, DisplayY, 8.0f, "AVOID FREEZE: ACTIVE");
		DisplayY += 10.0f;
	}

	if(m_aAutoHammerEnabled[DummyIdx])
	{
		TextRender()->TextColor(1.0f, 0.5f, 0.0f, 1.0f);
		TextRender()->Text(Width - 110.0f, DisplayY, 8.0f, "AUTO HAMMER: ACTIVE");
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

	if(m_aTargetId[DummyIdx] == -1)
	{
		int LocalId = GameClient()->m_Snap.m_LocalClientId;
		if(LocalId >= 0 && GameClient()->m_Snap.m_aCharacters[LocalId].m_Active)
		{
			vec2 LocalPos = GameClient()->m_aClients[LocalId].m_RenderPos;
			float ClosestDist = -1.0f;
			int ClosestId = -1;

			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(i == LocalId)
				{
					continue;
				}
				if(!GameClient()->m_aClients[i].m_Active)
				{
					continue;
				}
				if(!GameClient()->m_Snap.m_aCharacters[i].m_Active)
				{
					continue;
				}

				float Dist = distance(LocalPos, GameClient()->m_aClients[i].m_RenderPos);
				if(ClosestDist < 0.0f || Dist < ClosestDist)
				{
					ClosestDist = Dist;
					ClosestId = i;
				}
			}
			if(ClosestId == ClientId)
			{
				return ColorRGBA(1.0f, 1.0f, 0.0f, 1.0f);
			}
		}
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