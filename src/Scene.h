#pragma once

#include "Terrain.h"
#include "Enemy.h"
#include "ObjModel.h"
#include "TestCharacter.h"

#include <DirectXMath.h>
#include <string>
#include <vector>

class Scene
{
public:
    bool Initialize(
        Renderer& renderer,
        std::string& error
    );

    void Render(
        Renderer& renderer,
        const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& projection,
        const DirectX::XMFLOAT3& cameraPosition
    );


    void UpdateTestCharacter(
        Renderer& renderer,
        float deltaTime,
        bool moveForward,
        bool moveBackward,
        bool jumpPressed
    );

    float GroundHeightAt(
        float x,
        float z
    ) const;

    bool Shoot(
        const DirectX::XMFLOAT3& origin,
        const DirectX::XMFLOAT3& direction
    );

    bool EnemyAlive() const
    {
        return m_enemy.IsAlive();
    }

    int EnemyHealth() const
    {
        return m_enemy.Health();
    }

private:
    struct RockInstance
    {
        DirectX::XMFLOAT3 position;

        float scale = 1.0f;
        float yaw = 0.0f;
    };

private:
    Terrain m_terrain;
    Enemy m_enemy;

    ObjModel m_rockModel;
    TestCharacter m_testCharacter;

    bool m_rockLoaded = false;
    bool m_testCharacterLoaded = false;

    std::vector<RockInstance> m_rocks;
};