#include "AnimatedModel.h"

#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <fstream>
#include <limits>

using namespace DirectX;

namespace
{
    template<typename T>
    void SafeRelease(T*& value)
    {
        if (value)
        {
            value->Release();
            value = nullptr;
        }
    }

    const unsigned char* GetAccessorData(
        const tinygltf::Model& model,
        const tinygltf::Accessor& accessor,
        size_t& stride)
    {
        if (accessor.bufferView < 0)
            return nullptr;

        const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
        const tinygltf::Buffer& buffer = model.buffers[view.buffer];

        int calculatedStride = accessor.ByteStride(view);

        if (calculatedStride <= 0)
        {
            int componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
            int componentCount = tinygltf::GetNumComponentsInType(accessor.type);
            calculatedStride = componentSize * componentCount;
        }

        stride = static_cast<size_t>(calculatedStride);
        const size_t offset = view.byteOffset + accessor.byteOffset;

        if (offset >= buffer.data.size())
            return nullptr;

        return buffer.data.data() + offset;
    }

    XMMATRIX GetNodeTransform(const tinygltf::Node& node)
    {
        if (node.matrix.size() == 16)
        {
            return XMMATRIX(
                static_cast<float>(node.matrix[0]),
                static_cast<float>(node.matrix[1]),
                static_cast<float>(node.matrix[2]),
                static_cast<float>(node.matrix[3]),
                static_cast<float>(node.matrix[4]),
                static_cast<float>(node.matrix[5]),
                static_cast<float>(node.matrix[6]),
                static_cast<float>(node.matrix[7]),
                static_cast<float>(node.matrix[8]),
                static_cast<float>(node.matrix[9]),
                static_cast<float>(node.matrix[10]),
                static_cast<float>(node.matrix[11]),
                static_cast<float>(node.matrix[12]),
                static_cast<float>(node.matrix[13]),
                static_cast<float>(node.matrix[14]),
                static_cast<float>(node.matrix[15]));
        }

        XMMATRIX scale = XMMatrixIdentity();
        XMMATRIX rotation = XMMatrixIdentity();
        XMMATRIX translation = XMMatrixIdentity();

        if (node.scale.size() == 3)
        {
            scale = XMMatrixScaling(
                static_cast<float>(node.scale[0]),
                static_cast<float>(node.scale[1]),
                static_cast<float>(node.scale[2]));
        }

        if (node.rotation.size() == 4)
        {
            XMVECTOR quaternion = XMVectorSet(
                static_cast<float>(node.rotation[0]),
                static_cast<float>(node.rotation[1]),
                static_cast<float>(node.rotation[2]),
                static_cast<float>(node.rotation[3]));

            rotation = XMMatrixRotationQuaternion(quaternion);
        }

        if (node.translation.size() == 3)
        {
            translation = XMMatrixTranslation(
                static_cast<float>(node.translation[0]),
                static_cast<float>(node.translation[1]),
                static_cast<float>(node.translation[2]));
        }

        return scale * rotation * translation;
    }

    int GetImageFromTexture(const tinygltf::Model& model, int textureIndex)
    {
        if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size()))
            return -1;

        const int imageIndex = model.textures[textureIndex].source;

        if (imageIndex < 0 || imageIndex >= static_cast<int>(model.images.size()))
            return -1;

        return imageIndex;
    }

    uint16_t ReadJointComponent(const unsigned char* data, int componentType, int component)
    {
        switch (componentType)
        {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                return static_cast<uint16_t>(reinterpret_cast<const uint8_t*>(data)[component]);

            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                return reinterpret_cast<const uint16_t*>(data)[component];

            default:
                return 0;
        }
    }

    float ReadWeightComponent(
        const unsigned char* data,
        int componentType,
        bool normalized,
        int component)
    {
        switch (componentType)
        {
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                return reinterpret_cast<const float*>(data)[component];

            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            {
                const float value = static_cast<float>(reinterpret_cast<const uint8_t*>(data)[component]);
                return normalized ? value / 255.0f : value;
            }

            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            {
                const float value = static_cast<float>(reinterpret_cast<const uint16_t*>(data)[component]);
                return normalized ? value / 65535.0f : value;
            }

            default:
                return 0.0f;
        }
    }
}

AnimatedGltfModel::~AnimatedGltfModel()
{
    ReleaseResources();
}

void AnimatedGltfModel::ReleaseResources()
{
    for (Primitive& primitive : m_primitives)
    {
        SafeRelease(primitive.vertexBuffer);
        SafeRelease(primitive.indexBuffer);
    }

    m_primitives.clear();
    m_nodes.clear();
    m_rootNodes.clear();
    m_nodeByName.clear();
    m_skins.clear();

    for (ID3D11ShaderResourceView*& image : m_images)
        SafeRelease(image);

    m_images.clear();

    SafeRelease(m_whiteTexture);
    SafeRelease(m_sampler);
    SafeRelease(m_rasterState);
    SafeRelease(m_constantBuffer);
    SafeRelease(m_inputLayout);
    SafeRelease(m_pixelShader);
    SafeRelease(m_vertexShader);

    m_loaded = false;
    m_hasBounds = false;
    m_debugDumped = false;
    m_normalizationReady = false;
    m_normalizationScale = 1.0f;
    m_normalizationOrigin = {0.0f, 0.0f, 0.0f};
}

