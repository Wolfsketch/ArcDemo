#pragma once

#include <d3d11.h>
#include <DirectXMath.h>

#include <string>
#include <vector>

class GltfModel
{
public:
    GltfModel() = default;
    ~GltfModel();

    GltfModel(const GltfModel&) = delete;
    GltfModel& operator=(const GltfModel&) = delete;

    bool Load(
        ID3D11Device* device,
        const std::string& filename,
        std::string& error);

    void Render(
        ID3D11DeviceContext* context,
        const DirectX::XMMATRIX& world,
        const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& projection,
        const DirectX::XMFLOAT3& cameraPosition);

    DirectX::XMMATRIX MakeNormalizedTransform(float targetSize) const;

    bool IsLoaded() const { return m_loaded; }

private:
    struct Vertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 normal;
        DirectX::XMFLOAT2 uv;
    };

    struct Primitive
    {
        ID3D11Buffer* vertexBuffer = nullptr;
        ID3D11Buffer* indexBuffer = nullptr;
        UINT indexCount = 0;

        int baseColorImage = -1;
        int metallicRoughnessImage = -1;

        DirectX::XMFLOAT4 baseColorFactor{1,1,1,1};
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;

        DirectX::XMFLOAT4X4 nodeTransform{};
    };

    struct ModelConstantBuffer
    {
        DirectX::XMMATRIX world;
        DirectX::XMMATRIX worldViewProjection;
        DirectX::XMFLOAT4 baseColorFactor;
        DirectX::XMFLOAT4 materialParameters;
        DirectX::XMFLOAT4 lightDirection;
        DirectX::XMFLOAT4 cameraPosition;
    };

    bool CreatePipeline(ID3D11Device* device, std::string& error);
    void ReleaseResources();

private:
    std::vector<Primitive> m_primitives;
    std::vector<ID3D11ShaderResourceView*> m_images;

    ID3D11ShaderResourceView* m_whiteTexture = nullptr;

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
