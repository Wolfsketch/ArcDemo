#include "Renderer.h"

#include <d3dcompiler.h>
#include <cstring>
#include <iterator>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

namespace
{
    template<typename T>
    void SafeRelease(T*& object)
    {
        if (object)
        {
            object->Release();
            object = nullptr;
        }
    }
}

Renderer::~Renderer()
{
    Shutdown();
}

LRESULT CALLBACK Renderer::WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

bool Renderer::Initialize(
    HINSTANCE instance,
    int showCommand,
    int width,
    int height,
    const wchar_t* title,
    std::string& error)
{
    m_width = width;
    m_height = height;

    const wchar_t CLASS_NAME[] = L"ArcDemoWindowClassV2";

    WNDCLASS wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);

    if (!RegisterClass(&wc))
    {
        error = "Windows window class kon niet geregistreerd worden.";
        return false;
    }

    RECT windowRect{0, 0, width, height};

    AdjustWindowRect(
        &windowRect,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        FALSE);

    m_window = CreateWindowEx(
        0,
        CLASS_NAME,
        title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!m_window)
    {
        error = "Windows game window kon niet gemaakt worden.";
        return false;
    }

    ShowWindow(m_window, showCommand);
    UpdateWindow(m_window);

    DXGI_SWAP_CHAIN_DESC swapDesc{};
    swapDesc.BufferCount = 2;
    swapDesc.BufferDesc.Width = width;
    swapDesc.BufferDesc.Height = height;
    swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.OutputWindow = m_window;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &swapDesc,
        &m_swapChain,
        &m_device,
        nullptr,
        &m_context);

    if (FAILED(hr))
    {
        error = "DirectX 11 kon niet gestart worden.";
        return false;
    }

    if (!CreateBackBuffer(error))
        return false;

    if (!CreateWorldPipeline(error))
        return false;

    if (!CreateCubeMesh(error))
        return false;

    return true;
}

bool Renderer::CreateBackBuffer(std::string& error)
{
    ID3D11Texture2D* backBuffer = nullptr;

    HRESULT hr = m_swapChain->GetBuffer(
        0,
        __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(&backBuffer));

    if (FAILED(hr))
    {
        error = "DirectX back buffer kon niet opgehaald worden.";
        return false;
    }

    hr = m_device->CreateRenderTargetView(
        backBuffer,
        nullptr,
        &m_renderTarget);

    SafeRelease(backBuffer);

    if (FAILED(hr))
    {
        error = "Render target kon niet gemaakt worden.";
        return false;
    }

    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = m_width;
    depthDesc.Height = m_height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* depthTexture = nullptr;

    hr = m_device->CreateTexture2D(
        &depthDesc,
        nullptr,
        &depthTexture);

    if (FAILED(hr))
    {
        error = "Depth buffer texture kon niet gemaakt worden.";
        return false;
    }

    hr = m_device->CreateDepthStencilView(
        depthTexture,
        nullptr,
        &m_depthView);

    SafeRelease(depthTexture);

    if (FAILED(hr))
    {
        error = "Depth stencil view kon niet gemaakt worden.";
        return false;
    }

    m_context->OMSetRenderTargets(
        1,
        &m_renderTarget,
        m_depthView);

    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    m_context->RSSetViewports(1, &viewport);

    return true;
}

