#include "TestCharacter.h"

#include <algorithm>

using namespace DirectX;

bool TestCharacter::Initialize(
    Renderer& renderer,
    float initialGroundHeight,
    std::string& error)
{
    m_position = {0.0f, initialGroundHeight, 7.0f};
    m_yaw = XM_PI;

    if (!m_model.Load(
        renderer.Device(),
        "assets/models/characters/apocalyptic_survivor_test.glb",
        error))
    {
        return false;
    }

    std::string rifleError;
    m_rifleLoaded = m_rifle.Load(
        renderer.Device(),
        "assets/models/weapons/ashfall_r07_world.glb",
        rifleError);

    if (!m_rifleLoaded && !rifleError.empty())
        OutputDebugStringA(rifleError.c_str());

    m_model.Update(
        renderer.Context(),
        0.0f,
        AnimatedGltfModel::Motion::Idle,
        0.0f);

    m_loaded = true;
    return true;
}

void TestCharacter::Update(
    Renderer& renderer,
    float deltaTime,
    float groundHeight,
    bool moveForward,
    bool moveBackward,
    bool jumpPressed)
{
    if (!m_loaded)
        return;

    m_animationTime += deltaTime;

    float direction = 0.0f;
    if (moveForward)
        direction += 1.0f;
    if (moveBackward)
        direction -= 1.0f;

    if (direction != 0.0f)
    {
        const float walkSpeed = 1.8f;
        m_position.z += direction * walkSpeed * deltaTime;
        m_yaw = direction > 0.0f ? 0.0f : XM_PI;
    }

    if (jumpPressed && m_onGround)
    {
        m_verticalVelocity = 4.6f;
        m_onGround = false;
    }

    if (!m_onGround)
    {
        m_verticalVelocity -= 9.81f * deltaTime;
        m_position.y += m_verticalVelocity * deltaTime;

        if (m_position.y <= groundHeight)
        {
            m_position.y = groundHeight;
            m_verticalVelocity = 0.0f;
            m_onGround = true;
        }
    }
    else
    {
        m_position.y = groundHeight;
    }

    AnimatedGltfModel::Motion nextMotion = AnimatedGltfModel::Motion::Idle;

    if (!m_onGround)
        nextMotion = AnimatedGltfModel::Motion::Jump;
    else if (direction != 0.0f)
        nextMotion = AnimatedGltfModel::Motion::Walk;

    if (nextMotion != m_motion)
    {
        m_motion = nextMotion;
        m_motionTime = 0.0f;
    }
    else
    {
        m_motionTime += deltaTime;
    }

    m_model.Update(
        renderer.Context(),
        m_animationTime,
        m_motion,
        m_motionTime);
}

void TestCharacter::Render(
    Renderer& renderer,
    const XMMATRIX& view,
    const XMMATRIX& projection,
    const XMFLOAT3& cameraPosition)
{
    if (!m_loaded)
        return;

    const XMMATRIX placement =
        XMMatrixRotationY(m_yaw) *
        XMMatrixTranslation(
            m_position.x,
            m_position.y,
            m_position.z);

    const XMMATRIX characterWorld =
        m_model.MakeGroundedTransform(1.80f) *
        placement;

    m_model.Render(
        renderer.Context(),
        characterWorld,
        view,
        projection,
        cameraPosition);

    // Test carry setup: ASHFALL is slung diagonally across the survivor's back.
    // This deliberately keeps the locomotion test independent from hand IK.
    // A proper two-hand aim/fire layer can be added after this quality test.
    if (m_rifleLoaded)
    {
        const XMMATRIX rifleWorld =
            m_rifle.MakeNormalizedTransform(0.92f) *
            XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(8.0f),
                XMConvertToRadians(-90.0f),
                XMConvertToRadians(28.0f)) *
            XMMatrixTranslation(
                -0.16f,
                1.18f,
                -0.20f) *
            placement;

        m_rifle.Render(
            renderer.Context(),
            rifleWorld,
            view,
            projection,
            cameraPosition);
    }

    renderer.RestoreWorldPipeline();
}
