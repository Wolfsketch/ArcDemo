#include "Game.h"

#include <chrono>
#include <string>

using namespace DirectX;

bool Game::Initialize(
    HINSTANCE instance,
    int showCommand)
{
    std::string error;

    if (!m_renderer.Initialize(
        instance,
        showCommand,
        1280,
        720,
        L"ArcDemo - Modular Desert",
        error))
    {
        MessageBoxA(
            nullptr,
            error.c_str(),
            "ArcDemo startup error",
            MB_OK | MB_ICONERROR);

        return false;
    }

    if (!m_scene.Initialize(m_renderer, error))
    {
        MessageBoxA(
            m_renderer.Window(),
            error.c_str(),
            "Scene startup error",
            MB_OK | MB_ICONERROR);

        return false;
    }

    m_camera.Initialize(m_renderer.Window());

    std::string weaponError;

    if (!m_weapon.Initialize(
        m_renderer,
        "assets/models/weapons/ashfall_r07_game.glb",
        weaponError))
    {
        MessageBoxA(
            m_renderer.Window(),
            weaponError.c_str(),
            "Weapon GLB load error",
            MB_OK | MB_ICONWARNING);
    }

    return true;
}

void Game::UpdateWindowTitle(
    bool fired,
    bool hit)
{
    if (!fired)
        return;

    wchar_t title[160]{};

    if (!m_scene.EnemyAlive())
    {
        swprintf_s(
            title,
            L"ArcDemo - ARC DESTROYED");
    }
    else if (hit)
    {
        swprintf_s(
            title,
            L"ArcDemo - HIT! ARC HP: %d",
            m_scene.EnemyHealth());
    }
    else
    {
        swprintf_s(
            title,
            L"ArcDemo - MISS | ARC HP: %d",
            m_scene.EnemyHealth());
    }

    SetWindowTextW(
        m_renderer.Window(),
        title);
}

int Game::Run()
{
    MSG message{};
    bool running = true;

    auto previousTime =
        std::chrono::steady_clock::now();

    XMMATRIX projection = XMMatrixPerspectiveFovLH(
        XMConvertToRadians(70.0f),
        static_cast<float>(m_renderer.Width()) /
            static_cast<float>(m_renderer.Height()),
        0.05f,
        1000.0f);

    while (running)
    {
        while (PeekMessage(
            &message,
            nullptr,
            0,
            0,
            PM_REMOVE))
        {
            if (message.message == WM_QUIT)
                running = false;

            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        if (!running)
            break;

        auto currentTime =
            std::chrono::steady_clock::now();

        float deltaTime =
            std::chrono::duration<float>(
                currentTime - previousTime).count();

        previousTime = currentTime;

        if (deltaTime > 0.1f)
            deltaTime = 0.1f;

        XMFLOAT3 oldCameraPosition =
            m_camera.Position();

        float groundHeight =
            m_scene.GroundHeightAt(
                oldCameraPosition.x,
                oldCameraPosition.z);

        m_camera.Update(
            deltaTime,
            groundHeight);

        // Re-sample after movement so the player follows the dunes.
        XMFLOAT3 movedPosition =
            m_camera.Position();

        float movedGroundHeight =
            m_scene.GroundHeightAt(
                movedPosition.x,
                movedPosition.z);

        m_camera.ResolveGroundHeight(
            movedGroundHeight);

        bool focused =
            GetForegroundWindow() ==
            m_renderer.Window();

        bool mouseDown =
            focused &&
            ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);

        bool reloadDown =
            focused &&
            ((GetAsyncKeyState('R') & 0x8000) != 0);

        // SPACE is handled by the first-person Camera controller.
        // The imported survivor is no longer a separate NPC/test pawn.

        bool reloadPressed =
            reloadDown &&
            !m_previousReloadDown;

        if (reloadPressed)
            m_weapon.StartReload();

        bool triggerPressed =
            mouseDown &&
            !m_previousMouseDown;

        bool firedThisFrame =
            triggerPressed &&
            m_weapon.CanFire();

        bool hit = false;

        if (firedThisFrame)
        {
            hit = m_scene.Shoot(
                m_camera.Position(),
                m_camera.Forward());
        }

        m_previousMouseDown = mouseDown;
        m_previousReloadDown = reloadDown;

        m_weapon.Update(
            deltaTime,
            m_camera.IsMoving(),
            firedThisFrame);

        UpdateWindowTitle(
            firedThisFrame,
            hit);

        XMMATRIX view =
            m_camera.ViewMatrix();

        XMFLOAT3 cameraPosition =
            m_camera.Position();

        // Bright desert sky. The world shader fades distant geometry
        // toward a matching atmospheric fog color.
        m_renderer.BeginFrame(
            {0.52f, 0.72f, 0.79f, 1.0f});

        m_scene.Render(
            m_renderer,
            view,
            projection,
            cameraPosition);

        m_weapon.Render(m_renderer);

        m_renderer.EndFrame();
    }

    return 0;
}
