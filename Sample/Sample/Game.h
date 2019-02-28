#pragma once

#include <memory>

namespace dxstg {

class StgObject;
class Player;

void AddObject(std::unique_ptr<StgObject>&& newObject);

Player* GetPlayer();
void SetPlayer(Player*);

struct Input {
	bool left : 1;
	bool right : 1;
	bool up : 1;
	bool down : 1;
};

Input GetInput();

} // end namespace dxstg
