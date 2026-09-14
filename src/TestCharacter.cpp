#include "TestCharacter.h"

using namespace DirectX;

bool TestCharacter::Initialize(
    Renderer& renderer,
    std::string& error)
{
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

void TestCharacter::UpdateFromPlayer(
    Renderer& renderer,
    float deltaTime,
    const XMFLOAT3& feetPosition,
    float yaw,
    bool moving,
    bool sprinting,
    bool grounded)
{
    if (!m_loaded)
        return;

    m_position = feetPosition;
    m_yaw = yaw;

    const float animationSpeed = sprinting ? 1.65f : 1.0f;
    m_animationTime += deltaTime * animationSpeed;

    AnimatedGltfModel::Motion nextMotion = AnimatedGltfModel::Motion::Idle;

    if (!grounded)
        nextMotion = AnimatedGltfModel::Motion::Jump;
    else if (moving)
        nextMotion = AnimatedGltfModel::Motion::Walk;

    if (nextMotion != m_motion)
    {
        m_motion = nextMotion;
        m_motionTime = 0.0f;
    }
    else
    {
        m_motionTime += deltaTime * animationSpeed;
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
    const XMFLOAT3& cameraPosition,
    bool renderRifle)
{
    if (!m_loaded)
        return;

    // The imported survivor faces the opposite direction of our gameplay
    // forward convention, hence the PI correction here.
    const XMMATRIX placement =
        XMMatrixRotationY(m_yaw + XM_PI) *
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

    // Third-person world weapon. This remains slung for now until the hand
    // attachment/IK layer is implemented.
    if (renderRifle && m_rifleLoaded)
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
