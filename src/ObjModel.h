#pragma once

#include <d3d11.h>
#include <DirectXMath.h>

#include <string>
#include <cstdint>

class ObjModel
{
public:
    ObjModel();
    ~ObjModel();

    ObjModel(const ObjModel&) = delete;
    ObjModel& operator=(const ObjModel&) = delete;

    bool Load(
        ID3D11Device* device,
        const std::string& objFilename,
        const std::string& diffuseFilename,
        const std::string& normalFilename,
        const std::string& specularFilename,
        std::string& error
    );

    void Render(
        ID3D11DeviceContext* context,
        const DirectX::XMMATRIX& world,
        const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& projection,
        const DirectX::XMFLOAT3& cameraPosition
    );

    DirectX::XMMATRIX MakeNormalizedTransform(
        float targetSize
    ) const;

    bool IsLoaded() const;

private:
    struct Vertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 normal;
        DirectX::XMFLOAT2 uv;

        // xyz = tangent
        // w   = handedness
        DirectX::XMFLOAT4 tangent;
    };

    struct ConstantBuffer
    {
        DirectX::XMMATRIX world;
        DirectX::XMMATRIX worldViewProjection;

        DirectX::XMFLOAT4 cameraPosition;
        DirectX::XMFLOAT4 lightDirection;

        // x = diffuse texture aanwezig
        // y = normal map aanwezig
        // z = specular map aanwezig
        // w = specular power
        DirectX::XMFLOAT4 material;
    };

private:
    bool CreatePipeline(
        ID3D11Device* device,
        std::string& error
    );

    bool LoadTexture(
        ID3D11Device* device,
        const std::string& filename,
        bool srgb,
        ID3D11ShaderResourceView** output,
        std::string& error
    );

    void ReleaseResources();

private:
    ID3D11Buffer* m_vertexBuffer = nullptr;
    ID3D11Buffer* m_indexBuffer = nullptr;

    UINT m_indexCount = 0;

    ID3D11ShaderResourceView* m_diffuseTexture = nullptr;
    ID3D11ShaderResourceView* m_normalTexture = nullptr;
    ID3D11ShaderResourceView* m_specularTexture = nullptr;

    ID3D11VertexShader* m_vertexShader = nullptr;
    ID3D11PixelShader* m_pixelShader = nullptr;

    ID3D11InputLayout* m_inputLayout = nullptr;

    ID3D11Buffer* m_constantBuffer = nullptr;

    ID3D11SamplerState* m_sampler = nullptr;

    DirectX::XMFLOAT3 m_boundsMin{};
    DirectX::XMFLOAT3 m_boundsMax{};

    bool m_hasBounds = false;
    bool m_loaded = false;
};