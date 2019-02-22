#include "StgObject.h"
#include "Game.h"

namespace dxstg {

Player::Player()
	: StgObject(Type::PLAYER, TextureID::XCHU)
	, m_x(0)
	, m_y(0)
{
	updateRect();
}

Player::~Player()
{
	SetPlayer(nullptr);
}

void Player::update()
{
	if (GetInput().left)  m_x -= 0.05f;
	if (GetInput().right) m_x += 0.05f;
	if (GetInput().up)    m_y += 0.05f;
	if (GetInput().down)  m_y -= 0.05f;
	updateRect();
}

void Player::hit(const StgObject& obj)
{
	if (obj.getType() == Type::ENEMY) {
		removable = true;
	}
}

void Player::updateRect()
{
	drawRect.minX = hitRect.minX = m_x - 0.5f;
	drawRect.maxX = hitRect.maxX = m_x + 0.5f;
	drawRect.minY = hitRect.minY = m_y - 0.5f;
	drawRect.maxY = hitRect.maxY = m_y + 0.5f;
}

EnemyBullet::EnemyBullet(float x, float y)
	: StgObject(Type::ENEMY, TextureID::BULLET)
	, m_x(x)
	, m_y(y)
	, m_time(60)
{
	updateRect();
}

void EnemyBullet::update()
{
	m_x -= 0.1f;
	updateRect();

	if (--m_time <= 0) {
		removable = true;
	}
}

void EnemyBullet::hit(const StgObject& obj)
{

}

void EnemyBullet::updateRect()
{
	drawRect.maxX = hitRect.maxX = m_x + 0.3f;
	drawRect.minX = hitRect.minX = m_x - 0.3f;
	drawRect.maxY = hitRect.maxY = m_y + 0.15f;
	drawRect.minY = hitRect.minY = m_y - 0.15f;
}

Enemy::Enemy(float x, float y)
	: StgObject(Type::ENEMY, TextureID::XCHU)
	, m_x(x)
	, m_y(y)
	, m_count(0)
{
	updateRect();
	color.set(1.f, 0.3f, 0.0f);
}


void Enemy::update()
{
	constexpr float speed = 0.01f;
	const auto player = GetPlayer();
	if (player != nullptr) {
		float delta = player->getY() - m_y;
		if (delta > speed) {
			delta = speed;
		} else if (delta < -speed) {
			delta = -speed;
		}
		m_y += delta;
		updateRect();
	}


	if (++m_count >= 60) {
		m_count = 0;
		AddObject(std::make_unique<EnemyBullet>(m_x, m_y));
	}
}

void Enemy::hit(const StgObject& obj)
{

}

void Enemy::updateRect()
{
	drawRect.maxX = hitRect.maxX = m_x + 0.75f;
	drawRect.minX = hitRect.minX = m_x - 0.75f;
	drawRect.maxY = hitRect.maxY = m_y + 0.75f;
	drawRect.minY = hitRect.minY = m_y - 0.75f;
}

} // end namespace dxstg