#include "Weapon.h"
#include "Renderer.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

bool Weapon::Initialize(
    Renderer& renderer,
    const std::string& glbPath,
    std::string& error)
{
    m_loaded = m_model.Load(
        renderer.Device(),
        glbPath,
        error);

    m_ammoInMagazine = kMagazineCapacity;
    m_reloading = false;
    m_reloadTimer = 0.0f;
    m_casings.clear();

    return m_loaded;
}

void Weapon::SpawnCasing()
{
    Casing casing{};

    // View-space spawn point close to the ejection-port area.
    casing.position = {0.18f, -0.10f, 0.73f};

    // Eject to the player's right, slightly upward and backward.
    casing.velocity = {0.72f, 0.48f, -0.18f};
    casing.rotation = {0.2f, 0.0f, 0.5f};
    casing.angularVelocity = {9.0f, 15.0f, 12.0f};
    casing.life = 0.85f;

    m_casings.push_back(casing);
}

void Weapon::StartReload()
{
    if (!m_loaded || m_reloading)
        return;

    if (m_ammoInMagazine >= kMagazineCapacity)
        return;

    m_reloading = true;
    m_reloadTimer = 0.0f;
}

void Weapon::Update(
    float deltaTime,
    bool playerMoving,
    bool firedThisFrame)
{
    m_animationTime += deltaTime;
    m_playerMoving = playerMoving;

    if (firedThisFrame && CanFire())
    {
        m_recoil = 1.0f;
        --m_ammoInMagazine;
        SpawnCasing();
    }

    m_recoil -= deltaTime * 7.5f;

    if (m_recoil < 0.0f)
        m_recoil = 0.0f;

    if (m_reloading)
    {
        m_reloadTimer += deltaTime;

        if (m_reloadTimer >= kReloadDuration)
        {
            m_reloadTimer = 0.0f;
            m_reloading = false;
            m_ammoInMagazine = kMagazineCapacity;
        }
    }

    for (Casing& casing : m_casings)
    {
        casing.life -= deltaTime;

        casing.velocity.y -= 1.85f * deltaTime;

        casing.position.x += casing.velocity.x * deltaTime;
        casing.position.y += casing.velocity.y * deltaTime;
        casing.position.z += casing.velocity.z * deltaTime;

        casing.rotation.x += casing.angularVelocity.x * deltaTime;
        casing.rotation.y += casing.angularVelocity.y * deltaTime;
        casing.rotation.z += casing.angularVelocity.z * deltaTime;
    }

    m_casings.erase(
        std::remove_if(
            m_casings.begin(),
            m_casings.end(),
            [](const Casing& casing)
            {
                return casing.life <= 0.0f || casing.position.y < -1.2f;
            }),
        m_casings.end());
}

void Weapon::Render(Renderer& renderer)
{
    if (!m_loaded)
        return;

    renderer.ClearDepthOnly();

    XMMATRIX projection = XMMatrixPerspectiveFovLH(
        XMConvertToRadians(58.0f),
        static_cast<float>(renderer.Width()) /
            static_cast<float>(renderer.Height()),
        0.01f,
        10.0f);

    float bobStrength = m_playerMoving ? 1.0f : 0.0f;

    float bobX =
        sinf(m_animationTime * 8.0f) *
        0.012f *
        bobStrength;

    float bobY =
        fabsf(cosf(m_animationTime * 8.0f)) *
        0.009f *
        bobStrength;

    float idleX =
        sinf(m_animationTime * 1.45f) *
        0.0035f;

    float idleY =
        cosf(m_animationTime * 1.15f) *
        0.0025f;

    float recoilCurve = m_recoil * m_recoil;
    float recoilBack = recoilCurve * 0.105f;
    float recoilUp = recoilCurve * 0.030f;
    float recoilPitchDegrees = recoilCurve * -4.0f;

    // First-pass procedural reload: lower and roll the whole weapon,
    // then return it to the ready position. Later we can animate the
    // magazine/charging handle as separate GLB nodes.
    float reloadArc = 0.0f;

    if (m_reloading)
    {
        float reloadT = std::clamp(
            m_reloadTimer / kReloadDuration,
            0.0f,
            1.0f);

        reloadArc = sinf(reloadT * XM_PI);
    }

    float reloadDown = reloadArc * 0.23f;
    float reloadRight = reloadArc * 0.08f;
    float reloadRollDegrees = reloadArc * 28.0f;
    float reloadPitchDegrees = reloadArc * 7.0f;

    XMMATRIX normalized =
        m_model.MakeNormalizedTransform(1.35f);

    // ASHFALL is authored lengthwise on Blender's X axis.
    XMMATRIX orientation =
        XMMatrixRotationRollPitchYaw(
            XMConvertToRadians(recoilPitchDegrees + reloadPitchDegrees),
            XMConvertToRadians(-90.0f),
            XMConvertToRadians(reloadRollDegrees));

    XMMATRIX position =
        XMMatrixTranslation(
            0.28f + bobX + idleX + reloadRight,
            -0.36f + bobY + idleY + recoilUp - reloadDown,
            1.08f - recoilBack);

    XMMATRIX world =
        normalized *
        orientation *
        position;

    m_model.Render(
        renderer.Context(),
        world,
        XMMatrixIdentity(),
        projection,
        {0,0,0});

    // Render the ejected shell casings in view space. This is deliberately
    // lightweight for the prototype; later we can replace the cube with a
    // dedicated brass casing GLB.
    renderer.RestoreWorldPipeline();

    for (const Casing& casing : m_casings)
    {
        XMMATRIX casingWorld =
            XMMatrixScaling(0.008f, 0.008f, 0.024f) *
            XMMatrixRotationRollPitchYaw(
                casing.rotation.x,
                casing.rotation.y,
                casing.rotation.z) *
            XMMatrixTranslation(
                casing.position.x,
                casing.position.y,
                casing.position.z);

        renderer.DrawCube(
            casingWorld,
            XMMatrixIdentity(),
            projection,
            {0.80f, 0.52f, 0.16f, 1.0f},
            {0,0,0},
            false);
    }

    renderer.RestoreWorldPipeline();
}
