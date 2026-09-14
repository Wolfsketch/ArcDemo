#pragma once

#include "AnimatedModel.h"
#include "Model.h"
#include "Renderer.h"

#include <DirectXMath.h>
#include <string>

class TestCharacter
{
public:
    bool Initialize(
        Renderer& renderer,
        std::string& error);

    void UpdateFromPlayer(
        Renderer& renderer,
        float deltaTime,
        const DirectX::XMFLOAT3& feetPosition,
        float yaw,
        bool moving,
        bool sprinting,
        bool grounded);

    void Render(
        Renderer& renderer,
        const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& projection,
        const DirectX::XMFLOAT3& cameraPosition,
        bool renderRifle);

    DirectX::XMFLOAT3 Position() const { return m_position; }
    bool IsLoaded() const { return m_loaded; }

private:
    AnimatedGltfModel m_model;
    GltfModel m_rifle;

    DirectX::XMFLOAT3 m_position{0.0f, 0.0f, 0.0f};
    float m_yaw = 0.0f;
    float m_animationTime = 0.0f;
    float m_motionTime = 0.0f;

    AnimatedGltfModel::Motion m_motion = AnimatedGltfModel::Motion::Idle;

    bool m_loaded = false;
    bool m_rifleLoaded = false;
};
