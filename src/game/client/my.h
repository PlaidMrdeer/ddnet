#ifndef GAME_CLIENT_COMPONENTS_MY_H
#define GAME_CLIENT_COMPONENTS_MY_H

#include <game/client/component.h>

class CMyComponent : public CComponent
{
private:
	int m_TargetId = -1;
	bool m_SilentAimActive = false;
	vec2 m_SilentAimVector;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override { m_TargetId = -1; m_SilentAimActive = false; }
	void OnUpdate() override;
	void OnRender() override;

	bool IsSilentAimActive() const { return m_SilentAimActive; }
	vec2 GetSilentAimVector() const { return m_SilentAimVector; }
};

#endif