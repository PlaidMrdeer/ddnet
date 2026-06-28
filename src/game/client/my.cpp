#include "my.h"
#include <game/client/gameclient.h>
#include <game/client/components/controls.h>
#include <engine/graphics.h>
#include <base/math.h>
#include <base/vmath.h>
#include <cmath>
#include <algorithm>

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
	}
}

void CMyComponent::OnConsoleInit()
{
	Console()->Register("toggle_silentaim", "", CFGFLAG_CLIENT, ConToggleSilentAim, this, "Toggle Auto Aim");
	Console()->Register("+silentaim", "", CFGFLAG_CLIENT, ConKeySilentAim, this, "Hold Auto Aim");
	Console()->Register("cl_silentaim_fov", "i[fov]", CFGFLAG_CLIENT, ConFovSilentAim, this, "Set Auto Aim FOV");
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

	float BestScore = -1.0f;
	float BestCos = MinCos; 
	int BestId = -1;

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
		if(CosTheta > BestCos)
		{
			float Score = CosTheta * (1.0f - (Dist / HookLength) * 0.15f);
			if(Score > BestScore)
			{
				BestScore = Score;
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
	if(!m_aEnabled[DummyIdx])
	{
		return;
	}

	float Height = 300.0f;
	float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreen(0.0f, 0.0f, Width, Height);

	TextRender()->TextColor(0.0f, 1.0f, 0.0f, 1.0f);
	TextRender()->Text(Width - 110.0f, 150.0f, 8.0f, "AUTO AIM: ACTIVE");
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