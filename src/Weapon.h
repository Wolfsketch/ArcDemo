#pragma once

#include "Model.h"

#include <DirectXMath.h>
#include <string>
#include <vector>

class Renderer;

class Weapon
{
public:
    bool Initialize(
        Renderer& renderer,
        const std::string& glbPath,
        std::string& error);

    void Update(
        float deltaTime,
        bool playerMoving,
        bool firedThisFrame);

    void Render(Renderer& renderer);

    void StartReload();

    bool CanFire() const
    {
        return m_loaded && !m_reloading && m_ammoInMagazine > 0;
    }

    bool IsReloading() const { return m_reloading; }
    bool IsLoaded() const { return m_loaded; }
    int AmmoInMagazine() const { return m_ammoInMagazine; }
    int MagazineCapacity() const { return kMagazineCapacity; }

private:
    struct Casing
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 velocity{};
        DirectX::XMFLOAT3 rotation{};
        DirectX::XMFLOAT3 angularVelocity{};
        float life = 0.0f;
    };

    void SpawnCasing();

private:
    GltfModel m_model;
    bool m_loaded = false;

    float m_recoil = 0.0f;
    float m_animationTime = 0.0f;
    bool m_playerMoving = false;

    static constexpr int kMagazineCapacity = 8;
    int m_ammoInMagazine = kMagazineCapacity;

    bool m_reloading = false;
    float m_reloadTimer = 0.0f;
    static constexpr float kReloadDuration = 1.85f;

    std::vector<Casing> m_casings;
};
