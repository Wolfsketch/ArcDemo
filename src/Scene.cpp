#include "Scene.h"

#include <filesystem>

using namespace DirectX;

bool Scene::Initialize(
    Renderer& renderer,
    std::string& error)
{
    // ========================================================
    // Terrain
    // ========================================================

    if (!m_terrain.Initialize(
        renderer,
        220.0f,
        129,
        error))
    {
        return false;
    }

    // ========================================================
    // Enemy
    // ========================================================

    m_enemy.Initialize(
        m_terrain.HeightAt(
            0.0f,
            11.6f
        )
    );

    // ========================================================
    // Player character
    // ========================================================
    // The downloaded survivor is intended to become the PLAYER body, not a
    // second pawn standing in the world.  In first-person we therefore do
    // not spawn/render the old test character here.  The camera is the
    // player controller; a first-person body can be attached to it later.
    m_testCharacterLoaded = false;

    // ========================================================
    // Desert Rock OBJ
    // ========================================================

    const std::string rockObj =
        "assets/models/desert_rock/rock01.obj";

    const std::string rockDiffuse =
        "assets/models/desert_rock/rock_diffuse.png";

    const std::string rockNormal =
        "assets/models/desert_rock/rocktest_normal.png";

    const std::string rockSpecular =
        "assets/models/desert_rock/rocktest_specular.png";

    // Check that the OBJ actually exists.
    if (std::filesystem::exists(
        rockObj))
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

        if (!m_rockLoaded)
        {
            if (!rockError.empty())
            {
                OutputDebugStringA(
                    rockError.c_str()
                );

                MessageBoxA(
                    renderer.Window(),
                    rockError.c_str(),
                    "Desert Rock load error",
                    MB_OK |
                    MB_ICONWARNING
                );
            }
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
            MB_OK |
            MB_ICONWARNING
        );
    }

    // ========================================================
    // Rock placements
    //
    // Dit zijn GEEN aparte modellen.
    // We laden rock01.obj maar 1 keer en tekenen hem daarna
    // meerdere keren met andere positie / schaal / rotatie.
    // ========================================================

    m_rocks =
    {
        // Dichtbij links
        {
            {-7.0f, 0.0f, 12.0f},
            3.0f,
            XMConvertToRadians(20.0f)
        },

        // Dichtbij rechts
        {
            {8.0f, 0.0f, 15.0f},
            2.4f,
            XMConvertToRadians(110.0f)
        },

        // Links van enemy
        {
            {-13.0f, 0.0f, 22.0f},
            5.0f,
            XMConvertToRadians(55.0f)
        },

        // Rechts van enemy
        {
            {15.0f, 0.0f, 25.0f},
            3.8f,
            XMConvertToRadians(145.0f)
        },

        // Kleine steen
        {
            {3.0f, 0.0f, 29.0f},
            1.5f,
            XMConvertToRadians(210.0f)
        },

        // Groot midden-links
        {
            {-8.0f, 0.0f, 38.0f},
            7.0f,
            XMConvertToRadians(265.0f)
        },

        // Groot rechts
        {
            {24.0f, 0.0f, 46.0f},
            6.0f,
            XMConvertToRadians(80.0f)
        },

        // Ver links
        {
            {-29.0f, 0.0f, 52.0f},
            8.5f,
            XMConvertToRadians(175.0f)
        },

        // Ver rechts
        {
            {33.0f, 0.0f, 61.0f},
            5.5f,
            XMConvertToRadians(310.0f)
        },

        // Achtergrond
        {
            {-17.0f, 0.0f, 70.0f},
            10.0f,
            XMConvertToRadians(240.0f)
        },

        // Achtergrond rechts
        {
            {21.0f, 0.0f, 78.0f},
            9.0f,
            XMConvertToRadians(30.0f)
        },

        // Klein detail
        {
            {5.0f, 0.0f, 44.0f},
            2.0f,
            XMConvertToRadians(125.0f)
        }
    };

    return true;
}

// ============================================================
// Animated survivor test controls
// ============================================================

void Scene::UpdateTestCharacter(
    Renderer& renderer,
    float deltaTime,
    bool moveForward,
    bool moveBackward,
    bool jumpPressed)
{
    if (!m_testCharacterLoaded)
        return;

    const XMFLOAT3 position = m_testCharacter.Position();
    const float groundHeight = m_terrain.HeightAt(position.x, position.z);

    m_testCharacter.Update(
        renderer,
        deltaTime,
        groundHeight,
        moveForward,
        moveBackward,
        jumpPressed);
}

// ============================================================
// Terrain height
// ============================================================

float Scene::GroundHeightAt(
    float x,
    float z) const
{
    return
        m_terrain.HeightAt(
            x,
            z
        );
}

// ============================================================
// Shooting
// ============================================================

bool Scene::Shoot(
    const XMFLOAT3& origin,
    const XMFLOAT3& direction)
{
    return
        m_enemy.Shoot(
            origin,
            direction
        );
}

// ============================================================
// Render complete scene
// ============================================================

void Scene::Render(
    Renderer& renderer,
    const XMMATRIX& view,
    const XMMATRIX& projection,
    const XMFLOAT3& cameraPosition)
{
    // ========================================================
    // Terrain
    // ========================================================

    m_terrain.Render(
        renderer,
        view,
        projection,
        cameraPosition
    );

    // ========================================================
    // Enemy
    // ========================================================

    m_enemy.Render(
        renderer,
        view,
        projection,
        cameraPosition
    );

    // ========================================================
    // Real OBJ rocks
    // ========================================================

    if (m_rockLoaded)
    {
        // Normalize the original OBJ first.
        //
        // Hierdoor maakt het niet uit of rock01.obj oorspronkelijk
        // 0.2 units of 100 units groot geëxporteerd werd.
        XMMATRIX normalized =
            m_rockModel.MakeNormalizedTransform(
                1.0f
            );

        for (const RockInstance& rock :
             m_rocks)
        {
            // Terrain hoogte op deze X/Z positie.
            float terrainY =
                m_terrain.HeightAt(
                    rock.position.x,
                    rock.position.z
                );

            // Omdat MakeNormalizedTransform het model rond zijn
            // middelpunt centreert, zetten we hem iets hoger zodat
            // hij niet half onder het zand verdwijnt.
            float rockYOffset =
                rock.scale *
                0.22f;

            XMMATRIX world =
                normalized
                *
                XMMatrixScaling(
                    rock.scale,
                    rock.scale,
                    rock.scale
                )
                *
                XMMatrixRotationY(
                    rock.yaw
                )
                *
                XMMatrixTranslation(
                    rock.position.x,
                    terrainY +
                        rockYOffset,
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

        // ObjModel heeft zijn eigen shader gebruikt.
        // Herstel daarna onze normale wereldpipeline.
        renderer.RestoreWorldPipeline();
    }
}