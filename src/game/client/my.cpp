#include "my.h"
#include <game/client/gameclient.h>
#include <game/client/components/controls.h>
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
	}
}

void CMyComponent::OnConsoleInit()
{
	Console()->Register("toggle_silentaim", "", CFGFLAG_CLIENT, ConToggleSilentAim, this, "Toggle Auto Aim");
	Console()->Register("+silentaim", "", CFGFLAG_CLIENT, ConKeySilentAim, this, "Hold Auto Aim");
	Console()->Register("cl_silentaim_fov", "i[fov]", CFGFLAG_CLIENT, ConFovSilentAim, this, "Set Auto Aim FOV");

	Console()->Register("toggle_avoidfreeze", "", CFGFLAG_CLIENT, ConToggleAvoidFreeze, this, "Toggle Avoid Freeze");
	Console()->Register("+avoidfreeze", "", CFGFLAG_CLIENT, ConKeyAvoidFreeze, this, "Hold Avoid Freeze");
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

	if(m_aAvoidEnabled[DummyIdx])
	{
		int LocalId = GameClient()->m_Snap.m_LocalClientId;
		if(LocalId >= 0 && GameClient()->m_Snap.m_aCharacters[LocalId].m_Active)
		{
			vec2 LocalPos = GameClient()->m_aClients[LocalId].m_RenderPos;
			vec2 Vel = GameClient()->m_aClients[LocalId].m_Predicted.m_Vel;

			int InputDir = 0;
			if(GameClient()->m_Controls.m_aInputDirectionLeft[DummyIdx] && !GameClient()->m_Controls.m_aInputDirectionRight[DummyIdx])
				InputDir = -1;
			else if(!GameClient()->m_Controls.m_aInputDirectionLeft[DummyIdx] && GameClient()->m_Controls.m_aInputDirectionRight[DummyIdx])
				InputDir = 1;

			int TravelDir = InputDir;
			if(TravelDir == 0)
			{
				if(Vel.x > 0.5f)
					TravelDir = 1;
				else if(Vel.x < -0.5f)
					TravelDir = -1;
			}

			if(TravelDir != 0)
			{
				int StartTileX = round_to_int(LocalPos.x) / 32;
				int TileY = round_to_int(LocalPos.y) / 32;
				
				bool OnGround = Collision()->IsOnGround(LocalPos, 28.0f);
				float Friction = OnGround ? 0.5f : 0.95f;

				// 【精准计算核心 1】逆向推算当前速度下完全刹车所需的绝对距离 (像素)
				// Teeworlds 物理模型中：X_next = X + Vel_x; Vel_x_next = Vel_x * Friction;
				// 理论刹车总距离公式为：刹车像素 = |Vel.x| * Friction / (1.0f - Friction)
				float BrakeDistance = 0.0f;
				if (std::abs(Vel.x) > 0.1f)
				{
					// 额外附加一个半身宽的缓冲区(14.0f像素)，防止物理Tick采样误差导致边缘滑入
					BrakeDistance = (std::abs(Vel.x) * Friction) / (1.0f - Friction) + 14.0f;
				}

				// 根据精确刹车距离动态计算需要扫描多少个 Tile，至少扫 8 个，至多扫 24 个（应对超高速）
				int ScanTiles = std::max(8, std::min(24, round_to_int(BrakeDistance / 32.0f) + 2));
				int FreezeTileX = -1;

				if(TravelDir == 1)
				{
					for(int tx = StartTileX + 1; tx <= StartTileX + ScanTiles; ++tx)
					{
						bool Found = false;
						for(int ty = TileY - 1; ty <= TileY + 1; ++ty)
						{
							if(ty < 0 || ty >= Collision()->GetHeight())
								continue;
							int Index = ty * Collision()->GetWidth() + tx;
							if(Index >= 0 && Index < Collision()->GetWidth() * Collision()->GetHeight())
							{
								int Tile = Collision()->GetTileIndex(Index);
								int FTile = Collision()->GetFrontTileIndex(Index);
								if(Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE || Tile == TILE_DEATH ||
								   FTile == TILE_FREEZE || FTile == TILE_DFREEZE || FTile == TILE_LFREEZE || FTile == TILE_DEATH)
								{
									Found = true;
									break;
								}
							}
						}
						if(Found)
						{
							FreezeTileX = tx;
							break;
						}
					}
				}
				else if(TravelDir == -1)
				{
					for(int tx = StartTileX - 1; tx >= StartTileX - ScanTiles; --tx)
					{
						bool Found = false;
						for(int ty = TileY - 1; ty <= TileY + 1; ++ty)
						{
							if(ty < 0 || ty >= Collision()->GetHeight())
								continue;
							int Index = ty * Collision()->GetWidth() + tx;
							if(Index >= 0 && Index < Collision()->GetWidth() * Collision()->GetHeight())
							{
								int Tile = Collision()->GetTileIndex(Index);
								int FTile = Collision()->GetFrontTileIndex(Index);
								if(Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE || Tile == TILE_DEATH ||
								   FTile == TILE_FREEZE || FTile == TILE_DFREEZE || FTile == TILE_LFREEZE || FTile == TILE_DEATH)
								{
									Found = true;
									break;
								}
							}
						}
						if(Found)
						{
							FreezeTileX = tx;
							break;
						}
					}
				}

				if(FreezeTileX != -1)
				{
					// 计算到冻结/死亡水体边缘的绝对物理距离
					float TargetX = TravelDir == 1 ? (FreezeTileX * 32.0f) : ((FreezeTileX + 1) * 32.0f);
					float DistToWater = TravelDir == 1 ? (TargetX - LocalPos.x) : (LocalPos.x - TargetX);
					bool WillCross = false;

					// 【精准计算核心 2】当到水边缘的距离小于或等于预测出的刹车距离时，立刻判定需要急停
					if(DistToWater <= BrakeDistance)
					{
						WillCross = true;
					}
					else
					{
						// 多层保障：通过多 Tick 仿真进一步验证极端惯性
						float SimX = LocalPos.x;
						float SimVelX = Vel.x;

						for(int step = 0; step < 30; ++step)
						{
							SimVelX *= Friction;
							SimX += SimVelX;
							if(TravelDir == 1 && SimX >= TargetX - 14.0f)
							{
								WillCross = true;
								break;
							}
							if(TravelDir == -1 && SimX <= TargetX + 14.0f)
							{
								WillCross = true;
								break;
							}
						}
					}

					if(WillCross)
					{
						m_aAvoidActive[DummyIdx] = true;
						// 执行反向最高优先级输入以达到最大制动效果
						if(Vel.x > 0.1f)
							m_aAvoidDirection[DummyIdx] = -1;
						else if(Vel.x < -0.1f)
							m_aAvoidDirection[DummyIdx] = 1;
						else
							m_aAvoidDirection[DummyIdx] = 0;
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

	float HalfAngleRad = (m_aFov[DummyIdx] / 2.0f) * (pi / 180.0f);
	float MinCos = std::cos(HalfAngleRad);

	float BestDistToMouse = std::numeric_limits<float>::max();
	int BestId = -1;
	vec2 MouseWorldPos = LocalPos + GameClient()->m_Controls.m_aMousePos[DummyIdx];

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
		float Dist = distance(LocalPos, TargetPos);
		if(Dist > HookLength)
		{
			continue;
		}

		vec2 ColPos;
		vec2 NewPos;
		if(Collision()->IntersectLine(LocalPos, TargetPos, &ColPos, &NewPos) != 0)
		{
			continue;
		}

		vec2 TargetDir = TargetPos - LocalPos;
		if(length(TargetDir) < 0.001f)
		{
			continue;
		}
		TargetDir = normalize(TargetDir);

		float CosTheta = dot(AimDir, TargetDir);
		if(CosTheta >= MinCos)
		{
			float DistToMouse = distance(TargetPos, MouseWorldPos);
			if(DistToMouse < BestDistToMouse)
			{
				BestDistToMouse = DistToMouse;
				BestId = i;
			}
		}
	}

	m_aTargetId[DummyIdx] = BestId;
	bool UserWantsHook = GameClient()->m_Controls.m_aInputData[DummyIdx].m_Hook;

	bool PhysicalAimCanHook = false;
	if(BestId != -1)
	{
		vec2 TargetPos = GameClient()->m_aClients[BestId].m_RenderPos;
		vec2 ClosestPoint;
		if(closest_point_on_line(LocalPos, LocalPos + AimDir * HookLength, TargetPos, ClosestPoint))
		{
			if(distance(TargetPos, ClosestPoint) < 28.0f)
			{
				PhysicalAimCanHook = true;
			}
		}
	}

	if(PhysicalAimCanHook)
	{
		m_aAimState[DummyIdx] = STATE_IDLE;
		m_aHookOverride[DummyIdx] = false;
	}
	else
	{
		switch(m_aAimState[DummyIdx])
		{
		case STATE_IDLE:
			m_aHookOverride[DummyIdx] = false;
			if(UserWantsHook && m_aTargetId[DummyIdx] != -1)
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
				vec2 TargetDir = GameClient()->m_aClients[m_aTargetId[DummyIdx]].m_RenderPos - LocalPos;
				float Jitter = GetJitterAngle(DummyIdx);
				float TargetAngle = angle(TargetDir) + Jitter;
				vec2 JitteredTarget = direction(TargetAngle) * length(TargetDir);

				m_aCurrentAim[DummyIdx] = mix(m_aCurrentAim[DummyIdx], JitteredTarget, 0.22f);

				if(dot(normalize(m_aCurrentAim[DummyIdx]), normalize(TargetDir)) > 0.992f)
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
				vec2 TargetDir = GameClient()->m_aClients[m_aTargetId[DummyIdx]].m_RenderPos - LocalPos;
				float Jitter = GetJitterAngle(DummyIdx);
				float TargetAngle = angle(TargetDir) + Jitter;
				vec2 JitteredTarget = direction(TargetAngle) * length(TargetDir);

				m_aCurrentAim[DummyIdx] = mix(m_aCurrentAim[DummyIdx], JitteredTarget, 0.35f);
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