#pragma once

namespace dxstg {

struct Color {
	float r, g, b, a;

	Color() : r(1.f), g(1.f), b(1.f), a(1.f) {}
	Color(float r_, float g_, float b_, float a_ = 1.f)
		: r(r_), g(g_), b(b_), a(a_) {}

	void set(float r_, float g_, float b_, float a_ = 1.f)
	{
		r = r_;
		g = g_;
		b = b_;
		a = a_;
	}
};

struct Rectangle {
	float minX;
	float minY;
	float maxX;
	float maxY;

	bool intersects(const Rectangle& r) const noexcept
	{
		return (minX <= r.maxX && r.minX <= maxX) && (minY <= r.maxY && r.minY <= maxY);
	}
};

class StgObject {
public:
	enum class TextureID {
		XCHU,
		BULLET
	};

	enum class Type {
		PLAYER,
		// PLAYER_BULLET,
		ENEMY
	};

	StgObject(Type type, TextureID textureID)
		: removable(false)
		, m_type(type)
		, m_textureID(textureID) {}

	StgObject(const StgObject&) = delete;
	StgObject& operator = (const StgObject&) = delete;

	virtual ~StgObject() = default;

	virtual void update() = 0;
	virtual void hit(const StgObject& obj) = 0;
	const Rectangle& getHitRect() const noexcept { return hitRect; }
	const Rectangle& getDrawRect() const noexcept { return drawRect; }
	const Color& getColor() const noexcept { return color; }
	bool isMirrorX() const noexcept { return mirrorX; }
	bool isMirrorY() const noexcept { return mirrorY; }
	Type getType() const noexcept { return m_type; }
	TextureID getTextureID() const noexcept { return m_textureID; }
	
	bool removable;

protected:
	Rectangle hitRect;   // “–‚½‚è”»’è—Ìˆæ
	Rectangle drawRect;  // •`‰æ—Ìˆæ
	Color color;
	bool mirrorX = false;
	bool mirrorY = false;

private:
	const Type m_type;
	const TextureID m_textureID;
};


class Player : public StgObject {
public:
	Player();
	virtual ~Player();

	virtual void update() override;
	virtual void hit(const StgObject& obj) override;
	float getY() const noexcept { return m_y; }
private:
	float m_x, m_y;
	void updateRect();
};

class EnemyBullet : public StgObject {
public:
	EnemyBullet(float x, float y);
	virtual ~EnemyBullet() = default;

	virtual void update() override;
	virtual void hit(const StgObject& obj) override;

private:
	float m_x, m_y;
	int m_time;
	void updateRect();
};

class Enemy : public StgObject {
public:
	Enemy(float x, float y);
	virtual ~Enemy() = default;

	virtual void update() override;
	virtual void hit(const StgObject& obj) override;
private:
	float m_x, m_y;
	int m_count;
	void updateRect();
};

} // namespace dxstg
