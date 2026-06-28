#ifndef GAME_CLIENT_COMPONENTS_MY_H
#define GAME_CLIENT_COMPONENTS_MY_H

#include <game/client/component.h>

class CMyComponent : public CComponent
{
private:
	int m_TargetId = -1;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override { m_TargetId = -1; }
	void OnUpdate() override;
	void OnRender() override;
};

#endif