#include "Scene.h"

#include <filesystem>

using namespace DirectX;

bool Scene::Initialize(
    Renderer& renderer,
    std::string& error)
{
    if (!m_terrain.Initialize(
        renderer,
        220.0f,
        129,
        error))
    {
        return false;
    }

    m_enemy.Initialize(
        m_terrain.HeightAt(
            0.0f,
            11.6f
        )
    );

    // Load the survivor once. It no longer owns movement itself; the Camera
    // is the player controller and pushes its position/yaw/state into this body.
    {
        std::string characterError;
        m_playerCharacterLoaded = m_playerCharacter.Initialize(
            renderer,
            characterError);

        if (!m_playerCharacterLoaded && !characterError.empty())
        {
            OutputDebugStringA(characterError.c_str());
            MessageBoxA(
                renderer.Window(),
                characterError.c_str(),
                "Player survivor load error",
                MB_OK | MB_ICONWARNING);
        }
    }

    const std::string rockObj =
        "assets/models/desert_rock/rock01.obj";

    const std::string rockDiffuse =
        "assets/models/desert_rock/rock_diffuse.png";

    const std::string rockNormal =
        "assets/models/desert_rock/rocktest_normal.png";

    const std::string rockSpecular =
        "assets/models/desert_rock/rocktest_specular.png";

    if (std::filesystem::exists(rockObj))
    {
        std::string rockError;

        m_rockLoaded =
            m_rockModel.Load(
                renderer.Device(),
                rockObj,
                rockDiffuse,
                rockNormal,
                rockSpecular,
                rockError
            );

        if (!m_rockLoaded && !rockError.empty())
        {
            OutputDebugStringA(rockError.c_str());
            MessageBoxA(
                renderer.Window(),
                rockError.c_str(),
                "Desert Rock load error",
                MB_OK | MB_ICONWARNING
            );
        }
    }
    else
    {
        std::string missingMessage =
            "Kon de rots niet vinden:\n" +
            rockObj;

        MessageBoxA(
            renderer.Window(),
            missingMessage.c_str(),
            "Desert Rock ontbreekt",
            MB_OK | MB_ICONWARNING
        );
    }

    m_rocks =
    {
        {{-7.0f, 0.0f, 12.0f}, 3.0f, XMConvertToRadians(20.0f)},
        {{8.0f, 0.0f, 15.0f}, 2.4f, XMConvertToRadians(110.0f)},
        {{-13.0f, 0.0f, 22.0f}, 5.0f, XMConvertToRadians(55.0f)},
        {{15.0f, 0.0f, 25.0f}, 3.8f, XMConvertToRadians(145.0f)},
        {{3.0f, 0.0f, 29.0f}, 1.5f, XMConvertToRadians(210.0f)},
        {{-8.0f, 0.0f, 38.0f}, 7.0f, XMConvertToRadians(265.0f)},
        {{24.0f, 0.0f, 46.0f}, 6.0f, XMConvertToRadians(80.0f)},
        {{-29.0f, 0.0f, 52.0f}, 8.5f, XMConvertToRadians(175.0f)},
        {{33.0f, 0.0f, 61.0f}, 5.5f, XMConvertToRadians(310.0f)},
        {{-17.0f, 0.0f, 70.0f}, 10.0f, XMConvertToRadians(240.0f)},
        {{21.0f, 0.0f, 78.0f}, 9.0f, XMConvertToRadians(30.0f)},
        {{5.0f, 0.0f, 44.0f}, 2.0f, XMConvertToRadians(125.0f)}
    };

    return true;
}

void Scene::UpdatePlayerCharacter(
    Renderer& renderer,
    float deltaTime,
    const XMFLOAT3& feetPosition,
    float yaw,
    bool moving,
    bool sprinting,
    bool grounded)
{
    if (!m_playerCharacterLoaded)
        return;

    m_playerCharacter.UpdateFromPlayer(
        renderer,
        deltaTime,
        feetPosition,
        yaw,
        moving,
        sprinting,
        grounded);
}

float Scene::GroundHeightAt(
    float x,
    float z) const
{
    return m_terrain.HeightAt(x, z);
}

bool Scene::Shoot(
    const XMFLOAT3& origin,
    const XMFLOAT3& direction)
{
    return m_enemy.Shoot(origin, direction);
}

void Scene::Render(
    Renderer& renderer,
    const XMMATRIX& view,
    const XMMATRIX& projection,
    const XMFLOAT3& cameraPosition,
    bool renderPlayerBody,
    bool renderPlayerRifle)
{
    m_terrain.Render(
        renderer,
        view,
        projection,
        cameraPosition
    );

    m_enemy.Render(
        renderer,
        view,
        projection,
        cameraPosition
    );

    if (renderPlayerBody && m_playerCharacterLoaded)
    {
        m_playerCharacter.Render(
            renderer,
            view,
            projection,
            cameraPosition,
            renderPlayerRifle);

        renderer.RestoreWorldPipeline();
    }

    if (m_rockLoaded)
    {
        XMMATRIX normalized =
            m_rockModel.MakeNormalizedTransform(1.0f);

        for (const RockInstance& rock : m_rocks)
        {
            float terrainY =
                m_terrain.HeightAt(
                    rock.position.x,
                    rock.position.z
                );

            float rockYOffset =
                rock.scale * 0.22f;

            XMMATRIX world =
                normalized *
                XMMatrixScaling(
                    rock.scale,
                    rock.scale,
                    rock.scale
                ) *
                XMMatrixRotationY(rock.yaw) *
                XMMatrixTranslation(
                    rock.position.x,
                    terrainY + rockYOffset,
                    rock.position.z
                );

            m_rockModel.Render(
                renderer.Context(),
                world,
                view,
                projection,
                cameraPosition
            );
        }

        renderer.RestoreWorldPipeline();
    }
}
