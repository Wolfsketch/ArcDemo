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
        float initialGroundHeight,
        std::string& error);

    void Update(
        Renderer& renderer,
        float deltaTime,
        float groundHeight,
        bool moveForward,
        bool moveBackward,
        bool jumpPressed);

    void Render(
        Renderer& renderer,
        const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& projection,
        const DirectX::XMFLOAT3& cameraPosition);

    DirectX::XMFLOAT3 Position() const { return m_position; }
    bool IsLoaded() const { return m_loaded; }

private:
    AnimatedGltfModel m_model;
    GltfModel m_rifle;

    DirectX::XMFLOAT3 m_position{0.0f, 0.0f, 7.0f};
    float m_yaw = DirectX::XM_PI;
    float m_verticalVelocity = 0.0f;
    float m_animationTime = 0.0f;
    float m_motionTime = 0.0f;

    AnimatedGltfModel::Motion m_motion = AnimatedGltfModel::Motion::Idle;

    bool m_onGround = true;
    bool m_loaded = false;
    bool m_rifleLoaded = false;
};
