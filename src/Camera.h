#pragma once

#include <windows.h>
#include <DirectXMath.h>

class Camera
{
public:
    void Initialize(HWND window);

    void Update(
        float deltaTime,
        float groundHeight);

    // Re-sample the terrain after horizontal movement without processing
    // input twice. While grounded the eye follows the terrain; while
    // airborne this also performs the landing test against the new ground.
    void ResolveGroundHeight(float groundHeight);

    DirectX::XMMATRIX ViewMatrix() const;

    DirectX::XMFLOAT3 Position() const { return m_position; }
    DirectX::XMFLOAT3 Forward() const;

    bool IsMoving() const { return m_isMoving; }
    bool IsGrounded() const { return m_onGround; }

private:
    HWND m_window = nullptr;

    DirectX::XMFLOAT3 m_position{0.0f, 1.7f, -8.0f};

    float m_yaw = 0.0f;
    float m_pitch = 0.0f;

    float m_moveSpeed = 5.0f;
    float m_sprintSpeed = 10.0f;
    float m_mouseSensitivity = 0.0025f;

    float m_eyeHeight = 1.7f;
    float m_jumpVelocity = 5.2f;
    float m_verticalVelocity = 0.0f;
    float m_gravity = 9.81f;

    bool m_isMoving = false;
    bool m_onGround = true;
    bool m_previousJumpDown = false;
};