bool Renderer::CreateWorldPipeline(std::string& error)
{
    const char* vertexShaderSource = R"(
        cbuffer WorldConstantBuffer : register(b0)
        {
            matrix world;
            matrix worldViewProjection;
            float4 tint;
            float4 lightDirection;
            float4 fogColor;
            float4 fogParameters;
            float4 cameraPosition;
        };

        struct VSInput
        {
            float3 position : POSITION;
            float3 normal   : NORMAL;
            float3 color    : COLOR;
        };

        struct VSOutput
        {
            float4 position      : SV_POSITION;
            float3 worldPosition : TEXCOORD0;
            float3 normal        : NORMAL;
            float3 color         : COLOR;
        };

        VSOutput main(VSInput input)
        {
            VSOutput output;

            float4 localPosition = float4(input.position, 1.0f);

            output.position = mul(localPosition, worldViewProjection);
            output.worldPosition = mul(localPosition, world).xyz;
            output.normal = normalize(mul(float4(input.normal, 0.0f), world).xyz);
            output.color = input.color * tint.rgb;

            return output;
        }
    )";

    const char* pixelShaderSource = R"(
        cbuffer WorldConstantBuffer : register(b0)
        {
            matrix world;
            matrix worldViewProjection;
            float4 tint;
            float4 lightDirection;
            float4 fogColor;
            float4 fogParameters;
            float4 cameraPosition;
        };

        struct PSInput
        {
            float4 position      : SV_POSITION;
            float3 worldPosition : TEXCOORD0;
            float3 normal        : NORMAL;
            float3 color         : COLOR;
        };

        float4 main(PSInput input) : SV_TARGET
        {
            float3 N = normalize(input.normal);
            float3 L = normalize(-lightDirection.xyz);

            float diffuse = max(dot(N, L), 0.0f);
            float lightAmount = 0.34f + diffuse * 0.76f;

            float3 litColor = input.color * lightAmount;

            float distanceToCamera = distance(
                input.worldPosition,
                cameraPosition.xyz);

            float fogStart = fogParameters.x;
            float fogEnd = max(fogParameters.y, fogStart + 0.001f);

            float fogAmount = saturate(
                (distanceToCamera - fogStart) /
                (fogEnd - fogStart));

            float3 finalColor = lerp(
                litColor,
                fogColor.rgb,
                fogAmount);

            return float4(finalColor, 1.0f);
        }
    )";

    ID3DBlob* vertexBlob = nullptr;
    ID3DBlob* pixelBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(
        vertexShaderSource,
        std::strlen(vertexShaderSource),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "vs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        &vertexBlob,
        &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
            error.assign(
                static_cast<const char*>(errorBlob->GetBufferPointer()),
                errorBlob->GetBufferSize());
        else
            error = "World vertex shader compile error.";

        SafeRelease(errorBlob);
        SafeRelease(vertexBlob);
        return false;
    }

    SafeRelease(errorBlob);

    hr = D3DCompile(
        pixelShaderSource,
        std::strlen(pixelShaderSource),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        &pixelBlob,
        &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
            error.assign(
                static_cast<const char*>(errorBlob->GetBufferPointer()),
                errorBlob->GetBufferSize());
        else
            error = "World pixel shader compile error.";

        SafeRelease(errorBlob);
        SafeRelease(vertexBlob);
        SafeRelease(pixelBlob);
        return false;
    }

    SafeRelease(errorBlob);

    hr = m_device->CreateVertexShader(
        vertexBlob->GetBufferPointer(),
        vertexBlob->GetBufferSize(),
        nullptr,
        &m_worldVertexShader);

    if (FAILED(hr))
    {
        error = "World vertex shader kon niet aangemaakt worden.";
        SafeRelease(vertexBlob);
        SafeRelease(pixelBlob);
        return false;
    }

    hr = m_device->CreatePixelShader(
        pixelBlob->GetBufferPointer(),
        pixelBlob->GetBufferSize(),
        nullptr,
        &m_worldPixelShader);

    if (FAILED(hr))
    {
        error = "World pixel shader kon niet aangemaakt worden.";
        SafeRelease(vertexBlob);
        SafeRelease(pixelBlob);
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC inputElements[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    hr = m_device->CreateInputLayout(
        inputElements,
        3,
        vertexBlob->GetBufferPointer(),
        vertexBlob->GetBufferSize(),
        &m_worldInputLayout);

    SafeRelease(vertexBlob);
    SafeRelease(pixelBlob);

    if (FAILED(hr))
    {
        error = "World input layout kon niet aangemaakt worden.";
        return false;
    }

    D3D11_BUFFER_DESC constantDesc{};
    constantDesc.ByteWidth = sizeof(WorldConstantBuffer);
    constantDesc.Usage = D3D11_USAGE_DEFAULT;
    constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = m_device->CreateBuffer(
        &constantDesc,
        nullptr,
        &m_worldConstantBuffer);

    if (FAILED(hr))
    {
        error = "World constant buffer kon niet aangemaakt worden.";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterDesc{};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.DepthClipEnable = TRUE;

    hr = m_device->CreateRasterizerState(
        &rasterDesc,
        &m_rasterState);

    if (FAILED(hr))
    {
        error = "Rasterizer state kon niet aangemaakt worden.";
        return false;
    }

    RestoreWorldPipeline();
    return true;
}

bool Renderer::CreateCubeMesh(std::string& error)
{
    const WorldVertex vertices[] =
    {
        // Front (-Z)
        {{-1,-1,-1},{0,0,-1},{1,1,1}},
        {{-1, 1,-1},{0,0,-1},{1,1,1}},
        {{ 1, 1,-1},{0,0,-1},{1,1,1}},
        {{ 1,-1,-1},{0,0,-1},{1,1,1}},

        // Back (+Z)
        {{-1,-1, 1},{0,0, 1},{1,1,1}},
        {{ 1,-1, 1},{0,0, 1},{1,1,1}},
        {{ 1, 1, 1},{0,0, 1},{1,1,1}},
        {{-1, 1, 1},{0,0, 1},{1,1,1}},

        // Left (-X)
        {{-1,-1, 1},{-1,0,0},{1,1,1}},
        {{-1, 1, 1},{-1,0,0},{1,1,1}},
        {{-1, 1,-1},{-1,0,0},{1,1,1}},
        {{-1,-1,-1},{-1,0,0},{1,1,1}},

        // Right (+X)
        {{ 1,-1,-1},{1,0,0},{1,1,1}},
        {{ 1, 1,-1},{1,0,0},{1,1,1}},
        {{ 1, 1, 1},{1,0,0},{1,1,1}},
        {{ 1,-1, 1},{1,0,0},{1,1,1}},

        // Top (+Y)
        {{-1, 1,-1},{0,1,0},{1,1,1}},
        {{-1, 1, 1},{0,1,0},{1,1,1}},
        {{ 1, 1, 1},{0,1,0},{1,1,1}},
        {{ 1, 1,-1},{0,1,0},{1,1,1}},

        // Bottom (-Y)
        {{-1,-1, 1},{0,-1,0},{1,1,1}},
        {{-1,-1,-1},{0,-1,0},{1,1,1}},
        {{ 1,-1,-1},{0,-1,0},{1,1,1}},
        {{ 1,-1, 1},{0,-1,0},{1,1,1}}
    };

    const uint32_t indices[] =
    {
         0, 1, 2,  0, 2, 3,
         4, 5, 6,  4, 6, 7,
         8, 9,10,  8,10,11,
        12,13,14, 12,14,15,
        16,17,18, 16,18,19,
        20,21,22, 20,22,23
    };

    return CreateStaticMesh(
        vertices,
        static_cast<UINT>(std::size(vertices)),
        indices,
        static_cast<UINT>(std::size(indices)),
        m_cubeMesh,
        error);
}

bool Renderer::CreateStaticMesh(
    const WorldVertex* vertices,
    UINT vertexCount,
    const uint32_t* indices,
    UINT indexCount,
    StaticMesh& mesh,
    std::string& error)
{
    ReleaseMesh(mesh);

    if (!vertices || !indices || vertexCount == 0 || indexCount == 0)
    {
        error = "Ongeldige mesh data.";
        return false;
    }

    D3D11_BUFFER_DESC vertexDesc{};
    vertexDesc.ByteWidth = sizeof(WorldVertex) * vertexCount;
    vertexDesc.Usage = D3D11_USAGE_DEFAULT;
    vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vertexData{};
    vertexData.pSysMem = vertices;

    HRESULT hr = m_device->CreateBuffer(
        &vertexDesc,
        &vertexData,
        &mesh.vertexBuffer);

    if (FAILED(hr))
    {
        error = "Vertex buffer kon niet aangemaakt worden.";
        return false;
    }

    D3D11_BUFFER_DESC indexDesc{};
    indexDesc.ByteWidth = sizeof(uint32_t) * indexCount;
    indexDesc.Usage = D3D11_USAGE_DEFAULT;
    indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA indexData{};
    indexData.pSysMem = indices;

    hr = m_device->CreateBuffer(
        &indexDesc,
        &indexData,
        &mesh.indexBuffer);

    if (FAILED(hr))
    {
        error = "Index buffer kon niet aangemaakt worden.";
        ReleaseMesh(mesh);
        return false;
    }

    mesh.indexCount = indexCount;
    return true;
}

void Renderer::ReleaseMesh(StaticMesh& mesh)
{
    SafeRelease(mesh.vertexBuffer);
    SafeRelease(mesh.indexBuffer);
    mesh.indexCount = 0;
}

void Renderer::RestoreWorldPipeline()
{
    if (!m_context)
        return;

    m_context->IASetInputLayout(m_worldInputLayout);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_worldVertexShader, nullptr, 0);
    m_context->PSSetShader(m_worldPixelShader, nullptr, 0);
    m_context->VSSetConstantBuffers(0, 1, &m_worldConstantBuffer);
    m_context->PSSetConstantBuffers(0, 1, &m_worldConstantBuffer);
    m_context->RSSetState(m_rasterState);
}

void Renderer::BeginFrame(const XMFLOAT4& skyColor)
{
    const float color[4] =
    {
        skyColor.x,
        skyColor.y,
        skyColor.z,
        skyColor.w
    };

    m_context->OMSetRenderTargets(1, &m_renderTarget, m_depthView);
    m_context->ClearRenderTargetView(m_renderTarget, color);
    m_context->ClearDepthStencilView(
        m_depthView,
        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
        1.0f,
        0);

    RestoreWorldPipeline();
}

void Renderer::EndFrame()
{
    m_swapChain->Present(1, 0);
}

void Renderer::ClearDepthOnly()
{
    m_context->ClearDepthStencilView(
        m_depthView,
        D3D11_CLEAR_DEPTH,
        1.0f,
        0);
}

void Renderer::DrawMesh(
    const StaticMesh& mesh,
    const XMMATRIX& world,
    const XMMATRIX& view,
    const XMMATRIX& projection,
    const XMFLOAT4& tint,
    const XMFLOAT3& cameraPosition,
    bool fogEnabled)
{
    if (!mesh.vertexBuffer || !mesh.indexBuffer || mesh.indexCount == 0)
        return;

    RestoreWorldPipeline();

    UINT stride = sizeof(WorldVertex);
    UINT offset = 0;

    m_context->IASetVertexBuffers(
        0,
        1,
        &mesh.vertexBuffer,
        &stride,
        &offset);

    m_context->IASetIndexBuffer(
        mesh.indexBuffer,
        DXGI_FORMAT_R32_UINT,
        0);

    WorldConstantBuffer cb{};
    cb.world = XMMatrixTranspose(world);
    cb.worldViewProjection = XMMatrixTranspose(world * view * projection);
    cb.tint = tint;

    cb.lightDirection = {-0.35f, -0.88f, -0.30f, 0.0f};
    cb.fogColor = {0.62f, 0.77f, 0.80f, 1.0f};

    if (fogEnabled)
        cb.fogParameters = {70.0f, 190.0f, 0.0f, 0.0f};
    else
        cb.fogParameters = {10000.0f, 20000.0f, 0.0f, 0.0f};

    cb.cameraPosition = {
        cameraPosition.x,
        cameraPosition.y,
        cameraPosition.z,
        1.0f};

    m_context->UpdateSubresource(
        m_worldConstantBuffer,
        0,
        nullptr,
        &cb,
        0,
        0);

    m_context->DrawIndexed(mesh.indexCount, 0, 0);
}

void Renderer::DrawCube(
    const XMMATRIX& world,
    const XMMATRIX& view,
    const XMMATRIX& projection,
    const XMFLOAT4& tint,
    const XMFLOAT3& cameraPosition,
    bool fogEnabled)
{
    DrawMesh(
        m_cubeMesh,
        world,
        view,
        projection,
        tint,
        cameraPosition,
        fogEnabled);
}

void Renderer::Shutdown()
{
    if (m_context)
        m_context->ClearState();

    ReleaseMesh(m_cubeMesh);

    SafeRelease(m_worldConstantBuffer);
    SafeRelease(m_worldInputLayout);
    SafeRelease(m_worldPixelShader);
    SafeRelease(m_worldVertexShader);
    SafeRelease(m_rasterState);

    SafeRelease(m_depthView);
    SafeRelease(m_renderTarget);

    SafeRelease(m_swapChain);
    SafeRelease(m_context);
    SafeRelease(m_device);

    if (m_window)
    {
        DestroyWindow(m_window);
        m_window = nullptr;
    }
}
