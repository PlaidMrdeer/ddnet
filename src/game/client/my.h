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

    float GetJitterAngle(int DummyIdx) const;

    static void ConToggleSilentAim(IConsole::IResult *pResult, void *pUserData);
    static void ConKeySilentAim(IConsole::IResult *pResult, void *pUserData);

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
};

#endif