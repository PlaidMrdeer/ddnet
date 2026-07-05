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

				CWorldCore TempWorld;
				CCharacterCore TempCore;
				TempCore.Init(&TempWorld, Collision());

				if(m_aAvoidTargetDirection[DummyIdx] == 0)
				{
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
	vec2 AimDir = GameClient()->m_Controls.m_aMousePos[DummyIdx];
	if(length(AimDir) < 0.001f) AimDir = vec2(1.0f, 0.0f);
	else AimDir = normalize(AimDir);

	float HookLength = GameClient()->m_aTuning[DummyIdx].m_HookLength;
	if(HookLength < 1.0f) HookLength = 380.0f;
	float HookSpeed = GameClient()->m_aTuning[DummyIdx].m_HookFireSpeed;
	if(HookSpeed <= 0.0f) HookSpeed = 80.0f;

	float HalfAngleRad = (m_aFov[DummyIdx] / 2.0f) * (pi / 180.0f);
	float MinCos = std::cos(HalfAngleRad);

	int CurrentHooked = pLocalChar->Core()->HookedPlayer();
	bool RetainLock = false;

	if (m_aTargetId[DummyIdx] >= 0 && m_aTargetId[DummyIdx] < MAX_CLIENTS)
	{
		int PrevTargetId = m_aTargetId[DummyIdx];
		if (GameClient()->m_aClients[PrevTargetId].m_Active && GameClient()->m_Snap.m_aCharacters[PrevTargetId].m_Active)
		{
			if (CurrentHooked == PrevTargetId)
			{
				RetainLock = true;
			}
			else if (GameClient()->m_Controls.m_aInputData[DummyIdx].m_Hook && 
					 (m_aAimState[DummyIdx] == STATE_HOOKING || m_aAimState[DummyIdx] == STATE_AIMING_IN) &&
					 pLocalChar->Core()->m_HookState != HOOK_RETRACTED && pLocalChar->Core()->m_HookState != HOOK_IDLE)
			{
				RetainLock = true;
			}
		}
	}

	int BestId = -1;
	vec2 BestAimPos = vec2(0.0f, 0.0f);

	if (RetainLock)
	{
		BestId = m_aTargetId[DummyIdx];
		BestAimPos = GameClient()->m_aClients[BestId].m_RenderPos;
	}
	else
	{
		float BestScore = std::numeric_limits<float>::max();

		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (i == LocalId) continue;
			if (!GameClient()->m_aClients[i].m_Active || !GameClient()->m_Snap.m_aCharacters[i].m_Active) continue;
			if (!pLocalChar->CanCollide(i)) continue;

			vec2 TargetPos = GameClient()->m_aClients[i].m_RenderPos;
			vec2 TargetVel = GameClient()->m_aClients[i].m_Predicted.m_Vel;

			vec2 D = TargetPos - LocalPos;
			float a = dot(TargetVel, TargetVel) - HookSpeed * HookSpeed;
			float b = 2.0f * dot(D, TargetVel);
			float c = dot(D, D);

			float t = -1.0f;
			if (std::abs(a) < 1e-4f)
			{
				if (std::abs(b) > 1e-4f)
					t = -c / b;
			}
			else
			{
				float Discriminant = b * b - 4.0f * a * c;
				if (Discriminant >= 0.0f)
				{
					float t1 = (-b - std::sqrt(Discriminant)) / (2.0f * a);
					float t2 = (-b + std::sqrt(Discriminant)) / (2.0f * a);
					if (t1 > 0.0f && t2 > 0.0f) t = std::min(t1, t2);
					else if (t1 > 0.0f) t = t1;
					else if (t2 > 0.0f) t = t2;
				}
			}

			if (t < 0.0f || t > 100.0f)
			{
				t = distance(LocalPos, TargetPos) / HookSpeed;
			}

			vec2 PredictedPos = TargetPos + TargetVel * t;
			float DistToPredicted = distance(LocalPos, PredictedPos);

			if (DistToPredicted > HookLength) continue;

			vec2 DirToPredicted = normalize(PredictedPos - LocalPos);
			float CosTheta = dot(AimDir, DirToPredicted);
			if (CosTheta < MinCos) continue;

			bool PathClear = true;
			if (DistToPredicted > 42.0f)
			{
				vec2 HitPos;
				int WallHit = Collision()->IntersectLineTeleHook(LocalPos + DirToPredicted * 42.0f, LocalPos + DirToPredicted * DistToPredicted, &HitPos, nullptr, nullptr);
				if (WallHit)
				{
					PathClear = false;
				}
			}

			if (!PathClear) continue;

			float AngleDiff = std::acos(std::clamp(CosTheta, -1.0f, 1.0f));
			if (AngleDiff < BestScore)
			{
				BestScore = AngleDiff;
				BestId = i;
				BestAimPos = PredictedPos;
			}
		}
	}

	if (BestId != -1)
	{
		m_aTargetId[DummyIdx] = BestId;
		vec2 TargetVec = BestAimPos - LocalPos;
		bool UserWantsHook = GameClient()->m_Controls.m_aInputData[DummyIdx].m_Hook;

		if (UserWantsHook)
		{
			m_aAimState[DummyIdx] = STATE_HOOKING;
			m_aCurrentAim[DummyIdx] = TargetVec;
			m_aHookOverride[DummyIdx] = true;
		}
		else
		{
			m_aAimState[DummyIdx] = STATE_AIMING_IN;
			m_aCurrentAim[DummyIdx] = mix(m_aCurrentAim[DummyIdx], TargetVec, 0.35f);
			m_aHookOverride[DummyIdx] = false;
		}
	}
	else
	{
		m_aTargetId[DummyIdx] = -1;
		m_aHookOverride[DummyIdx] = false;

		if (m_aAimState[DummyIdx] != STATE_IDLE)
		{
			m_aAimState[DummyIdx] = STATE_AIMING_OUT;
			vec2 PhysMouse = GameClient()->m_Controls.m_aMousePos[DummyIdx];
			m_aCurrentAim[DummyIdx] = mix(m_aCurrentAim[DummyIdx], PhysMouse, 0.25f);
			if (distance(m_aCurrentAim[DummyIdx], PhysMouse) < 5.0f)
			{
				m_aAimState[DummyIdx] = STATE_IDLE;
			}
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