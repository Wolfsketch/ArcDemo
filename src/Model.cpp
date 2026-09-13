#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE

#include "Model.h"

#include <tiny_gltf.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>
#include <vector>

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

        size_t offset = view.byteOffset + accessor.byteOffset;

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

    int GetImageFromTexture(
        const tinygltf::Model& model,
        int textureIndex)
    {
        if (textureIndex < 0 ||
            textureIndex >= static_cast<int>(model.textures.size()))
            return -1;

        int imageIndex = model.textures[textureIndex].source;

        if (imageIndex < 0 ||
            imageIndex >= static_cast<int>(model.images.size()))
            return -1;

        return imageIndex;
    }
}

GltfModel::~GltfModel()
{
    ReleaseResources();
}

void GltfModel::ReleaseResources()
{
    for (Primitive& primitive : m_primitives)
    {
        SafeRelease(primitive.vertexBuffer);
        SafeRelease(primitive.indexBuffer);
    }

    m_primitives.clear();

    for (ID3D11ShaderResourceView*& image : m_images)
        SafeRelease(image);

    m_images.clear();

    SafeRelease(m_whiteTexture);
    SafeRelease(m_sampler);
    SafeRelease(m_constantBuffer);
    SafeRelease(m_inputLayout);
    SafeRelease(m_pixelShader);
    SafeRelease(m_vertexShader);

    m_loaded = false;
    m_hasBounds = false;
}

bool GltfModel::CreatePipeline(
    ID3D11Device* device,
    std::string& error)
{
    const char* vertexShaderSource = R"(
        cbuffer ModelConstantBuffer : register(b0)
        {
            matrix world;
            matrix worldViewProjection;
            float4 baseColorFactor;
            float4 materialParameters;
            float4 lightDirection;
            float4 cameraPosition;
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

            output.position = mul(
                float4(input.position, 1.0f),
                worldViewProjection);

            output.worldPosition = mul(
                float4(input.position, 1.0f),
                world).xyz;

            output.normal = normalize(
                mul(float4(input.normal, 0.0f), world).xyz);

            output.uv = input.uv;
            return output;
        }
    )";

    const char* pixelShaderSource = R"(
        Texture2D BaseColorTexture : register(t0);
        Texture2D MetallicRoughnessTexture : register(t1);
        SamplerState TextureSampler : register(s0);

        cbuffer ModelConstantBuffer : register(b0)
        {
            matrix world;
            matrix worldViewProjection;
            float4 baseColorFactor;
            float4 materialParameters;
            float4 lightDirection;
            float4 cameraPosition;
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

            return NdotV /
                (NdotV * (1.0f - k) + k);
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
            return F0 +
                (1.0f - F0) *
                pow(saturate(1.0f - cosTheta), 5.0f);
        }

        float4 main(PSInput input) : SV_TARGET
        {
            float hasBaseTexture = materialParameters.z;
            float hasMRTexture = materialParameters.w;

            float4 sampledBase = BaseColorTexture.Sample(
                TextureSampler,
                input.uv);

            // glTF baseColorFactor is already linear.
            // Only the sampled base-color texture is sRGB encoded.
            float3 albedo = max(
                baseColorFactor.rgb,
                float3(0.0001f,0.0001f,0.0001f));

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
                float4 mr = MetallicRoughnessTexture.Sample(
                    TextureSampler,
                    input.uv);

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

            float3 lightColor = float3(1.55f, 1.50f, 1.40f);

            float3 directLight =
                (kD * albedo / PI + specular) *
                lightColor *
                NdotL;

            float3 ambient = albedo * 0.07f;
            float3 color = ambient + directLight;

            color = color / (color + 1.0f);
            color = pow(max(color, 0.0f), 1.0f / 2.2f);

            return float4(color, 1.0f);
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
            error = "Model vertex shader kon niet compileren.";

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
            error = "Model pixel shader kon niet compileren.";

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
        error = "Model vertex shader kon niet aangemaakt worden.";
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
        error = "Model pixel shader kon niet aangemaakt worden.";
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
        error = "GLB input layout kon niet aangemaakt worden.";
        return false;
    }

    D3D11_BUFFER_DESC constantDesc{};
    constantDesc.ByteWidth = sizeof(ModelConstantBuffer);
    constantDesc.Usage = D3D11_USAGE_DEFAULT;
    constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = device->CreateBuffer(
        &constantDesc,
        nullptr,
        &m_constantBuffer);

    if (FAILED(hr))
    {
        error = "GLB constant buffer kon niet aangemaakt worden.";
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
        error = "GLB texture sampler kon niet aangemaakt worden.";
        return false;
    }

    const uint32_t whitePixel = 0xFFFFFFFF;

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

    if (FAILED(hr))
    {
        error = "Fallback texture kon niet aangemaakt worden.";
        return false;
    }

    hr = device->CreateShaderResourceView(
        whiteTexture,
        nullptr,
        &m_whiteTexture);

    SafeRelease(whiteTexture);

    if (FAILED(hr))
    {
        error = "Fallback texture view kon niet aangemaakt worden.";
        return false;
    }

    return true;
}

