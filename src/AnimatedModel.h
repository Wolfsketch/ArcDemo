#pragma once

#include <d3d11.h>
#include <DirectXMath.h>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class AnimatedGltfModel
{
public:
    enum class Motion
    {
        Idle,
        Walk,
        Jump
    };

    AnimatedGltfModel() = default;
    ~AnimatedGltfModel();

    AnimatedGltfModel(const AnimatedGltfModel&) = delete;
    AnimatedGltfModel& operator=(const AnimatedGltfModel&) = delete;

    bool Load(
        ID3D11Device* device,
        const std::string& filename,
        std::string& error);

    void Update(
        ID3D11DeviceContext* context,
        float animationTime,
        Motion motion,
        float motionTime);

    void Render(
        ID3D11DeviceContext* context,
        const DirectX::XMMATRIX& world,
        const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& projection,
        const DirectX::XMFLOAT3& cameraPosition);

    DirectX::XMMATRIX MakeGroundedTransform(float targetHeight) const;

    bool IsLoaded() const { return m_loaded; }

private:
    struct GpuVertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 normal;
        DirectX::XMFLOAT2 uv;
    };

    struct SourceVertex
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 normal{0, 1, 0};
        DirectX::XMFLOAT2 uv{};
        std::array<uint16_t, 4> joints{0,0,0,0};
        DirectX::XMFLOAT4 weights{1,0,0,0};
        bool skinned = false;
    };

    struct Node
    {
        std::string name;
        int parent = -1;
        std::vector<int> children;

        DirectX::XMFLOAT4X4 restLocal{};
        DirectX::XMFLOAT4X4 global{};
    };

    struct Skin
    {
        std::vector<int> joints;
        std::vector<DirectX::XMFLOAT4X4> inverseBind;
        std::vector<DirectX::XMFLOAT4X4> jointMatrices;
    };

    struct Primitive
    {
        ID3D11Buffer* vertexBuffer = nullptr;
        ID3D11Buffer* indexBuffer = nullptr;
        UINT indexCount = 0;

        std::vector<SourceVertex> sourceVertices;
        std::vector<GpuVertex> skinnedVertices;

        int nodeIndex = -1;
        int skinIndex = -1;

        int baseColorImage = -1;
        int metallicRoughnessImage = -1;

        DirectX::XMFLOAT4 baseColorFactor{1,1,1,1};
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        float alphaCutoff = 0.5f;
        bool alphaTest = false;
        std::string debugName;
    };

    struct ConstantBuffer
    {
        DirectX::XMMATRIX world;
        DirectX::XMMATRIX worldViewProjection;
        DirectX::XMFLOAT4 baseColorFactor;
        DirectX::XMFLOAT4 materialParameters;
        DirectX::XMFLOAT4 lightDirection;
        DirectX::XMFLOAT4 cameraPosition;
        DirectX::XMFLOAT4 alphaParameters;
    };

    bool CreatePipeline(ID3D11Device* device, std::string& error);
    void ReleaseResources();
    void EvaluatePose(float animationTime, Motion motion, float motionTime);
    void UpdateSkinMatrices();
    void UpdateVertexBuffers(ID3D11DeviceContext* context);

private:
    std::vector<Node> m_nodes;
    std::vector<int> m_rootNodes;
    std::unordered_map<std::string, int> m_nodeByName;
    std::vector<Skin> m_skins;
    std::vector<Primitive> m_primitives;
    std::vector<ID3D11ShaderResourceView*> m_images;

    ID3D11ShaderResourceView* m_whiteTexture = nullptr;
    ID3D11VertexShader* m_vertexShader = nullptr;
    ID3D11PixelShader* m_pixelShader = nullptr;
    ID3D11InputLayout* m_inputLayout = nullptr;
    ID3D11Buffer* m_constantBuffer = nullptr;
    ID3D11SamplerState* m_sampler = nullptr;
    ID3D11RasterizerState* m_rasterState = nullptr;

    DirectX::XMFLOAT3 m_boundsMin{};
    DirectX::XMFLOAT3 m_boundsMax{};
    DirectX::XMFLOAT3 m_normalizationOrigin{};
    float m_normalizationScale = 1.0f;
    bool m_normalizationReady = false;
    bool m_hasBounds = false;
    bool m_loaded = false;
    bool m_debugDumped = false;
};
