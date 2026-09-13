#include "Enemy.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

void Enemy::Initialize(float groundHeight)
{
    m_groundY = groundHeight;
    m_health = 100;
    m_alive = true;
}

bool Enemy::RayIntersectsAabb(
    const XMFLOAT3& origin,
    const XMFLOAT3& direction) const
{
    XMFLOAT3 minBounds
    {
        -1.7f,
        m_groundY,
        10.2f
    };

    XMFLOAT3 maxBounds
    {
         1.7f,
        m_groundY + 3.6f,
        13.0f
    };

    float tMin = 0.0f;
    float tMax = 1000.0f;

    auto TestAxis =
        [&](float originValue,
            float directionValue,
            float minValue,
            float maxValue) -> bool
    {
        if (fabsf(directionValue) < 0.000001f)
        {
            return originValue >= minValue &&
                   originValue <= maxValue;
        }

        float inverseDirection = 1.0f / directionValue;
        float t1 = (minValue - originValue) * inverseDirection;
        float t2 = (maxValue - originValue) * inverseDirection;

        if (t1 > t2)
            std::swap(t1, t2);

        tMin = (std::max)(tMin, t1);
        tMax = (std::min)(tMax, t2);

        return tMin <= tMax;
    };

    return
        TestAxis(origin.x, direction.x, minBounds.x, maxBounds.x) &&
        TestAxis(origin.y, direction.y, minBounds.y, maxBounds.y) &&
        TestAxis(origin.z, direction.z, minBounds.z, maxBounds.z);
}

bool Enemy::Shoot(
    const XMFLOAT3& rayOrigin,
    const XMFLOAT3& rayDirection)
{
    if (!m_alive)
        return false;

    if (!RayIntersectsAabb(rayOrigin, rayDirection))
        return false;

    m_health -= 25;

    if (m_health <= 0)
    {
        m_health = 0;
        m_alive = false;
    }

    return true;
}

void Enemy::Render(
    Renderer& renderer,
    const XMMATRIX& view,
    const XMMATRIX& projection,
    const XMFLOAT3& cameraPosition)
{
    if (!m_alive)
        return;

    const float z = 11.6f;

    XMFLOAT4 dark{0.20f, 0.34f, 0.40f, 1.0f};
    XMFLOAT4 armor{0.40f, 0.62f, 0.67f, 1.0f};
    XMFLOAT4 sensor{1.00f, 0.28f, 0.08f, 1.0f};

    auto DrawPart =
        [&](float sx, float sy, float sz,
            float x, float y, float partZ,
            const XMFLOAT4& color)
    {
        XMMATRIX world =
            XMMatrixScaling(sx, sy, sz) *
            XMMatrixTranslation(x, m_groundY + y, partZ);

        renderer.DrawCube(
            world,
            view,
            projection,
            color,
            cameraPosition,
            true);
    };

    DrawPart(1.15f, 0.75f, 0.80f,  0.00f, 2.25f, z, armor);
    DrawPart(0.50f, 0.35f, 0.48f,  0.00f, 3.25f, z, dark);
    DrawPart(0.16f, 0.16f, 0.10f,  0.00f, 3.25f, z - 0.53f, sensor);

    DrawPart(0.24f, 0.85f, 0.27f, -0.65f, 0.85f, z, dark);
    DrawPart(0.24f, 0.85f, 0.27f,  0.65f, 0.85f, z, dark);

    DrawPart(0.35f, 0.32f, 0.45f, -1.40f, 2.35f, z, dark);
    DrawPart(0.35f, 0.32f, 0.45f,  1.40f, 2.35f, z, dark);

    DrawPart(0.13f, 0.13f, 0.65f,  0.00f, 2.55f, z - 0.80f, dark);
}
