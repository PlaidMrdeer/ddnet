#include "my.h"
#include <game/client/gameclient.h>
#include <game/client/components/controls.h>
#include <engine/graphics.h>
#include <base/math.h>
#include <base/vmath.h>

void CMyComponent::OnUpdate()
{
	m_TargetId = -1;

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
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
	vec2 AimDir = GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy];
	if(length(AimDir) < 0.001f)
	{
		return;
	}
	AimDir = normalize(AimDir);

	float HookLength = GameClient()->m_aTuning[g_Config.m_ClDummy].m_HookLength;
	if(HookLength < 1.0f)
	{
		HookLength = 380.0f;
	}

	float BestScore = -1.0f;
	float BestCos = 0.906f; 
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

	if(BestId != -1)
	{
		m_TargetId = BestId;
		if(GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy].m_Hook)
		{
			GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy] = GameClient()->m_aClients[BestId].m_RenderPos - LocalPos;
			GameClient()->m_Controls.m_aMouseInputType[g_Config.m_ClDummy] = CControls::EMouseInputType::ABSOLUTE;
		}
	}
}

void CMyComponent::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
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
	vec2 TargetPos;
	bool Locked = false;

	if(m_TargetId != -1 && GameClient()->m_aClients[m_TargetId].m_Active && GameClient()->m_Snap.m_aCharacters[m_TargetId].m_Active)
	{
		TargetPos = GameClient()->m_aClients[m_TargetId].m_RenderPos;
		Locked = true;
	}
	else
	{
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

		if(ClosestId != -1)
		{
			TargetPos = GameClient()->m_aClients[ClosestId].m_RenderPos;
		}
		else
		{
			return;
		}
	}

	vec2 Center = GameClient()->m_Camera.m_Center;
	float aPoints[4];
	Graphics()->MapScreenToWorld(Center.x, Center.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, aPoints);
	Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);

	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	if(Locked)
	{
		Graphics()->SetColor(0.0f, 1.0f, 0.0f, 1.0f);
	}
	else
	{
		Graphics()->SetColor(1.0f, 0.0f, 0.0f, 1.0f);
	}
	IGraphics::CLineItem Line(LocalPos.x, LocalPos.y, TargetPos.x, TargetPos.y);
	Graphics()->LinesDraw(&Line, 1);
	Graphics()->LinesEnd();

	Graphics()->QuadsBegin();
	if(Locked)
	{
		Graphics()->SetColor(0.0f, 1.0f, 0.0f, 0.8f);
	}
	else
	{
		Graphics()->SetColor(1.0f, 1.0f, 0.0f, 0.8f);
	}
	IGraphics::CQuadItem Quad(TargetPos.x - 8.0f, TargetPos.y - 48.0f, 16.0f, 16.0f);
	Graphics()->QuadsDrawTL(&Quad, 1);
	Graphics()->QuadsEnd();
}