#include "my.h"
#include <game/client/gameclient.h>
#include <engine/graphics.h>
#include <base/math.h>
#include <base/vmath.h>

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
		vec2 TargetPos = GameClient()->m_aClients[ClosestId].m_RenderPos;

		vec2 Center = GameClient()->m_Camera.m_Center;
		float aPoints[4];
		Graphics()->MapScreenToWorld(Center.x, Center.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, aPoints);
		Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);

		Graphics()->TextureClear();
		Graphics()->LinesBegin();
		Graphics()->SetColor(1.0f, 0.0f, 0.0f, 1.0f);
		IGraphics::CLineItem Line(LocalPos.x, LocalPos.y, TargetPos.x, TargetPos.y);
		Graphics()->LinesDraw(&Line, 1);
		Graphics()->LinesEnd();

		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 0.0f, 0.8f);
		IGraphics::CQuadItem Quad(TargetPos.x - 8.0f, TargetPos.y - 48.0f, 16.0f, 16.0f);
		Graphics()->QuadsDrawTL(&Quad, 1);
		Graphics()->QuadsEnd();
	}
}