bool GltfModel::Load(
    ID3D11Device* device,
    const std::string& filename,
    std::string& error)
{
    ReleaseResources();

    if (!device)
    {
        error = "Geen DirectX device beschikbaar.";
        return false;
    }

    if (!CreatePipeline(device, error))
        return false;

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;

    std::string warning;
    std::string loadingError;

    bool success = loader.LoadBinaryFromFile(
        &model,
        &loadingError,
        &warning,
        filename);

    if (!warning.empty())
        OutputDebugStringA(warning.c_str());

    if (!success)
    {
        error = "GLB kon niet geladen worden:\n" + loadingError;
        return false;
    }

    m_images.resize(model.images.size(), nullptr);

    for (size_t i = 0; i < model.images.size(); ++i)
    {
        const tinygltf::Image& image = model.images[i];

        if (image.width <= 0 ||
            image.height <= 0 ||
            image.image.empty() ||
            image.bits != 8)
            continue;

        const size_t pixelCount =
            static_cast<size_t>(image.width) *
            static_cast<size_t>(image.height);

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
                unsigned char value = image.image[pixel * 2 + 0];
                rgba[pixel * 4 + 0] = value;
                rgba[pixel * 4 + 1] = value;
                rgba[pixel * 4 + 2] = value;
                rgba[pixel * 4 + 3] = image.image[pixel * 2 + 1];
            }
            else if (image.component == 1)
            {
                unsigned char value = image.image[pixel];
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

        HRESULT hr = device->CreateTexture2D(
            &textureDesc,
            &textureData,
            &texture);

        if (SUCCEEDED(hr))
        {
            device->CreateShaderResourceView(
                texture,
                nullptr,
                &m_images[i]);
        }

        SafeRelease(texture);
    }

    float infinity = std::numeric_limits<float>::infinity();

    m_boundsMin = { infinity,  infinity,  infinity};
    m_boundsMax = {-infinity, -infinity, -infinity};

    auto ProcessMesh =
        [&](int meshIndex, const XMMATRIX& nodeTransform) -> bool
    {
        if (meshIndex < 0 ||
            meshIndex >= static_cast<int>(model.meshes.size()))
            return true;

        const tinygltf::Mesh& mesh = model.meshes[meshIndex];

        for (const tinygltf::Primitive& sourcePrimitive : mesh.primitives)
        {
            if (sourcePrimitive.mode != TINYGLTF_MODE_TRIANGLES &&
                sourcePrimitive.mode != -1)
                continue;

            auto positionIt = sourcePrimitive.attributes.find("POSITION");

            if (positionIt == sourcePrimitive.attributes.end())
                continue;

            const tinygltf::Accessor& positionAccessor =
                model.accessors[positionIt->second];

            if (positionAccessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
                positionAccessor.type != TINYGLTF_TYPE_VEC3)
                continue;

            size_t positionStride = 0;

            const unsigned char* positionData = GetAccessorData(
                model,
                positionAccessor,
                positionStride);

            if (!positionData)
                continue;

            const tinygltf::Accessor* normalAccessor = nullptr;
            const unsigned char* normalData = nullptr;
            size_t normalStride = 0;

            auto normalIt = sourcePrimitive.attributes.find("NORMAL");

            if (normalIt != sourcePrimitive.attributes.end())
            {
                normalAccessor = &model.accessors[normalIt->second];
                normalData = GetAccessorData(
                    model,
                    *normalAccessor,
                    normalStride);
            }

            const tinygltf::Accessor* uvAccessor = nullptr;
            const unsigned char* uvData = nullptr;
            size_t uvStride = 0;

            auto uvIt = sourcePrimitive.attributes.find("TEXCOORD_0");

            if (uvIt != sourcePrimitive.attributes.end())
            {
                uvAccessor = &model.accessors[uvIt->second];
                uvData = GetAccessorData(
                    model,
                    *uvAccessor,
                    uvStride);
            }

            std::vector<Vertex> vertices(positionAccessor.count);

            for (size_t vertexIndex = 0;
                 vertexIndex < positionAccessor.count;
                 ++vertexIndex)
            {
                const float* position = reinterpret_cast<const float*>(
                    positionData + vertexIndex * positionStride);

                vertices[vertexIndex].position =
                {
                    position[0],
                    position[1],
                    position[2]
                };

                if (normalData &&
                    normalAccessor &&
                    normalAccessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
                {
                    const float* normal = reinterpret_cast<const float*>(
                        normalData + vertexIndex * normalStride);

                    vertices[vertexIndex].normal =
                    {
                        normal[0],
                        normal[1],
                        normal[2]
                    };
                }
                else
                {
                    vertices[vertexIndex].normal = {0,1,0};
                }

                if (uvData &&
                    uvAccessor &&
                    uvAccessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
                {
                    const float* uv = reinterpret_cast<const float*>(
                        uvData + vertexIndex * uvStride);

                    vertices[vertexIndex].uv = {uv[0], uv[1]};
                }
                else
                {
                    vertices[vertexIndex].uv = {0,0};
                }

                XMVECTOR p = XMVectorSet(
                    position[0],
                    position[1],
                    position[2],
                    1.0f);

                p = XMVector3TransformCoord(p, nodeTransform);

                XMFLOAT3 transformed{};
                XMStoreFloat3(&transformed, p);

                m_boundsMin.x = (std::min)(m_boundsMin.x, transformed.x);
                m_boundsMin.y = (std::min)(m_boundsMin.y, transformed.y);
                m_boundsMin.z = (std::min)(m_boundsMin.z, transformed.z);

                m_boundsMax.x = (std::max)(m_boundsMax.x, transformed.x);
                m_boundsMax.y = (std::max)(m_boundsMax.y, transformed.y);
                m_boundsMax.z = (std::max)(m_boundsMax.z, transformed.z);

                m_hasBounds = true;
            }

            std::vector<uint32_t> indices;

            if (sourcePrimitive.indices >= 0)
            {
                const tinygltf::Accessor& indexAccessor =
                    model.accessors[sourcePrimitive.indices];

                size_t indexStride = 0;

                const unsigned char* indexData = GetAccessorData(
                    model,
                    indexAccessor,
                    indexStride);

                if (!indexData)
                    continue;

                indices.resize(indexAccessor.count);

                for (size_t index = 0;
                     index < indexAccessor.count;
                     ++index)
                {
                    const unsigned char* source =
                        indexData + index * indexStride;

                    switch (indexAccessor.componentType)
                    {
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                            indices[index] = *reinterpret_cast<const uint8_t*>(source);
                            break;

                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                            indices[index] = *reinterpret_cast<const uint16_t*>(source);
                            break;

                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                            indices[index] = *reinterpret_cast<const uint32_t*>(source);
                            break;

                        default:
                            indices[index] = 0;
                            break;
                    }
                }
            }
            else
            {
                indices.resize(vertices.size());

                for (size_t i = 0; i < vertices.size(); ++i)
                    indices[i] = static_cast<uint32_t>(i);
            }

            if (vertices.empty() || indices.empty())
                continue;

            Primitive primitive{};
            XMStoreFloat4x4(&primitive.nodeTransform, nodeTransform);

            if (sourcePrimitive.material >= 0 &&
                sourcePrimitive.material < static_cast<int>(model.materials.size()))
            {
                const tinygltf::Material& material =
                    model.materials[sourcePrimitive.material];

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

                primitive.baseColorImage = GetImageFromTexture(
                    model,
                    pbr.baseColorTexture.index);

                primitive.metallicRoughnessImage = GetImageFromTexture(
                    model,
                    pbr.metallicRoughnessTexture.index);
            }

            D3D11_BUFFER_DESC vertexDesc{};
            vertexDesc.ByteWidth = static_cast<UINT>(
                vertices.size() * sizeof(Vertex));
            vertexDesc.Usage = D3D11_USAGE_DEFAULT;
            vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

            D3D11_SUBRESOURCE_DATA vertexInitial{};
            vertexInitial.pSysMem = vertices.data();

            HRESULT hr = device->CreateBuffer(
                &vertexDesc,
                &vertexInitial,
                &primitive.vertexBuffer);

            if (FAILED(hr))
                continue;

            D3D11_BUFFER_DESC indexDesc{};
            indexDesc.ByteWidth = static_cast<UINT>(
                indices.size() * sizeof(uint32_t));
            indexDesc.Usage = D3D11_USAGE_DEFAULT;
            indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

            D3D11_SUBRESOURCE_DATA indexInitial{};
            indexInitial.pSysMem = indices.data();

            hr = device->CreateBuffer(
                &indexDesc,
                &indexInitial,
                &primitive.indexBuffer);

            if (FAILED(hr))
            {
                SafeRelease(primitive.vertexBuffer);
                continue;
            }

            primitive.indexCount = static_cast<UINT>(indices.size());
            m_primitives.push_back(primitive);
        }

        return true;
    };

    std::function<void(int, const XMMATRIX&)> VisitNode;

    VisitNode =
        [&](int nodeIndex, const XMMATRIX& parentTransform)
    {
        if (nodeIndex < 0 ||
            nodeIndex >= static_cast<int>(model.nodes.size()))
            return;

        const tinygltf::Node& node = model.nodes[nodeIndex];

        XMMATRIX local = GetNodeTransform(node);
        XMMATRIX combined = local * parentTransform;

        if (node.mesh >= 0)
            ProcessMesh(node.mesh, combined);

        for (int child : node.children)
            VisitNode(child, combined);
    };

    if (!model.scenes.empty())
    {
        int sceneIndex = model.defaultScene;

        if (sceneIndex < 0 ||
            sceneIndex >= static_cast<int>(model.scenes.size()))
            sceneIndex = 0;

        for (int rootNode : model.scenes[sceneIndex].nodes)
            VisitNode(rootNode, XMMatrixIdentity());
    }
    else
    {
        for (int i = 0; i < static_cast<int>(model.nodes.size()); ++i)
            VisitNode(i, XMMatrixIdentity());
    }

    if (m_primitives.empty())
    {
        error = "GLB is geladen, maar bevat geen bruikbare triangle meshes.";
        return false;
    }

    m_loaded = true;
    return true;
}

XMMATRIX GltfModel::MakeNormalizedTransform(float targetSize) const
{
    if (!m_hasBounds)
        return XMMatrixIdentity();

    XMFLOAT3 center
    {
        (m_boundsMin.x + m_boundsMax.x) * 0.5f,
        (m_boundsMin.y + m_boundsMax.y) * 0.5f,
        (m_boundsMin.z + m_boundsMax.z) * 0.5f
    };

    float sizeX = m_boundsMax.x - m_boundsMin.x;
    float sizeY = m_boundsMax.y - m_boundsMin.y;
    float sizeZ = m_boundsMax.z - m_boundsMin.z;

    float largest = (std::max)(sizeX, (std::max)(sizeY, sizeZ));

    if (largest <= 0.0001f)
        largest = 1.0f;

    float scale = targetSize / largest;

    return XMMatrixTranslation(
        -center.x,
        -center.y,
        -center.z) *
        XMMatrixScaling(scale, scale, scale);
}

void GltfModel::Render(
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
    context->VSSetConstantBuffers(0, 1, &m_constantBuffer);
    context->PSSetConstantBuffers(0, 1, &m_constantBuffer);
    context->PSSetSamplers(0, 1, &m_sampler);

    for (const Primitive& primitive : m_primitives)
    {
        UINT stride = sizeof(Vertex);
        UINT offset = 0;

        context->IASetVertexBuffers(
            0,
            1,
            &primitive.vertexBuffer,
            &stride,
            &offset);

        context->IASetIndexBuffer(
            primitive.indexBuffer,
            DXGI_FORMAT_R32_UINT,
            0);

        XMMATRIX nodeTransform = XMLoadFloat4x4(&primitive.nodeTransform);
        XMMATRIX finalWorld = nodeTransform * world;

        ModelConstantBuffer cb{};
        cb.world = XMMatrixTranspose(finalWorld);
        cb.worldViewProjection = XMMatrixTranspose(
            finalWorld * view * projection);
        cb.baseColorFactor = primitive.baseColorFactor;

        bool hasBaseTexture =
            primitive.baseColorImage >= 0 &&
            primitive.baseColorImage < static_cast<int>(m_images.size()) &&
            m_images[primitive.baseColorImage] != nullptr;

        bool hasMRTexture =
            primitive.metallicRoughnessImage >= 0 &&
            primitive.metallicRoughnessImage < static_cast<int>(m_images.size()) &&
            m_images[primitive.metallicRoughnessImage] != nullptr;

        cb.materialParameters =
        {
            primitive.metallicFactor,
            primitive.roughnessFactor,
            hasBaseTexture ? 1.0f : 0.0f,
            hasMRTexture ? 1.0f : 0.0f
        };

        cb.lightDirection = {-0.35f, -0.75f, -0.50f, 0.0f};
        cb.cameraPosition =
        {
            cameraPosition.x,
            cameraPosition.y,
            cameraPosition.z,
            1.0f
        };

        context->UpdateSubresource(
            m_constantBuffer,
            0,
            nullptr,
            &cb,
            0,
            0);

        ID3D11ShaderResourceView* textures[2] =
        {
            hasBaseTexture
                ? m_images[primitive.baseColorImage]
                : m_whiteTexture,

            hasMRTexture
                ? m_images[primitive.metallicRoughnessImage]
                : m_whiteTexture
        };

        context->PSSetShaderResources(0, 2, textures);
        context->DrawIndexed(primitive.indexCount, 0, 0);
    }

    ID3D11ShaderResourceView* nullTextures[2] = {nullptr, nullptr};
    context->PSSetShaderResources(0, 2, nullTextures);
}
