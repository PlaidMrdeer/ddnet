#ifndef GAME_CLIENT_COMPONENTS_MY_H
#define GAME_CLIENT_COMPONENTS_MY_H

#include <game/client/component.h>
#include <engine/client/enums.h>
#include <base/vmath.h>

class CMyComponent : public CComponent
{
public:
	enum EAimState
	{
		STATE_IDLE = 0,
		STATE_AIMING_IN,
		STATE_HOOKING,
		STATE_AIMING_OUT
	};

private:
	int m_aTargetId[NUM_DUMMIES];
	EAimState m_aAimState[NUM_DUMMIES];
	vec2 m_aCurrentAim[NUM_DUMMIES];
	bool m_aHookOverride[NUM_DUMMIES];

	float GetJitterAngle(int DummyIdx) const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnUpdate() override;
	void OnRender() override;

	bool IsSilentAimActive(int DummyIdx) const;
	vec2 GetSilentAimVector(int DummyIdx) const;
	bool GetHookOverride(int DummyIdx) const;
};

#endif