bool AnimatedGltfModel::CreatePipeline(ID3D11Device* device, std::string& error)
{
    const char* vertexShaderSource = R"(
        cbuffer CharacterConstantBuffer : register(b0)
        {
            matrix world;
            matrix worldViewProjection;
            float4 baseColorFactor;
            float4 materialParameters;
            float4 lightDirection;
            float4 cameraPosition;
            float4 alphaParameters;
        };

        struct VSInput
        {
            float3 position : POSITION;
            float3 normal   : NORMAL;
            float2 uv       : TEXCOORD;
        };

        struct VSOutput
        {
            float4 position      : SV_POSITION;
            float3 worldPosition : TEXCOORD1;
            float3 normal        : NORMAL;
            float2 uv            : TEXCOORD0;
        };

        VSOutput main(VSInput input)
        {
            VSOutput output;
            output.position = mul(float4(input.position, 1.0f), worldViewProjection);
            output.worldPosition = mul(float4(input.position, 1.0f), world).xyz;
            output.normal = normalize(mul(float4(input.normal, 0.0f), world).xyz);
            output.uv = input.uv;
            return output;
        }
    )";

    const char* pixelShaderSource = R"(
        Texture2D BaseColorTexture : register(t0);
        Texture2D MetallicRoughnessTexture : register(t1);
        SamplerState TextureSampler : register(s0);

        cbuffer CharacterConstantBuffer : register(b0)
        {
            matrix world;
            matrix worldViewProjection;
            float4 baseColorFactor;
            float4 materialParameters;
            float4 lightDirection;
            float4 cameraPosition;
            float4 alphaParameters;
        };

        struct PSInput
        {
            float4 position      : SV_POSITION;
            float3 worldPosition : TEXCOORD1;
            float3 normal        : NORMAL;
            float2 uv            : TEXCOORD0;
        };

        static const float PI = 3.14159265359f;

        float DistributionGGX(float3 N, float3 H, float roughness)
        {
            float a = roughness * roughness;
            float a2 = a * a;
            float NdotH = max(dot(N, H), 0.0f);
            float NdotH2 = NdotH * NdotH;
            float numerator = a2;
            float denominator = NdotH2 * (a2 - 1.0f) + 1.0f;
            denominator = PI * denominator * denominator;
            return numerator / max(denominator, 0.0001f);
        }

        float GeometrySchlickGGX(float NdotV, float roughness)
        {
            float r = roughness + 1.0f;
            float k = (r * r) / 8.0f;
            return NdotV / (NdotV * (1.0f - k) + k);
        }

        float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
        {
            float NdotV = max(dot(N, V), 0.0f);
            float NdotL = max(dot(N, L), 0.0f);
            return GeometrySchlickGGX(NdotV, roughness) *
                   GeometrySchlickGGX(NdotL, roughness);
        }

        float3 FresnelSchlick(float cosTheta, float3 F0)
        {
            return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
        }

        float4 main(PSInput input) : SV_TARGET
        {
            float4 sampledBase = BaseColorTexture.Sample(TextureSampler, input.uv);
            float hasBaseTexture = materialParameters.z;
            float hasMRTexture = materialParameters.w;

            float alpha = baseColorFactor.a;
            if (hasBaseTexture > 0.5f)
                alpha *= sampledBase.a;

            if (alphaParameters.y > 0.5f)
                clip(alpha - alphaParameters.x);

            float3 albedo = max(baseColorFactor.rgb, float3(0.0001f,0.0001f,0.0001f));

            if (hasBaseTexture > 0.5f)
            {
                float3 textureLinear = pow(
                    max(sampledBase.rgb, float3(0.0001f,0.0001f,0.0001f)),
                    2.2f);
                albedo *= textureLinear;
            }

            float metallic = saturate(materialParameters.x);
            float roughness = clamp(materialParameters.y, 0.05f, 1.0f);

            if (hasMRTexture > 0.5f)
            {
                float4 mr = MetallicRoughnessTexture.Sample(TextureSampler, input.uv);
                roughness *= mr.g;
                metallic *= mr.b;
                roughness = clamp(roughness, 0.05f, 1.0f);
                metallic = saturate(metallic);
            }

            float3 N = normalize(input.normal);
            float3 V = normalize(cameraPosition.xyz - input.worldPosition);
            float3 L = normalize(-lightDirection.xyz);
            float3 H = normalize(V + L);

            float NdotL = max(dot(N, L), 0.0f);
            float NdotV = max(dot(N, V), 0.0f);
            float3 F0 = lerp(float3(0.04f,0.04f,0.04f), albedo, metallic);
            float NDF = DistributionGGX(N, H, roughness);
            float G = GeometrySmith(N, V, L, roughness);
            float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);
            float3 numerator = NDF * G * F;
            float denominator = 4.0f * NdotV * NdotL + 0.0001f;
            float3 specular = numerator / denominator;
            float3 kS = F;
            float3 kD = (1.0f - kS) * (1.0f - metallic);
            float3 lightColor = float3(1.45f, 1.42f, 1.34f);
            float3 directLight = (kD * albedo / PI + specular) * lightColor * NdotL;
            float3 ambient = albedo * 0.095f;
            float3 color = ambient + directLight;
            color = color / (color + 1.0f);
            color = pow(max(color, 0.0f), 1.0f / 2.2f);
            return float4(color, alpha);
        }
    )";

    ID3DBlob* vertexBlob = nullptr;
    ID3DBlob* pixelBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(
        vertexShaderSource,
        std::strlen(vertexShaderSource),
        nullptr, nullptr, nullptr,
        "main", "vs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        &vertexBlob,
        &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
            error.assign(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
        else
            error = "Character vertex shader kon niet compileren.";

        SafeRelease(errorBlob);
        SafeRelease(vertexBlob);
        return false;
    }

    SafeRelease(errorBlob);

    hr = D3DCompile(
        pixelShaderSource,
        std::strlen(pixelShaderSource),
        nullptr, nullptr, nullptr,
        "main", "ps_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        &pixelBlob,
        &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
            error.assign(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
        else
            error = "Character pixel shader kon niet compileren.";

        SafeRelease(errorBlob);
        SafeRelease(vertexBlob);
        SafeRelease(pixelBlob);
        return false;
    }

    SafeRelease(errorBlob);

    hr = device->CreateVertexShader(
        vertexBlob->GetBufferPointer(),
        vertexBlob->GetBufferSize(),
        nullptr,
        &m_vertexShader);

    if (FAILED(hr))
    {
        error = "Character vertex shader kon niet aangemaakt worden.";
        SafeRelease(vertexBlob);
        SafeRelease(pixelBlob);
        return false;
    }

    hr = device->CreatePixelShader(
        pixelBlob->GetBufferPointer(),
        pixelBlob->GetBufferSize(),
        nullptr,
        &m_pixelShader);

    if (FAILED(hr))
    {
        error = "Character pixel shader kon niet aangemaakt worden.";
        SafeRelease(vertexBlob);
        SafeRelease(pixelBlob);
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC inputElements[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    hr = device->CreateInputLayout(
        inputElements,
        3,
        vertexBlob->GetBufferPointer(),
        vertexBlob->GetBufferSize(),
        &m_inputLayout);

    SafeRelease(vertexBlob);
    SafeRelease(pixelBlob);

    if (FAILED(hr))
    {
        error = "Character input layout kon niet aangemaakt worden.";
        return false;
    }

    D3D11_BUFFER_DESC constantDesc{};
    constantDesc.ByteWidth = sizeof(ConstantBuffer);
    constantDesc.Usage = D3D11_USAGE_DEFAULT;
    constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = device->CreateBuffer(&constantDesc, nullptr, &m_constantBuffer);
    if (FAILED(hr))
    {
        error = "Character constant buffer kon niet aangemaakt worden.";
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.MaxAnisotropy = 16;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = device->CreateSamplerState(&samplerDesc, &m_sampler);
    if (FAILED(hr))
    {
        error = "Character sampler kon niet aangemaakt worden.";
        return false;
    }

    D3D11_RASTERIZER_DESC rasterDesc{};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.DepthClipEnable = TRUE;

    hr = device->CreateRasterizerState(&rasterDesc, &m_rasterState);
    if (FAILED(hr))
    {
        error = "Character rasterizer state kon niet aangemaakt worden.";
        return false;
    }

    const uint32_t whitePixel = 0xFFFFFFFFu;
    D3D11_TEXTURE2D_DESC whiteDesc{};
    whiteDesc.Width = 1;
    whiteDesc.Height = 1;
    whiteDesc.MipLevels = 1;
    whiteDesc.ArraySize = 1;
    whiteDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    whiteDesc.SampleDesc.Count = 1;
    whiteDesc.Usage = D3D11_USAGE_IMMUTABLE;
    whiteDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA whiteData{};
    whiteData.pSysMem = &whitePixel;
    whiteData.SysMemPitch = sizeof(uint32_t);

    ID3D11Texture2D* whiteTexture = nullptr;
    hr = device->CreateTexture2D(&whiteDesc, &whiteData, &whiteTexture);

    if (SUCCEEDED(hr))
        device->CreateShaderResourceView(whiteTexture, nullptr, &m_whiteTexture);

    SafeRelease(whiteTexture);

    if (!m_whiteTexture)
    {
        error = "Character fallback texture kon niet gemaakt worden.";
        return false;
    }

    return true;
}

bool AnimatedGltfModel::Load(
    ID3D11Device* device,
    const std::string& filename,
    std::string& error)
{
    ReleaseResources();

    if (!device)
    {
        error = "Geen DirectX device beschikbaar voor character.";
        return false;
    }

    if (!CreatePipeline(device, error))
        return false;

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string warning;
    std::string loadingError;

    const bool success = loader.LoadBinaryFromFile(
        &model,
        &loadingError,
        &warning,
        filename);

    if (!warning.empty())
        OutputDebugStringA(warning.c_str());

    if (!success)
    {
        error = "Character GLB kon niet geladen worden:\n" + loadingError;
        return false;
    }

    // --------------------------------------------------------
    // Textures
    // --------------------------------------------------------
    m_images.resize(model.images.size(), nullptr);

    for (size_t i = 0; i < model.images.size(); ++i)
    {
        const tinygltf::Image& image = model.images[i];

        if (image.width <= 0 || image.height <= 0 || image.image.empty() || image.bits != 8)
            continue;

        const size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
        std::vector<unsigned char> rgba(pixelCount * 4, 255);

        for (size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            if (image.component == 4)
            {
                rgba[pixel * 4 + 0] = image.image[pixel * 4 + 0];
                rgba[pixel * 4 + 1] = image.image[pixel * 4 + 1];
                rgba[pixel * 4 + 2] = image.image[pixel * 4 + 2];
                rgba[pixel * 4 + 3] = image.image[pixel * 4 + 3];
            }
            else if (image.component == 3)
            {
                rgba[pixel * 4 + 0] = image.image[pixel * 3 + 0];
                rgba[pixel * 4 + 1] = image.image[pixel * 3 + 1];
                rgba[pixel * 4 + 2] = image.image[pixel * 3 + 2];
            }
            else if (image.component == 2)
            {
                const unsigned char value = image.image[pixel * 2 + 0];
                rgba[pixel * 4 + 0] = value;
                rgba[pixel * 4 + 1] = value;
                rgba[pixel * 4 + 2] = value;
                rgba[pixel * 4 + 3] = image.image[pixel * 2 + 1];
            }
            else if (image.component == 1)
            {
                const unsigned char value = image.image[pixel];
                rgba[pixel * 4 + 0] = value;
                rgba[pixel * 4 + 1] = value;
                rgba[pixel * 4 + 2] = value;
            }
        }

        D3D11_TEXTURE2D_DESC textureDesc{};
        textureDesc.Width = static_cast<UINT>(image.width);
        textureDesc.Height = static_cast<UINT>(image.height);
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA textureData{};
        textureData.pSysMem = rgba.data();
        textureData.SysMemPitch = static_cast<UINT>(image.width * 4);

        ID3D11Texture2D* texture = nullptr;
        HRESULT hr = device->CreateTexture2D(&textureDesc, &textureData, &texture);

        if (SUCCEEDED(hr))
            device->CreateShaderResourceView(texture, nullptr, &m_images[i]);

        SafeRelease(texture);
    }

    // --------------------------------------------------------
    // Node hierarchy / imported pose
    // --------------------------------------------------------
    m_nodes.resize(model.nodes.size());

    for (size_t i = 0; i < model.nodes.size(); ++i)
    {
        const tinygltf::Node& source = model.nodes[i];
        Node& node = m_nodes[i];

        node.name = source.name;
        node.children = source.children;

        const XMMATRIX local = GetNodeTransform(source);
        XMStoreFloat4x4(&node.restLocal, local);
        XMStoreFloat4x4(&node.global, XMMatrixIdentity());

        if (!node.name.empty())
            m_nodeByName[node.name] = static_cast<int>(i);
    }

    for (size_t i = 0; i < m_nodes.size(); ++i)
    {
        for (int child : m_nodes[i].children)
        {
            if (child >= 0 && child < static_cast<int>(m_nodes.size()))
                m_nodes[child].parent = static_cast<int>(i);
        }
    }

    for (size_t i = 0; i < m_nodes.size(); ++i)
    {
        if (m_nodes[i].parent < 0)
            m_rootNodes.push_back(static_cast<int>(i));
    }

    // --------------------------------------------------------
    // Skins / inverse bind matrices
    // --------------------------------------------------------
    m_skins.resize(model.skins.size());

    for (size_t skinIndex = 0; skinIndex < model.skins.size(); ++skinIndex)
    {
        const tinygltf::Skin& sourceSkin = model.skins[skinIndex];
        Skin& skin = m_skins[skinIndex];
        skin.joints = sourceSkin.joints;
        skin.inverseBind.resize(skin.joints.size());
        skin.jointMatrices.resize(skin.joints.size());

        for (size_t j = 0; j < skin.joints.size(); ++j)
        {
            XMStoreFloat4x4(&skin.inverseBind[j], XMMatrixIdentity());
            XMStoreFloat4x4(&skin.jointMatrices[j], XMMatrixIdentity());
        }

        if (sourceSkin.inverseBindMatrices >= 0)
        {
            const tinygltf::Accessor& accessor = model.accessors[sourceSkin.inverseBindMatrices];
            size_t stride = 0;
            const unsigned char* data = GetAccessorData(model, accessor, stride);

            if (data && accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && accessor.type == TINYGLTF_TYPE_MAT4)
            {
                const size_t count = (std::min)(accessor.count, skin.joints.size());

                for (size_t j = 0; j < count; ++j)
                {
                    const float* m = reinterpret_cast<const float*>(data + j * stride);
                    const XMMATRIX matrix(
                        m[0], m[1], m[2], m[3],
                        m[4], m[5], m[6], m[7],
                        m[8], m[9], m[10], m[11],
                        m[12], m[13], m[14], m[15]);
                    XMStoreFloat4x4(&skin.inverseBind[j], matrix);
                }
            }
        }
    }

    // --------------------------------------------------------
    // Recover the true neutral skin bind pose.
    // --------------------------------------------------------
    // This Sketchfab asset ships with a Crossarmed clip and its node defaults
    // are not a useful locomotion base. If we use those node transforms as
    // restLocal, the procedural walk only adds small deltas on top of crossed
    // arms and a bent leg. The inverse-bind matrices are the authoritative
    // skin bind pose: inverse(inverseBind) is the joint's global bind matrix.
    // Reconstruct local joint transforms from that pose so idle/walk/jump all
    // start from a neutral body. At neutral, inverseBind * jointGlobal becomes
    // identity, which also keeps the original skinned vertex bind shape intact.
    {
        std::vector<XMMATRIX> importedGlobals(m_nodes.size(), XMMatrixIdentity());

        std::function<void(int, const XMMATRIX&)> evaluateImported;
        evaluateImported = [&](int nodeIndex, const XMMATRIX& parentGlobal)
        {
            if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
                return;

            const XMMATRIX local = XMLoadFloat4x4(&m_nodes[nodeIndex].restLocal);
            const XMMATRIX global = local * parentGlobal;
            importedGlobals[nodeIndex] = global;

            for (int child : m_nodes[nodeIndex].children)
                evaluateImported(child, global);
        };

        for (int root : m_rootNodes)
            evaluateImported(root, XMMatrixIdentity());

        for (const Skin& skin : m_skins)
        {
            std::vector<XMMATRIX> bindGlobals(skin.joints.size(), XMMatrixIdentity());

            for (size_t j = 0; j < skin.joints.size(); ++j)
            {
                const XMMATRIX inverseBind = XMLoadFloat4x4(&skin.inverseBind[j]);
                XMVECTOR determinant{};
                bindGlobals[j] = XMMatrixInverse(&determinant, inverseBind);
            }

            for (size_t j = 0; j < skin.joints.size(); ++j)
            {
                const int nodeIndex = skin.joints[j];
                if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
                    continue;

                const int parentIndex = m_nodes[nodeIndex].parent;
                XMMATRIX parentGlobal = XMMatrixIdentity();

                if (parentIndex >= 0)
                {
                    bool parentIsJoint = false;

                    for (size_t parentSlot = 0; parentSlot < skin.joints.size(); ++parentSlot)
                    {
                        if (skin.joints[parentSlot] == parentIndex)
                        {
                            parentGlobal = bindGlobals[parentSlot];
                            parentIsJoint = true;
                            break;
                        }
                    }

                    if (!parentIsJoint && parentIndex < static_cast<int>(importedGlobals.size()))
                        parentGlobal = importedGlobals[parentIndex];
                }

                XMVECTOR parentDeterminant{};
                const XMMATRIX inverseParent = XMMatrixInverse(&parentDeterminant, parentGlobal);
                const XMMATRIX localBind = bindGlobals[j] * inverseParent;
                XMStoreFloat4x4(&m_nodes[nodeIndex].restLocal, localBind);
            }
        }
    }

    // --------------------------------------------------------
    // Mesh primitives
    // --------------------------------------------------------
    for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex)
    {
        const tinygltf::Node& node = model.nodes[nodeIndex];

        if (node.mesh < 0 || node.mesh >= static_cast<int>(model.meshes.size()))
            continue;

        const tinygltf::Mesh& mesh = model.meshes[node.mesh];

        for (const tinygltf::Primitive& sourcePrimitive : mesh.primitives)
        {
            if (sourcePrimitive.mode != TINYGLTF_MODE_TRIANGLES && sourcePrimitive.mode != -1)
                continue;

            const auto positionIt = sourcePrimitive.attributes.find("POSITION");
            if (positionIt == sourcePrimitive.attributes.end())
                continue;

            const tinygltf::Accessor& positionAccessor = model.accessors[positionIt->second];
            if (positionAccessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || positionAccessor.type != TINYGLTF_TYPE_VEC3)
                continue;

            size_t positionStride = 0;
            const unsigned char* positionData = GetAccessorData(model, positionAccessor, positionStride);
            if (!positionData)
                continue;

            const tinygltf::Accessor* normalAccessor = nullptr;
            const unsigned char* normalData = nullptr;
            size_t normalStride = 0;

            const auto normalIt = sourcePrimitive.attributes.find("NORMAL");
            if (normalIt != sourcePrimitive.attributes.end())
            {
                normalAccessor = &model.accessors[normalIt->second];
                normalData = GetAccessorData(model, *normalAccessor, normalStride);
            }

            const tinygltf::Accessor* uvAccessor = nullptr;
            const unsigned char* uvData = nullptr;
            size_t uvStride = 0;

            const auto uvIt = sourcePrimitive.attributes.find("TEXCOORD_0");
            if (uvIt != sourcePrimitive.attributes.end())
            {
                uvAccessor = &model.accessors[uvIt->second];
                uvData = GetAccessorData(model, *uvAccessor, uvStride);
            }

            const tinygltf::Accessor* jointsAccessor = nullptr;
            const unsigned char* jointsData = nullptr;
            size_t jointsStride = 0;

            const auto jointsIt = sourcePrimitive.attributes.find("JOINTS_0");
            if (jointsIt != sourcePrimitive.attributes.end())
            {
                jointsAccessor = &model.accessors[jointsIt->second];
                jointsData = GetAccessorData(model, *jointsAccessor, jointsStride);
            }

            const tinygltf::Accessor* weightsAccessor = nullptr;
            const unsigned char* weightsData = nullptr;
            size_t weightsStride = 0;

            const auto weightsIt = sourcePrimitive.attributes.find("WEIGHTS_0");
            if (weightsIt != sourcePrimitive.attributes.end())
            {
                weightsAccessor = &model.accessors[weightsIt->second];
                weightsData = GetAccessorData(model, *weightsAccessor, weightsStride);
            }

            Primitive primitive{};
            primitive.nodeIndex = static_cast<int>(nodeIndex);
            primitive.skinIndex = node.skin;
            primitive.debugName = node.name + "/" + mesh.name;
            primitive.sourceVertices.resize(positionAccessor.count);
            primitive.skinnedVertices.resize(positionAccessor.count);

            const bool canSkin =
                primitive.skinIndex >= 0 &&
                primitive.skinIndex < static_cast<int>(m_skins.size()) &&
                jointsAccessor && jointsData &&
                weightsAccessor && weightsData &&
                jointsAccessor->type == TINYGLTF_TYPE_VEC4 &&
                weightsAccessor->type == TINYGLTF_TYPE_VEC4;

            for (size_t vertexIndex = 0; vertexIndex < positionAccessor.count; ++vertexIndex)
            {
                SourceVertex& vertex = primitive.sourceVertices[vertexIndex];
                const float* position = reinterpret_cast<const float*>(positionData + vertexIndex * positionStride);
                vertex.position = {position[0], position[1], position[2]};

                if (normalData && normalAccessor && normalAccessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
                {
                    const float* normal = reinterpret_cast<const float*>(normalData + vertexIndex * normalStride);
                    vertex.normal = {normal[0], normal[1], normal[2]};
                }

                if (uvData && uvAccessor && uvAccessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
                {
                    const float* uv = reinterpret_cast<const float*>(uvData + vertexIndex * uvStride);
                    vertex.uv = {uv[0], uv[1]};
                }

                if (canSkin)
                {
                    const unsigned char* jointSource = jointsData + vertexIndex * jointsStride;
                    const unsigned char* weightSource = weightsData + vertexIndex * weightsStride;

                    for (int component = 0; component < 4; ++component)
                        vertex.joints[component] = ReadJointComponent(jointSource, jointsAccessor->componentType, component);

                    vertex.weights.x = ReadWeightComponent(weightSource, weightsAccessor->componentType, weightsAccessor->normalized, 0);
                    vertex.weights.y = ReadWeightComponent(weightSource, weightsAccessor->componentType, weightsAccessor->normalized, 1);
                    vertex.weights.z = ReadWeightComponent(weightSource, weightsAccessor->componentType, weightsAccessor->normalized, 2);
                    vertex.weights.w = ReadWeightComponent(weightSource, weightsAccessor->componentType, weightsAccessor->normalized, 3);
                    vertex.skinned = true;
                }

                primitive.skinnedVertices[vertexIndex] = {vertex.position, vertex.normal, vertex.uv};
            }

            std::vector<uint32_t> indices;

            if (sourcePrimitive.indices >= 0)
            {
                const tinygltf::Accessor& indexAccessor = model.accessors[sourcePrimitive.indices];
                size_t indexStride = 0;
                const unsigned char* indexData = GetAccessorData(model, indexAccessor, indexStride);
                if (!indexData)
                    continue;

                indices.resize(indexAccessor.count);

                for (size_t i = 0; i < indexAccessor.count; ++i)
                {
                    const unsigned char* source = indexData + i * indexStride;

                    switch (indexAccessor.componentType)
                    {
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                            indices[i] = *reinterpret_cast<const uint8_t*>(source);
                            break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                            indices[i] = *reinterpret_cast<const uint16_t*>(source);
                            break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                            indices[i] = *reinterpret_cast<const uint32_t*>(source);
                            break;
                        default:
                            indices[i] = 0;
                            break;
                    }
                }
            }
            else
            {
                indices.resize(primitive.sourceVertices.size());
                for (size_t i = 0; i < indices.size(); ++i)
                    indices[i] = static_cast<uint32_t>(i);
            }

            if (primitive.sourceVertices.empty() || indices.empty())
                continue;

            if (sourcePrimitive.material >= 0 && sourcePrimitive.material < static_cast<int>(model.materials.size()))
            {
                const tinygltf::Material& material = model.materials[sourcePrimitive.material];
                const auto& pbr = material.pbrMetallicRoughness;

                if (pbr.baseColorFactor.size() == 4)
                {
                    primitive.baseColorFactor =
                    {
                        static_cast<float>(pbr.baseColorFactor[0]),
                        static_cast<float>(pbr.baseColorFactor[1]),
                        static_cast<float>(pbr.baseColorFactor[2]),
                        static_cast<float>(pbr.baseColorFactor[3])
                    };
                }

                primitive.metallicFactor = static_cast<float>(pbr.metallicFactor);
                primitive.roughnessFactor = static_cast<float>(pbr.roughnessFactor);
                primitive.baseColorImage = GetImageFromTexture(model, pbr.baseColorTexture.index);
                primitive.metallicRoughnessImage = GetImageFromTexture(model, pbr.metallicRoughnessTexture.index);
                primitive.alphaCutoff = static_cast<float>(material.alphaCutoff);
                primitive.alphaTest = material.alphaMode == "MASK";
            }

            D3D11_BUFFER_DESC vertexDesc{};
            vertexDesc.ByteWidth = static_cast<UINT>(primitive.skinnedVertices.size() * sizeof(GpuVertex));
            vertexDesc.Usage = D3D11_USAGE_DYNAMIC;
            vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            vertexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            D3D11_SUBRESOURCE_DATA vertexInitial{};
            vertexInitial.pSysMem = primitive.skinnedVertices.data();

            HRESULT hr = device->CreateBuffer(&vertexDesc, &vertexInitial, &primitive.vertexBuffer);
            if (FAILED(hr))
                continue;

            D3D11_BUFFER_DESC indexDesc{};
            indexDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
            indexDesc.Usage = D3D11_USAGE_IMMUTABLE;
            indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

            D3D11_SUBRESOURCE_DATA indexInitial{};
            indexInitial.pSysMem = indices.data();

            hr = device->CreateBuffer(&indexDesc, &indexInitial, &primitive.indexBuffer);
            if (FAILED(hr))
            {
                SafeRelease(primitive.vertexBuffer);
                continue;
            }

            primitive.indexCount = static_cast<UINT>(indices.size());
            m_primitives.push_back(std::move(primitive));
        }
    }

    if (m_primitives.empty())
    {
        error = "Character GLB bevat geen bruikbare triangle meshes.";
        return false;
    }

    EvaluatePose(0.0f, Motion::Idle, 0.0f);
    UpdateSkinMatrices();

    ID3D11DeviceContext* immediate = nullptr;
    device->GetImmediateContext(&immediate);
    if (immediate)
    {
        UpdateVertexBuffers(immediate);
        immediate->Release();
    }

    m_loaded = true;
    return true;
}

void AnimatedGltfModel::EvaluatePose(
    float animationTime,
    Motion motion,
    float motionTime)
{
    struct Delta
    {
        float pitch = 0.0f;
        float yaw = 0.0f;
        float roll = 0.0f;
    };

    std::vector<Delta> deltas(m_nodes.size());

    auto addDelta = [&](const char* name, float pitch, float yaw = 0.0f, float roll = 0.0f)
    {
        const auto it = m_nodeByName.find(name);
        if (it == m_nodeByName.end())
            return;

        Delta& delta = deltas[it->second];
        delta.pitch += pitch;
        delta.yaw += yaw;
        delta.roll += roll;
    };

    // Small breathing motion in every state.
    const float breathing = std::sinf(animationTime * 1.55f) * 0.018f;
    addDelta("spine_02_04", breathing);
    addDelta("head_048", -breathing * 0.30f);

    if (motion == Motion::Walk)
    {
        const float cycle = std::sinf(animationTime * 7.2f);
        const float halfCycle = std::sinf(animationTime * 7.2f + XM_PIDIV2);

        addDelta("thigh_l_049", cycle * 0.46f);
        addDelta("thigh_r_055", -cycle * 0.46f);

        const float leftKnee = (std::max)(-cycle, 0.0f);
        const float rightKnee = (std::max)(cycle, 0.0f);

        addDelta("calf_l_050", -leftKnee * 0.68f);
        addDelta("calf_r_056", -rightKnee * 0.68f);

        addDelta("foot_l_052", leftKnee * 0.20f);
        addDelta("foot_r_058", rightKnee * 0.20f);

        addDelta("upperarm_l_07", -cycle * 0.22f);
        addDelta("upperarm_r_028", cycle * 0.22f);
        addDelta("lowerarm_l_08", -0.10f + halfCycle * 0.05f);
        addDelta("lowerarm_r_029", -0.10f - halfCycle * 0.05f);
        addDelta("spine_03_05", 0.0f, 0.0f, cycle * 0.025f);
    }
    else if (motion == Motion::Jump)
    {
        const float jumpT = (std::min)((std::max)(motionTime / 0.90f, 0.0f), 1.0f);
        const float tuck = std::sinf(jumpT * XM_PI);

        addDelta("thigh_l_049", tuck * 0.22f);
        addDelta("thigh_r_055", tuck * 0.22f);
        addDelta("calf_l_050", -tuck * 0.62f);
        addDelta("calf_r_056", -tuck * 0.62f);
        addDelta("foot_l_052", tuck * 0.18f);
        addDelta("foot_r_058", tuck * 0.18f);
        addDelta("upperarm_l_07", -tuck * 0.08f);
        addDelta("upperarm_r_028", tuck * 0.08f);
        addDelta("spine_03_05", -tuck * 0.08f);
    }

    std::vector<XMMATRIX> localMatrices(m_nodes.size(), XMMatrixIdentity());

    for (size_t i = 0; i < m_nodes.size(); ++i)
    {
        const Node& node = m_nodes[i];
        const Delta& delta = deltas[i];
        const XMMATRIX restLocal = XMLoadFloat4x4(&node.restLocal);
        const XMMATRIX deltaRotation = XMMatrixRotationRollPitchYaw(
            delta.pitch,
            delta.yaw,
            delta.roll);

        // Keep the exact reconstructed bind transform intact. Add procedural
        // locomotion before it so the motion starts from the neutral bind pose.
        localMatrices[i] = deltaRotation * restLocal;
    }

    std::function<void(int, const XMMATRIX&)> evaluateNode;

    evaluateNode = [&](int nodeIndex, const XMMATRIX& parentGlobal)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
            return;

        const XMMATRIX global = localMatrices[nodeIndex] * parentGlobal;
        XMStoreFloat4x4(&m_nodes[nodeIndex].global, global);

        for (int child : m_nodes[nodeIndex].children)
            evaluateNode(child, global);
    };

    for (int root : m_rootNodes)
        evaluateNode(root, XMMatrixIdentity());
}

void AnimatedGltfModel::UpdateSkinMatrices()
{
    for (Skin& skin : m_skins)
    {
        for (size_t jointIndex = 0; jointIndex < skin.joints.size(); ++jointIndex)
        {
            const int nodeIndex = skin.joints[jointIndex];
            if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
                continue;

            const XMMATRIX inverseBind = XMLoadFloat4x4(&skin.inverseBind[jointIndex]);
            const XMMATRIX jointGlobal = XMLoadFloat4x4(&m_nodes[nodeIndex].global);
            XMStoreFloat4x4(&skin.jointMatrices[jointIndex], inverseBind * jointGlobal);
        }
    }
}

void AnimatedGltfModel::UpdateVertexBuffers(ID3D11DeviceContext* context)
{
    if (!context)
        return;

    const float infinity = std::numeric_limits<float>::infinity();
    XMFLOAT3 rawMin{ infinity, infinity, infinity };
    XMFLOAT3 rawMax{-infinity,-infinity,-infinity };

    // Pass 1: CPU skinning in the model's original coordinate space.
    for (Primitive& primitive : m_primitives)
    {
        const XMMATRIX nodeGlobal =
            (primitive.nodeIndex >= 0 && primitive.nodeIndex < static_cast<int>(m_nodes.size()))
                ? XMLoadFloat4x4(&m_nodes[primitive.nodeIndex].global)
                : XMMatrixIdentity();

        Skin* skin = nullptr;
        if (primitive.skinIndex >= 0 && primitive.skinIndex < static_cast<int>(m_skins.size()))
            skin = &m_skins[primitive.skinIndex];

        for (size_t vertexIndex = 0; vertexIndex < primitive.sourceVertices.size(); ++vertexIndex)
        {
            const SourceVertex& source = primitive.sourceVertices[vertexIndex];
            XMVECTOR sourcePosition = XMLoadFloat3(&source.position);
            XMVECTOR sourceNormal = XMLoadFloat3(&source.normal);
            XMVECTOR finalPosition = XMVectorZero();
            XMVECTOR finalNormal = XMVectorZero();

            if (source.skinned && skin)
            {
                const float weights[4] =
                {
                    source.weights.x,
                    source.weights.y,
                    source.weights.z,
                    source.weights.w
                };

                float weightSum = 0.0f;

                for (int influence = 0; influence < 4; ++influence)
                {
                    const float weight = weights[influence];
                    const uint16_t jointSlot = source.joints[influence];

                    if (weight <= 0.00001f || jointSlot >= skin->jointMatrices.size())
                        continue;

                    const XMMATRIX jointMatrix = XMLoadFloat4x4(&skin->jointMatrices[jointSlot]);
                    finalPosition += XMVector3TransformCoord(sourcePosition, jointMatrix) * weight;
                    finalNormal += XMVector3TransformNormal(sourceNormal, jointMatrix) * weight;
                    weightSum += weight;
                }

                if (weightSum > 0.00001f)
                {
                    finalPosition /= weightSum;
                    finalNormal /= weightSum;
                }
                else
                {
                    finalPosition = XMVector3TransformCoord(sourcePosition, nodeGlobal);
                    finalNormal = XMVector3TransformNormal(sourceNormal, nodeGlobal);
                }
            }
            else
            {
                finalPosition = XMVector3TransformCoord(sourcePosition, nodeGlobal);
                finalNormal = XMVector3TransformNormal(sourceNormal, nodeGlobal);
            }

            finalNormal = XMVector3Normalize(finalNormal);

            GpuVertex& destination = primitive.skinnedVertices[vertexIndex];
            XMStoreFloat3(&destination.position, finalPosition);
            XMStoreFloat3(&destination.normal, finalNormal);
            destination.uv = source.uv;

            rawMin.x = (std::min)(rawMin.x, destination.position.x);
            rawMin.y = (std::min)(rawMin.y, destination.position.y);
            rawMin.z = (std::min)(rawMin.z, destination.position.z);
            rawMax.x = (std::max)(rawMax.x, destination.position.x);
            rawMax.y = (std::max)(rawMax.y, destination.position.y);
            rawMax.z = (std::max)(rawMax.z, destination.position.z);
        }
    }

    // Establish a stable canonical model space exactly once from the bind pose.
    // This prevents the very large FBX/Sketchfab source coordinates from being
    // pushed through the world matrix every frame, and it prevents animation
    // bounds from changing the scale/centering during a jump or walk cycle.
    if (!m_normalizationReady)
    {
        const float height = (std::max)(rawMax.y - rawMin.y, 0.0001f);
        m_normalizationScale = 1.0f / height;
        m_normalizationOrigin =
        {
            (rawMin.x + rawMax.x) * 0.5f,
            rawMin.y,
            (rawMin.z + rawMax.z) * 0.5f
        };
        m_normalizationReady = true;
    }

    m_boundsMin = { infinity, infinity, infinity };
    m_boundsMax = {-infinity,-infinity,-infinity };
    m_hasBounds = false;

    // Pass 2: normalize directly in CPU vertex data and upload.
    for (Primitive& primitive : m_primitives)
    {
        for (GpuVertex& vertex : primitive.skinnedVertices)
        {
            vertex.position.x = (vertex.position.x - m_normalizationOrigin.x) * m_normalizationScale;
            vertex.position.y = (vertex.position.y - m_normalizationOrigin.y) * m_normalizationScale;
            vertex.position.z = (vertex.position.z - m_normalizationOrigin.z) * m_normalizationScale;

            m_boundsMin.x = (std::min)(m_boundsMin.x, vertex.position.x);
            m_boundsMin.y = (std::min)(m_boundsMin.y, vertex.position.y);
            m_boundsMin.z = (std::min)(m_boundsMin.z, vertex.position.z);
            m_boundsMax.x = (std::max)(m_boundsMax.x, vertex.position.x);
            m_boundsMax.y = (std::max)(m_boundsMax.y, vertex.position.y);
            m_boundsMax.z = (std::max)(m_boundsMax.z, vertex.position.z);
            m_hasBounds = true;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context->Map(primitive.vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(
                mapped.pData,
                primitive.skinnedVertices.data(),
                primitive.skinnedVertices.size() * sizeof(GpuVertex));
            context->Unmap(primitive.vertexBuffer, 0);
        }
    }
}

void AnimatedGltfModel::Update(
    ID3D11DeviceContext* context,
    float animationTime,
    Motion motion,
    float motionTime)
{
    if (!m_loaded || !context)
        return;

    EvaluatePose(animationTime, motion, motionTime);
    UpdateSkinMatrices();
    UpdateVertexBuffers(context);
}

XMMATRIX AnimatedGltfModel::MakeGroundedTransform(float targetHeight) const
{
    if (!m_hasBounds)
        return XMMatrixIdentity();

    return XMMatrixScaling(targetHeight, targetHeight, targetHeight);
}

void AnimatedGltfModel::Render(
    ID3D11DeviceContext* context,
    const XMMATRIX& world,
    const XMMATRIX& view,
    const XMMATRIX& projection,
    const XMFLOAT3& cameraPosition)
{
    if (!m_loaded || !context)
        return;

    context->IASetInputLayout(m_inputLayout);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(m_vertexShader, nullptr, 0);
    context->PSSetShader(m_pixelShader, nullptr, 0);
    context->PSSetSamplers(0, 1, &m_sampler);
    context->RSSetState(m_rasterState);

    const UINT stride = sizeof(GpuVertex);
    const UINT offset = 0;

    for (const Primitive& primitive : m_primitives)
    {
        ConstantBuffer constants{};
        // HLSL constant-buffer matrices are column-major by default.
        // Match the working static GLB renderer and transpose DirectXMath matrices
        // before upload; without this the skinned character collapses into long lines.
        constants.world = XMMatrixTranspose(world);
        constants.worldViewProjection = XMMatrixTranspose(world * view * projection);
        constants.baseColorFactor = primitive.baseColorFactor;
        constants.materialParameters =
        {
            primitive.metallicFactor,
            primitive.roughnessFactor,
            primitive.baseColorImage >= 0 ? 1.0f : 0.0f,
            primitive.metallicRoughnessImage >= 0 ? 1.0f : 0.0f
        };
        constants.lightDirection = {0.25f, -0.92f, 0.30f, 0.0f};
        constants.cameraPosition = {cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f};
        constants.alphaParameters =
        {
            primitive.alphaCutoff,
            primitive.alphaTest ? 1.0f : 0.0f,
            0.0f,
            0.0f
        };

        context->UpdateSubresource(m_constantBuffer, 0, nullptr, &constants, 0, 0);
        context->VSSetConstantBuffers(0, 1, &m_constantBuffer);
        context->PSSetConstantBuffers(0, 1, &m_constantBuffer);

        ID3D11ShaderResourceView* baseTexture = m_whiteTexture;
        if (primitive.baseColorImage >= 0 &&
            primitive.baseColorImage < static_cast<int>(m_images.size()) &&
            m_images[primitive.baseColorImage])
        {
            baseTexture = m_images[primitive.baseColorImage];
        }

        ID3D11ShaderResourceView* mrTexture = m_whiteTexture;
        if (primitive.metallicRoughnessImage >= 0 &&
            primitive.metallicRoughnessImage < static_cast<int>(m_images.size()) &&
            m_images[primitive.metallicRoughnessImage])
        {
            mrTexture = m_images[primitive.metallicRoughnessImage];
        }

        ID3D11ShaderResourceView* textures[2] = {baseTexture, mrTexture};
        context->PSSetShaderResources(0, 2, textures);
        context->IASetVertexBuffers(0, 1, &primitive.vertexBuffer, &stride, &offset);
        context->IASetIndexBuffer(primitive.indexBuffer, DXGI_FORMAT_R32_UINT, 0);
        context->DrawIndexed(primitive.indexCount, 0, 0);
    }
}
