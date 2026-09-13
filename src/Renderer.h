#pragma once

#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <DirectXMath.h>

#include <cstdint>
#include <string>

struct WorldVertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT3 color;
};

struct StaticMesh
{
    ID3D11Buffer* vertexBuffer = nullptr;
    ID3D11Buffer* indexBuffer = nullptr;
    UINT indexCount = 0;
};

class Renderer
{
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool Initialize(
        HINSTANCE instance,
        int showCommand,
        int width,
        int height,
        const wchar_t* title,
        std::string& error);

    void Shutdown();

    void BeginFrame(const DirectX::XMFLOAT4& skyColor);
    void EndFrame();

    bool CreateStaticMesh(
        const WorldVertex* vertices,
        UINT vertexCount,
        const uint32_t* indices,
        UINT indexCount,
        StaticMesh& mesh,
        std::string& error);

    static void ReleaseMesh(StaticMesh& mesh);

    void DrawMesh(
        const StaticMesh& mesh,
        const DirectX::XMMATRIX& world,
        const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& projection,
        const DirectX::XMFLOAT4& tint,
        const DirectX::XMFLOAT3& cameraPosition,
        bool fogEnabled = true);

    void DrawCube(
        const DirectX::XMMATRIX& world,
        const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& projection,
        const DirectX::XMFLOAT4& tint,
        const DirectX::XMFLOAT3& cameraPosition,
        bool fogEnabled = true);

    void RestoreWorldPipeline();
    void ClearDepthOnly();

    HWND Window() const { return m_window; }
    ID3D11Device* Device() const { return m_device; }
    ID3D11DeviceContext* Context() const { return m_context; }
    ID3D11DepthStencilView* DepthView() const { return m_depthView; }

    int Width() const { return m_width; }
    int Height() const { return m_height; }

private:
    struct WorldConstantBuffer
    {
        DirectX::XMMATRIX world;
        DirectX::XMMATRIX worldViewProjection;
        DirectX::XMFLOAT4 tint;
        DirectX::XMFLOAT4 lightDirection;
        DirectX::XMFLOAT4 fogColor;
        DirectX::XMFLOAT4 fogParameters;
        DirectX::XMFLOAT4 cameraPosition;
    };

    static LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    bool CreateBackBuffer(std::string& error);
    bool CreateWorldPipeline(std::string& error);
    bool CreateCubeMesh(std::string& error);

private:
    HWND m_window = nullptr;
    int m_width = 0;
    int m_height = 0;

    IDXGISwapChain* m_swapChain = nullptr;
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    ID3D11RenderTargetView* m_renderTarget = nullptr;
    ID3D11DepthStencilView* m_depthView = nullptr;

    ID3D11RasterizerState* m_rasterState = nullptr;
    ID3D11VertexShader* m_worldVertexShader = nullptr;
    ID3D11PixelShader* m_worldPixelShader = nullptr;
    ID3D11InputLayout* m_worldInputLayout = nullptr;
    ID3D11Buffer* m_worldConstantBuffer = nullptr;

    StaticMesh m_cubeMesh;
};
