#ifndef GAME_CLIENT_COMPONENTS_MY_H
#define GAME_CLIENT_COMPONENTS_MY_H

#include <game/client/component.h>
#include <engine/client/enums.h>
#include <engine/console.h>
#include <base/vmath.h>
#include <base/color.h>

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
	bool m_aEnabled[NUM_DUMMIES];
	int m_aFov[NUM_DUMMIES];

	bool m_aAvoidEnabled[NUM_DUMMIES];
	int m_aAvoidDirection[NUM_DUMMIES];
	bool m_aAvoidActive[NUM_DUMMIES];
	int m_aAvoidTargetDirection[NUM_DUMMIES];

	bool m_aAutoHammerEnabled[NUM_DUMMIES];
	bool m_aHammerOverride[NUM_DUMMIES];
	vec2 m_aHammerTarget[NUM_DUMMIES];

	bool m_aAutoLaserEnabled[NUM_DUMMIES];
	bool m_aLaserOverride[NUM_DUMMIES];
	vec2 m_aLaserTarget[NUM_DUMMIES];

	float GetJitterAngle(int DummyIdx) const;

	static void ConToggleSilentAim(IConsole::IResult *pResult, void *pUserData);
	static void ConKeySilentAim(IConsole::IResult *pResult, void *pUserData);
	static void ConFovSilentAim(IConsole::IResult *pResult, void *pUserData);

	static void ConToggleAvoidFreeze(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyAvoidFreeze(IConsole::IResult *pResult, void *pUserData);

	static void ConToggleAutoHammer(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyAutoHammer(IConsole::IResult *pResult, void *pUserData);

	static void ConToggleAutoLaser(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyAutoLaser(IConsole::IResult *pResult, void *pUserData);

	bool IsTargetApproachingFreeze(int TargetId) const;

	bool SimulateLaser(
		int ShooterId,
		vec2 From,
		vec2 Dir,
		float Energy,
		float BounceCost,
		int MaxBounces,
		int TargetId,
		vec2 TargetPos,
		float TargetRadius,
		vec2 *pOutHitPos);

	bool FindLaserTrajectory(
		int ShooterId,
		vec2 ShooterPos,
		int TargetId,
		vec2 TargetPos,
		vec2 TargetVel,
		bool TargetOnGround,
		int DummyIdx,
		vec2 &OutDir);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnConsoleInit() override;
	void OnUpdate() override;
	void OnRender() override;

	bool IsSilentAimActive(int DummyIdx) const;
	vec2 GetSilentAimVector(int DummyIdx) const;
	bool GetHookOverride(int DummyIdx) const;

	bool IsEnabled(int DummyIdx) const { return m_aEnabled[DummyIdx]; }
	int GetTargetId(int DummyIdx) const { return m_aTargetId[DummyIdx]; }
	ColorRGBA GetTargetColor(int ClientId, int DummyIdx) const;

	int GetFov(int DummyIdx) const { return m_aFov[DummyIdx]; }
	void SetFov(int DummyIdx, int Fov) { m_aFov[DummyIdx] = std::clamp(Fov, 1, 360); }

	bool IsAvoidActive(int DummyIdx) const;
	int GetAvoidDirection(int DummyIdx) const;

	bool IsAutoHammerEnabled(int DummyIdx) const { return m_aAutoHammerEnabled[DummyIdx]; }
	bool GetHammerOverride(int DummyIdx) const { return m_aHammerOverride[DummyIdx]; }
	vec2 GetHammerTarget(int DummyIdx) const { return m_aHammerTarget[DummyIdx]; }

	bool IsAutoLaserEnabled(int DummyIdx) const { return m_aAutoLaserEnabled[DummyIdx]; }
	bool GetLaserOverride(int DummyIdx) const { return m_aLaserOverride[DummyIdx]; }
	vec2 GetLaserTarget(int DummyIdx) const { return m_aLaserTarget[DummyIdx]; }
};

#endif