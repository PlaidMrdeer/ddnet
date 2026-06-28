#ifndef GAME_CLIENT_COMPONENTS_MY_H
#define GAME_CLIENT_COMPONENTS_MY_H

#include <game/client/component.h>

class CMyComponent : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
};

#endif