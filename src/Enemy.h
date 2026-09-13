#pragma once

#include "Renderer.h"

class Enemy
{
public:
    void Initialize(float groundHeight);

    bool Shoot(
        const DirectX::XMFLOAT3& rayOrigin,
        const DirectX::XMFLOAT3& rayDirection);

    void Render(
        Renderer& renderer,
        const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& projection,
        const DirectX::XMFLOAT3& cameraPosition);

    bool IsAlive() const { return m_alive; }
    int Health() const { return m_health; }

private:
    bool RayIntersectsAabb(
        const DirectX::XMFLOAT3& origin,
        const DirectX::XMFLOAT3& direction) const;

private:
    float m_groundY = 0.0f;
    int m_health = 100;
    bool m_alive = true;
};
