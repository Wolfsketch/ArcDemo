#include "Camera.h"

#include <cmath>

using namespace DirectX;

void Camera::Initialize(HWND window)
{
    m_window = window;
}

XMFLOAT3 Camera::Forward() const
{
    XMFLOAT3 result{};

    XMVECTOR forward = XMVectorSet(
        sinf(m_yaw) * cosf(m_pitch),
        sinf(m_pitch),
        cosf(m_yaw) * cosf(m_pitch),
        0.0f);

    forward = XMVector3Normalize(forward);
    XMStoreFloat3(&result, forward);

    return result;
}

void Camera::Update(
    float deltaTime,
    float groundHeight)
{
    if (!m_window)
        return;

    const bool focused = GetForegroundWindow() == m_window;

    if (focused)
    {
        RECT clientRect{};
        GetClientRect(m_window, &clientRect);

        POINT center
        {
            (clientRect.right - clientRect.left) / 2,
            (clientRect.bottom - clientRect.top) / 2
        };

        ClientToScreen(m_window, &center);

        POINT mouse{};
        GetCursorPos(&mouse);

        const int deltaX = mouse.x - center.x;
        const int deltaY = mouse.y - center.y;

        m_yaw += deltaX * m_mouseSensitivity;
        m_pitch -= deltaY * m_mouseSensitivity;

        const float pitchLimit = XMConvertToRadians(89.0f);

        if (m_pitch > pitchLimit)
            m_pitch = pitchLimit;

        if (m_pitch < -pitchLimit)
            m_pitch = -pitchLimit;

        SetCursorPos(center.x, center.y);
    }

    XMVECTOR worldUp = XMVectorSet(0, 1, 0, 0);

    XMVECTOR flatForward = XMVectorSet(
        sinf(m_yaw),
        0.0f,
        cosf(m_yaw),
        0.0f);

    flatForward = XMVector3Normalize(flatForward);

    XMVECTOR flatRight = XMVector3Normalize(
        XMVector3Cross(worldUp, flatForward));

    XMVECTOR position = XMLoadFloat3(&m_position);

    m_isMoving = false;
    m_isSprinting = false;

    if (focused)
    {
        float speed = m_moveSpeed;

        if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        {
            speed = m_sprintSpeed;
            m_isSprinting = true;
        }

        if (GetAsyncKeyState('W') & 0x8000)
        {
            position += flatForward * speed * deltaTime;
            m_isMoving = true;
        }

        if (GetAsyncKeyState('S') & 0x8000)
        {
            position -= flatForward * speed * deltaTime;
            m_isMoving = true;
        }

        if (GetAsyncKeyState('D') & 0x8000)
        {
            position += flatRight * speed * deltaTime;
            m_isMoving = true;
        }

        if (GetAsyncKeyState('A') & 0x8000)
        {
            position -= flatRight * speed * deltaTime;
            m_isMoving = true;
        }

        if (!m_isMoving)
            m_isSprinting = false;
    }

    XMStoreFloat3(&m_position, position);

    const bool jumpDown =
        focused &&
        ((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0);

    const bool jumpPressed = jumpDown && !m_previousJumpDown;
    m_previousJumpDown = jumpDown;

    if (jumpPressed && m_onGround)
    {
        m_verticalVelocity = m_jumpVelocity;
        m_onGround = false;
    }

    const float targetEyeY = groundHeight + m_eyeHeight;

    if (!m_onGround)
    {
        m_verticalVelocity -= m_gravity * deltaTime;
        m_position.y += m_verticalVelocity * deltaTime;

        if (m_position.y <= targetEyeY && m_verticalVelocity <= 0.0f)
        {
            m_position.y = targetEyeY;
            m_verticalVelocity = 0.0f;
            m_onGround = true;
        }
    }
    else
    {
        m_position.y = targetEyeY;
    }
}

void Camera::ResolveGroundHeight(float groundHeight)
{
    const float targetEyeY = groundHeight + m_eyeHeight;

    if (m_onGround)
    {
        m_position.y = targetEyeY;
        return;
    }

    if (m_verticalVelocity <= 0.0f && m_position.y <= targetEyeY)
    {
        m_position.y = targetEyeY;
        m_verticalVelocity = 0.0f;
        m_onGround = true;
    }
}

XMMATRIX Camera::ViewMatrix() const
{
    XMVECTOR position = XMLoadFloat3(&m_position);
    XMFLOAT3 forwardFloat = Forward();
    XMVECTOR forward = XMLoadFloat3(&forwardFloat);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    return XMMatrixLookToLH(position, forward, up);
}

XMFLOAT3 Camera::ThirdPersonPosition(
    float distance,
    float height) const
{
    const XMFLOAT3 forwardFloat = Forward();
    XMVECTOR forward = XMLoadFloat3(&forwardFloat);
    XMVECTOR eye = XMLoadFloat3(&m_position);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    XMVECTOR thirdPerson = eye - forward * distance + up * height;

    XMFLOAT3 result{};
    XMStoreFloat3(&result, thirdPerson);
    return result;
}

XMMATRIX Camera::ThirdPersonViewMatrix(
    float distance,
    float height) const
{
    const XMFLOAT3 cameraPosition = ThirdPersonPosition(distance, height);

    XMVECTOR eye = XMLoadFloat3(&cameraPosition);
    XMVECTOR target = XMLoadFloat3(&m_position);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    return XMMatrixLookAtLH(eye, target, up);
}
