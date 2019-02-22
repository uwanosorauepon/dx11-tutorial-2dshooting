#pragma once

#include <memory>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace dxstg {

constexpr UINT clientWidth = 640;
constexpr UINT clientHeight = 480;

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
