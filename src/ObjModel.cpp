#ifndef NOMINMAX
#define NOMINMAX
#endif

#define TINYOBJLOADER_IMPLEMENTATION

#include "ObjModel.h"

#include <tiny_obj_loader.h>
#include <stb_image.h>

#include <d3dcompiler.h>

#include <vector>
#include <algorithm>
#include <limits>
#include <cmath>
#include <cstring>
#include <sstream>

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

    XMFLOAT3 Subtract(
        const XMFLOAT3& a,
        const XMFLOAT3& b)
    {
        return
        {
            a.x - b.x,
            a.y - b.y,
            a.z - b.z
        };
    }

    XMFLOAT2 Subtract(
        const XMFLOAT2& a,
        const XMFLOAT2& b)
    {
        return
        {
            a.x - b.x,
            a.y - b.y
        };
    }

    XMFLOAT3 CalculateFaceNormal(
        const XMFLOAT3& p0,
        const XMFLOAT3& p1,
        const XMFLOAT3& p2)
    {
        XMVECTOR a =
            XMLoadFloat3(&p0);

        XMVECTOR b =
            XMLoadFloat3(&p1);

        XMVECTOR c =
            XMLoadFloat3(&p2);

        XMVECTOR edge1 =
            b - a;

        XMVECTOR edge2 =
            c - a;

        XMVECTOR normal =
            XMVector3Normalize(
                XMVector3Cross(
                    edge1,
                    edge2
                )
            );

        XMFLOAT3 result{};

        XMStoreFloat3(
            &result,
            normal
        );

        return result;
    }

    XMFLOAT4 CreateFallbackTangent(
        const XMFLOAT3& normal)
    {
        XMVECTOR n =
            XMLoadFloat3(&normal);

        XMVECTOR up =
            XMVectorSet(
                0.0f,
                1.0f,
                0.0f,
                0.0f
            );

        float dot =
            fabsf(
                XMVectorGetX(
                    XMVector3Dot(
                        n,
                        up
                    )
                )
            );

        if (dot > 0.95f)
        {
            up =
                XMVectorSet(
                    1.0f,
                    0.0f,
                    0.0f,
                    0.0f
                );
        }

        XMVECTOR tangent =
            XMVector3Normalize(
                XMVector3Cross(
                    up,
                    n
                )
            );

        XMFLOAT3 tangentFloat{};

        XMStoreFloat3(
            &tangentFloat,
            tangent
        );

        return
        {
            tangentFloat.x,
            tangentFloat.y,
            tangentFloat.z,
            1.0f
        };
    }
}

// ============================================================
// Constructor / destructor
// ============================================================

ObjModel::ObjModel() = default;

ObjModel::~ObjModel()
{
    ReleaseResources();
}

// ============================================================
// State
// ============================================================

bool ObjModel::IsLoaded() const
{
    return m_loaded;
}

// ============================================================
// Cleanup
// ============================================================

void ObjModel::ReleaseResources()
{
    SafeRelease(
        m_vertexBuffer
    );

    SafeRelease(
        m_indexBuffer
    );

    SafeRelease(
        m_diffuseTexture
    );

    SafeRelease(
        m_normalTexture
    );

    SafeRelease(
        m_specularTexture
    );

    SafeRelease(
        m_vertexShader
    );

    SafeRelease(
        m_pixelShader
    );

    SafeRelease(
        m_inputLayout
    );

    SafeRelease(
        m_constantBuffer
    );

    SafeRelease(
        m_sampler
    );

    m_indexCount = 0;

    m_loaded = false;
    m_hasBounds = false;
}

// ============================================================
// Texture loading
//
// stb_image implementation is already provided by Model.cpp.
// Therefore DO NOT define STB_IMAGE_IMPLEMENTATION here.
// ============================================================

bool ObjModel::LoadTexture(
    ID3D11Device* device,
    const std::string& filename,
    bool srgb,
    ID3D11ShaderResourceView** output,
    std::string& error)
{
    if (!device || !output)
    {
        error =
            "Ongeldige parameters voor LoadTexture.";

        return false;
    }

    *output = nullptr;

    int width = 0;
    int height = 0;
    int channels = 0;

    unsigned char* pixels =
        stbi_load(
            filename.c_str(),
            &width,
            &height,
            &channels,
            STBI_rgb_alpha
        );

    if (!pixels)
    {
        error =
            "Texture kon niet worden geladen:\n" +
            filename;

        return false;
    }

    D3D11_TEXTURE2D_DESC textureDesc{};

    textureDesc.Width =
        static_cast<UINT>(
            width
        );

    textureDesc.Height =
        static_cast<UINT>(
            height
        );

    textureDesc.MipLevels =
        1;

    textureDesc.ArraySize =
        1;

    textureDesc.Format =
        srgb
        ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        : DXGI_FORMAT_R8G8B8A8_UNORM;

    textureDesc.SampleDesc.Count =
        1;

    textureDesc.Usage =
        D3D11_USAGE_IMMUTABLE;

    textureDesc.BindFlags =
        D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA textureData{};

    textureData.pSysMem =
        pixels;

    textureData.SysMemPitch =
        static_cast<UINT>(
            width * 4
        );

    ID3D11Texture2D* texture =
        nullptr;

    HRESULT hr =
        device->CreateTexture2D(
            &textureDesc,
            &textureData,
            &texture
        );

    stbi_image_free(
        pixels
    );

    if (FAILED(hr))
    {
        error =
            "DirectX kon texture niet maken:\n" +
            filename;

        return false;
    }

    hr =
        device->CreateShaderResourceView(
            texture,
            nullptr,
            output
        );

    SafeRelease(
        texture
    );

    if (FAILED(hr))
    {
        error =
            "DirectX kon ShaderResourceView niet maken:\n" +
            filename;

        return false;
    }

    return true;
}

// ============================================================
// Create DirectX pipeline
// ============================================================

bool ObjModel::CreatePipeline(
    ID3D11Device* device,
    std::string& error)
{
    // --------------------------------------------------------
    // Vertex shader
    // --------------------------------------------------------

    const char* vertexShaderSource = R"(

        cbuffer ObjectBuffer : register(b0)
        {
            matrix world;
            matrix worldViewProjection;

            float4 cameraPosition;
            float4 lightDirection;

            float4 material;
        };

        struct VSInput
        {
            float3 position : POSITION;
            float3 normal   : NORMAL;
            float2 uv       : TEXCOORD0;
            float4 tangent  : TANGENT;
        };

        struct VSOutput
        {
            float4 position      : SV_POSITION;
            float3 worldPosition : TEXCOORD1;

            float3 normal  : NORMAL;
            float2 uv      : TEXCOORD0;
            float4 tangent : TANGENT;
        };

        VSOutput main(
            VSInput input)
        {
            VSOutput output;

            output.position =
                mul(
                    float4(
                        input.position,
                        1.0f
                    ),
                    worldViewProjection
                );

            output.worldPosition =
                mul(
                    float4(
                        input.position,
                        1.0f
                    ),
                    world
                ).xyz;

            output.normal =
                normalize(
                    mul(
                        float4(
                            input.normal,
                            0.0f
                        ),
                        world
                    ).xyz
                );

            output.tangent.xyz =
                normalize(
                    mul(
                        float4(
                            input.tangent.xyz,
                            0.0f
                        ),
                        world
                    ).xyz
                );

            output.tangent.w =
                input.tangent.w;

            output.uv =
                input.uv;

            return output;
        }

    )";

    // --------------------------------------------------------
    // Pixel shader
    // --------------------------------------------------------

    const char* pixelShaderSource = R"(

        Texture2D DiffuseTexture
            : register(t0);

        Texture2D NormalTexture
            : register(t1);

        Texture2D SpecularTexture
            : register(t2);

        SamplerState TextureSampler
            : register(s0);

        cbuffer ObjectBuffer : register(b0)
        {
            matrix world;
            matrix worldViewProjection;

            float4 cameraPosition;
            float4 lightDirection;

            float4 material;
        };

        struct PSInput
        {
            float4 position      : SV_POSITION;
            float3 worldPosition : TEXCOORD1;

            float3 normal  : NORMAL;
            float2 uv      : TEXCOORD0;
            float4 tangent : TANGENT;
        };

        float4 main(
            PSInput input
        ) : SV_TARGET
        {
            float hasDiffuse =
                material.x;

            float hasNormal =
                material.y;

            float hasSpecular =
                material.z;

            float specularPower =
                material.w;

            // ------------------------------------------------
            // Base color
            // ------------------------------------------------

            float3 baseColor =
                float3(
                    0.55f,
                    0.48f,
                    0.38f
                );

            if (hasDiffuse > 0.5f)
            {
                baseColor =
                    DiffuseTexture.Sample(
                        TextureSampler,
                        input.uv
                    ).rgb;
            }

            // Diffuse texture is stored as sRGB.
            // Hardware already converts SRGB texture to linear.

            // ------------------------------------------------
            // Normal
            // ------------------------------------------------

            float3 N =
                normalize(
                    input.normal
                );

            if (hasNormal > 0.5f)
            {
                float3 T =
                    normalize(
                        input.tangent.xyz
                    );

                float3 B =
                    normalize(
                        cross(
                            N,
                            T
                        )
                    )
                    *
                    input.tangent.w;

                float3 normalSample =
                    NormalTexture.Sample(
                        TextureSampler,
                        input.uv
                    ).xyz;

                normalSample =
                    normalSample *
                    2.0f -
                    1.0f;

                float3x3 TBN =
                    float3x3(
                        T,
                        B,
                        N
                    );

                N =
                    normalize(
                        mul(
                            normalSample,
                            TBN
                        )
                    );
            }

            // ------------------------------------------------
            // Lighting
            // ------------------------------------------------

            float3 L =
                normalize(
                    -lightDirection.xyz
                );

            float3 V =
                normalize(
                    cameraPosition.xyz -
                    input.worldPosition
                );

            float3 H =
                normalize(
                    L + V
                );

            float NdotL =
                max(
                    dot(
                        N,
                        L
                    ),
                    0.0f
                );

            // ------------------------------------------------
            // Specular map
            // ------------------------------------------------

            float specularStrength =
                0.15f;

            if (hasSpecular > 0.5f)
            {
                float3 specSample =
                    SpecularTexture.Sample(
                        TextureSampler,
                        input.uv
                    ).rgb;

                specularStrength =
                    dot(
                        specSample,
                        float3(
                            0.3333f,
                            0.3333f,
                            0.3333f
                        )
                    );
            }

            float specular =
                pow(
                    max(
                        dot(
                            N,
                            H
                        ),
                        0.0f
                    ),
                    specularPower
                )
                *
                specularStrength;

            // ------------------------------------------------
            // Desert lighting
            // ------------------------------------------------

            float3 sunColor =
                float3(
                    1.20f,
                    1.05f,
                    0.85f
                );

            float3 ambientColor =
                float3(
                    0.19f,
                    0.22f,
                    0.25f
                );

            float3 ambient =
                baseColor *
                ambientColor;

            float3 diffuse =
                baseColor *
                sunColor *
                NdotL;

            float3 specularColor =
                sunColor *
                specular *
                0.65f;

            float3 finalColor =
                ambient +
                diffuse +
                specularColor;

            // ------------------------------------------------
            // Simple tone mapping
            // ------------------------------------------------

            finalColor =
                finalColor /
                (
                    finalColor +
                    1.0f
                );

            // Linear -> screen gamma

            finalColor =
                pow(
                    max(
                        finalColor,
                        0.0f
                    ),
                    1.0f / 2.2f
                );

            return float4(
                finalColor,
                1.0f
            );
        }

    )";

    ID3DBlob* vertexBlob =
        nullptr;

    ID3DBlob* pixelBlob =
        nullptr;

    ID3DBlob* errorBlob =
        nullptr;

    HRESULT hr =
        D3DCompile(
            vertexShaderSource,
            strlen(
                vertexShaderSource
            ),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "vs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS,
            0,
            &vertexBlob,
            &errorBlob
        );

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            error.assign(
                static_cast<const char*>(
                    errorBlob->GetBufferPointer()
                ),
                errorBlob->GetBufferSize()
            );
        }
        else
        {
            error =
                "OBJ vertex shader kon niet compileren.";
        }

        SafeRelease(
            errorBlob
        );

        SafeRelease(
            vertexBlob
        );

        return false;
    }

    SafeRelease(
        errorBlob
    );

    hr =
        D3DCompile(
            pixelShaderSource,
            strlen(
                pixelShaderSource
            ),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "ps_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS,
            0,
            &pixelBlob,
            &errorBlob
        );

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            error.assign(
                static_cast<const char*>(
                    errorBlob->GetBufferPointer()
                ),
                errorBlob->GetBufferSize()
            );
        }
        else
        {
            error =
                "OBJ pixel shader kon niet compileren.";
        }

        SafeRelease(
            errorBlob
        );

        SafeRelease(
            vertexBlob
        );

        SafeRelease(
            pixelBlob
        );

        return false;
    }

    SafeRelease(
        errorBlob
    );

    // --------------------------------------------------------
    // DirectX shaders
    // --------------------------------------------------------

    hr =
        device->CreateVertexShader(
            vertexBlob->GetBufferPointer(),
            vertexBlob->GetBufferSize(),
            nullptr,
            &m_vertexShader
        );

    if (FAILED(hr))
    {
        error =
            "OBJ vertex shader kon niet worden aangemaakt.";

        SafeRelease(vertexBlob);
        SafeRelease(pixelBlob);

        return false;
    }

    hr =
        device->CreatePixelShader(
            pixelBlob->GetBufferPointer(),
            pixelBlob->GetBufferSize(),
            nullptr,
            &m_pixelShader
        );

    if (FAILED(hr))
    {
        error =
            "OBJ pixel shader kon niet worden aangemaakt.";

        SafeRelease(vertexBlob);
        SafeRelease(pixelBlob);

        return false;
    }

    // --------------------------------------------------------
    // Input layout
    // --------------------------------------------------------

    D3D11_INPUT_ELEMENT_DESC elements[] =
    {
        {
            "POSITION",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            0,
            D3D11_INPUT_PER_VERTEX_DATA,
            0
        },

        {
            "NORMAL",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            12,
            D3D11_INPUT_PER_VERTEX_DATA,
            0
        },

        {
            "TEXCOORD",
            0,
            DXGI_FORMAT_R32G32_FLOAT,
            0,
            24,
            D3D11_INPUT_PER_VERTEX_DATA,
            0
        },

        {
            "TANGENT",
            0,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            0,
            32,
            D3D11_INPUT_PER_VERTEX_DATA,
            0
        }
    };

    hr =
        device->CreateInputLayout(
            elements,
            4,
            vertexBlob->GetBufferPointer(),
            vertexBlob->GetBufferSize(),
            &m_inputLayout
        );

    SafeRelease(
        vertexBlob
    );

    SafeRelease(
        pixelBlob
    );

    if (FAILED(hr))
    {
        error =
            "OBJ input layout kon niet worden aangemaakt.";

        return false;
    }

    // --------------------------------------------------------
    // Constant buffer
    // --------------------------------------------------------

    D3D11_BUFFER_DESC cbDesc{};

    cbDesc.ByteWidth =
        sizeof(ConstantBuffer);

    cbDesc.Usage =
        D3D11_USAGE_DEFAULT;

    cbDesc.BindFlags =
        D3D11_BIND_CONSTANT_BUFFER;

    hr =
        device->CreateBuffer(
            &cbDesc,
            nullptr,
            &m_constantBuffer
        );

    if (FAILED(hr))
    {
        error =
            "OBJ constant buffer kon niet worden aangemaakt.";

        return false;
    }

    // --------------------------------------------------------
    // Sampler
    // --------------------------------------------------------

    D3D11_SAMPLER_DESC samplerDesc{};

    samplerDesc.Filter =
        D3D11_FILTER_MIN_MAG_MIP_LINEAR;

    samplerDesc.AddressU =
        D3D11_TEXTURE_ADDRESS_WRAP;

    samplerDesc.AddressV =
        D3D11_TEXTURE_ADDRESS_WRAP;

    samplerDesc.AddressW =
        D3D11_TEXTURE_ADDRESS_WRAP;

    samplerDesc.MaxLOD =
        D3D11_FLOAT32_MAX;

    hr =
        device->CreateSamplerState(
            &samplerDesc,
            &m_sampler
        );

    if (FAILED(hr))
    {
        error =
            "OBJ sampler kon niet worden aangemaakt.";

        return false;
    }

    return true;
}

// ============================================================
// Load OBJ
// ============================================================

bool ObjModel::Load(
    ID3D11Device* device,
    const std::string& objFilename,
    const std::string& diffuseFilename,
    const std::string& normalFilename,
    const std::string& specularFilename,
    std::string& error)
{
    ReleaseResources();

    if (!device)
    {
        error =
            "Geen DirectX device beschikbaar.";

        return false;
    }

    if (!CreatePipeline(
        device,
        error))
    {
        return false;
    }

    // ========================================================
    // TinyObjLoader
    // ========================================================

    tinyobj::ObjReaderConfig config{};

    config.triangulate =
        true;

    tinyobj::ObjReader reader;

    if (!reader.ParseFromFile(
        objFilename,
        config))
    {
        error =
            "OBJ kon niet worden geladen:\n" +
            objFilename +
            "\n\n" +
            reader.Error();

        return false;
    }

    if (!reader.Warning().empty())
    {
        OutputDebugStringA(
            reader.Warning().c_str()
        );
    }

    const tinyobj::attrib_t& attrib =
        reader.GetAttrib();

    const std::vector<tinyobj::shape_t>& shapes =
        reader.GetShapes();

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // ========================================================
    // Bounds
    // ========================================================

    float infinity =
        std::numeric_limits<float>::infinity();

    m_boundsMin =
    {
         infinity,
         infinity,
         infinity
    };

    m_boundsMax =
    {
        -infinity,
        -infinity,
        -infinity
    };

    // ========================================================
    // Read shapes
    // ========================================================

    for (const tinyobj::shape_t& shape : shapes)
    {
        size_t indexOffset =
            0;

        for (size_t face = 0;
             face <
                shape.mesh.num_face_vertices.size();
             ++face)
        {
            int verticesInFace =
                shape.mesh.num_face_vertices[face];

            // We requested triangulation.
            if (verticesInFace != 3)
            {
                indexOffset +=
                    static_cast<size_t>(
                        verticesInFace
                    );

                continue;
            }

            Vertex triangle[3]{};

            bool normalsAvailable =
                true;

            // ------------------------------------------------
            // Load three vertices
            // ------------------------------------------------

            for (int corner = 0;
                 corner < 3;
                 ++corner)
            {
                const tinyobj::index_t& index =
                    shape.mesh.indices[
                        indexOffset +
                        static_cast<size_t>(corner)
                    ];

                Vertex vertex{};

                // --------------------------------------------
                // Position
                // --------------------------------------------

                if (index.vertex_index >= 0)
                {
                    size_t base =
                        static_cast<size_t>(
                            index.vertex_index
                        ) * 3;

                    vertex.position =
                    {
                        attrib.vertices[
                            base + 0
                        ],

                        attrib.vertices[
                            base + 1
                        ],

                        attrib.vertices[
                            base + 2
                        ]
                    };
                }

                // --------------------------------------------
                // Normal
                // --------------------------------------------

                if (index.normal_index >= 0)
                {
                    size_t base =
                        static_cast<size_t>(
                            index.normal_index
                        ) * 3;

                    vertex.normal =
                    {
                        attrib.normals[
                            base + 0
                        ],

                        attrib.normals[
                            base + 1
                        ],

                        attrib.normals[
                            base + 2
                        ]
                    };
                }
                else
                {
                    normalsAvailable =
                        false;
                }

                // --------------------------------------------
                // UV
                // --------------------------------------------

                if (index.texcoord_index >= 0)
                {
                    size_t base =
                        static_cast<size_t>(
                            index.texcoord_index
                        ) * 2;

                    float u =
                        attrib.texcoords[
                            base + 0
                        ];

                    float v =
                        attrib.texcoords[
                            base + 1
                        ];

                    // OBJ usually has bottom-left texture origin.
                    // DirectX/stb texture data is top-left.
                    vertex.uv =
                    {
                        u,
                        1.0f - v
                    };
                }
                else
                {
                    vertex.uv =
                    {
                        0.0f,
                        0.0f
                    };
                }

                triangle[corner] =
                    vertex;

                // --------------------------------------------
                // Update model bounds
                // --------------------------------------------

                m_boundsMin.x =
                    (std::min)(
                        m_boundsMin.x,
                        vertex.position.x
                    );

                m_boundsMin.y =
                    (std::min)(
                        m_boundsMin.y,
                        vertex.position.y
                    );

                m_boundsMin.z =
                    (std::min)(
                        m_boundsMin.z,
                        vertex.position.z
                    );

                m_boundsMax.x =
                    (std::max)(
                        m_boundsMax.x,
                        vertex.position.x
                    );

                m_boundsMax.y =
                    (std::max)(
                        m_boundsMax.y,
                        vertex.position.y
                    );

                m_boundsMax.z =
                    (std::max)(
                        m_boundsMax.z,
                        vertex.position.z
                    );

                m_hasBounds =
                    true;
            }

            indexOffset +=
                3;

            // ------------------------------------------------
            // Generate face normal if OBJ has none
            // ------------------------------------------------

            if (!normalsAvailable)
            {
                XMFLOAT3 normal =
                    CalculateFaceNormal(
                        triangle[0].position,
                        triangle[1].position,
                        triangle[2].position
                    );

                triangle[0].normal =
                    normal;

                triangle[1].normal =
                    normal;

                triangle[2].normal =
                    normal;
            }

            // ------------------------------------------------
            // Tangent calculation for normal mapping
            // ------------------------------------------------

            XMFLOAT3 edge1 =
                Subtract(
                    triangle[1].position,
                    triangle[0].position
                );

            XMFLOAT3 edge2 =
                Subtract(
                    triangle[2].position,
                    triangle[0].position
                );

            XMFLOAT2 deltaUV1 =
                Subtract(
                    triangle[1].uv,
                    triangle[0].uv
                );

            XMFLOAT2 deltaUV2 =
                Subtract(
                    triangle[2].uv,
                    triangle[0].uv
                );

            float denominator =
                deltaUV1.x *
                deltaUV2.y
                -
                deltaUV1.y *
                deltaUV2.x;

            XMFLOAT4 tangent{};

            if (fabsf(denominator) >
                0.000001f)
            {
                float r =
                    1.0f /
                    denominator;

                XMFLOAT3 tangent3
                {
                    (
                        edge1.x *
                        deltaUV2.y
                        -
                        edge2.x *
                        deltaUV1.y
                    ) * r,

                    (
                        edge1.y *
                        deltaUV2.y
                        -
                        edge2.y *
                        deltaUV1.y
                    ) * r,

                    (
                        edge1.z *
                        deltaUV2.y
                        -
                        edge2.z *
                        deltaUV1.y
                    ) * r
                };

                XMFLOAT3 bitangent3
                {
                    (
                        edge2.x *
                        deltaUV1.x
                        -
                        edge1.x *
                        deltaUV2.x
                    ) * r,

                    (
                        edge2.y *
                        deltaUV1.x
                        -
                        edge1.y *
                        deltaUV2.x
                    ) * r,

                    (
                        edge2.z *
                        deltaUV1.x
                        -
                        edge1.z *
                        deltaUV2.x
                    ) * r
                };

                XMVECTOR tangentVector =
                    XMVector3Normalize(
                        XMLoadFloat3(
                            &tangent3
                        )
                    );

                XMVECTOR bitangentVector =
                    XMVector3Normalize(
                        XMLoadFloat3(
                            &bitangent3
                        )
                    );

                XMVECTOR normalVector =
                    XMVector3Normalize(
                        XMLoadFloat3(
                            &triangle[0].normal
                        )
                    );

                XMVECTOR crossNT =
                    XMVector3Cross(
                        normalVector,
                        tangentVector
                    );

                float handedness =
                    XMVectorGetX(
                        XMVector3Dot(
                            crossNT,
                            bitangentVector
                        )
                    ) < 0.0f
                    ? -1.0f
                    : 1.0f;

                XMFLOAT3 tangentFloat{};

                XMStoreFloat3(
                    &tangentFloat,
                    tangentVector
                );

                tangent =
                {
                    tangentFloat.x,
                    tangentFloat.y,
                    tangentFloat.z,
                    handedness
                };
            }
            else
            {
                tangent =
                    CreateFallbackTangent(
                        triangle[0].normal
                    );
            }

            triangle[0].tangent =
                tangent;

            triangle[1].tangent =
                tangent;

            triangle[2].tangent =
                tangent;

            // ------------------------------------------------
            // Add triangle
            // ------------------------------------------------

            uint32_t first =
                static_cast<uint32_t>(
                    vertices.size()
                );

            vertices.push_back(
                triangle[0]
            );

            vertices.push_back(
                triangle[1]
            );

            vertices.push_back(
                triangle[2]
            );

            indices.push_back(
                first + 0
            );

            indices.push_back(
                first + 1
            );

            indices.push_back(
                first + 2
            );
        }
    }

    if (vertices.empty() ||
        indices.empty())
    {
        error =
            "OBJ werd geladen, maar bevat geen bruikbare triangles.";

        return false;
    }

    // ========================================================
    // Vertex buffer
    // ========================================================

    D3D11_BUFFER_DESC vertexBufferDesc{};

    vertexBufferDesc.ByteWidth =
        static_cast<UINT>(
            vertices.size() *
            sizeof(Vertex)
        );

    vertexBufferDesc.Usage =
        D3D11_USAGE_DEFAULT;

    vertexBufferDesc.BindFlags =
        D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vertexData{};

    vertexData.pSysMem =
        vertices.data();

    HRESULT hr =
        device->CreateBuffer(
            &vertexBufferDesc,
            &vertexData,
            &m_vertexBuffer
        );

    if (FAILED(hr))
    {
        error =
            "OBJ vertex buffer kon niet worden gemaakt.";

        return false;
    }

    // ========================================================
    // Index buffer
    // ========================================================

    D3D11_BUFFER_DESC indexBufferDesc{};

    indexBufferDesc.ByteWidth =
        static_cast<UINT>(
            indices.size() *
            sizeof(uint32_t)
        );

    indexBufferDesc.Usage =
        D3D11_USAGE_DEFAULT;

    indexBufferDesc.BindFlags =
        D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA indexData{};

    indexData.pSysMem =
        indices.data();

    hr =
        device->CreateBuffer(
            &indexBufferDesc,
            &indexData,
            &m_indexBuffer
        );

    if (FAILED(hr))
    {
        error =
            "OBJ index buffer kon niet worden gemaakt.";

        return false;
    }

    m_indexCount =
        static_cast<UINT>(
            indices.size()
        );

    // ========================================================
    // Load textures
    // ========================================================

    if (!diffuseFilename.empty())
    {
        if (!LoadTexture(
            device,
            diffuseFilename,
            true,
            &m_diffuseTexture,
            error))
        {
            return false;
        }
    }

    if (!normalFilename.empty())
    {
        if (!LoadTexture(
            device,
            normalFilename,
            false,
            &m_normalTexture,
            error))
        {
            return false;
        }
    }

    if (!specularFilename.empty())
    {
        if (!LoadTexture(
            device,
            specularFilename,
            false,
            &m_specularTexture,
            error))
        {
            return false;
        }
    }

    m_loaded =
        true;

    return true;
}

// ============================================================
// Normalize model size
// ============================================================

XMMATRIX ObjModel::MakeNormalizedTransform(
    float targetSize) const
{
    if (!m_hasBounds)
    {
        return
            XMMatrixIdentity();
    }

    XMFLOAT3 center
    {
        (
            m_boundsMin.x +
            m_boundsMax.x
        ) * 0.5f,

        (
            m_boundsMin.y +
            m_boundsMax.y
        ) * 0.5f,

        (
            m_boundsMin.z +
            m_boundsMax.z
        ) * 0.5f
    };

    float sizeX =
        m_boundsMax.x -
        m_boundsMin.x;

    float sizeY =
        m_boundsMax.y -
        m_boundsMin.y;

    float sizeZ =
        m_boundsMax.z -
        m_boundsMin.z;

    float largest =
        (std::max)(
            sizeX,
            (std::max)(
                sizeY,
                sizeZ
            )
        );

    if (largest < 0.0001f)
    {
        largest =
            1.0f;
    }

    float scale =
        targetSize /
        largest;

    return
        XMMatrixTranslation(
            -center.x,
            -center.y,
            -center.z
        )
        *
        XMMatrixScaling(
            scale,
            scale,
            scale
        );
}

// ============================================================
// Render
// ============================================================

void ObjModel::Render(
    ID3D11DeviceContext* context,
    const XMMATRIX& world,
    const XMMATRIX& view,
    const XMMATRIX& projection,
    const XMFLOAT3& cameraPosition)
{
    if (!m_loaded ||
        !context)
    {
        return;
    }

    // ========================================================
    // Pipeline
    // ========================================================

    context->IASetInputLayout(
        m_inputLayout
    );

    context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
    );

    context->VSSetShader(
        m_vertexShader,
        nullptr,
        0
    );

    context->PSSetShader(
        m_pixelShader,
        nullptr,
        0
    );

    context->VSSetConstantBuffers(
        0,
        1,
        &m_constantBuffer
    );

    context->PSSetConstantBuffers(
        0,
        1,
        &m_constantBuffer
    );

    // ========================================================
    // Geometry
    // ========================================================

    UINT stride =
        sizeof(Vertex);

    UINT offset =
        0;

    context->IASetVertexBuffers(
        0,
        1,
        &m_vertexBuffer,
        &stride,
        &offset
    );

    context->IASetIndexBuffer(
        m_indexBuffer,
        DXGI_FORMAT_R32_UINT,
        0
    );

    // ========================================================
    // Constant buffer
    // ========================================================

    ConstantBuffer cb{};

    cb.world =
        XMMatrixTranspose(
            world
        );

    cb.worldViewProjection =
        XMMatrixTranspose(
            world *
            view *
            projection
        );

    cb.cameraPosition =
    {
        cameraPosition.x,
        cameraPosition.y,
        cameraPosition.z,
        1.0f
    };

    cb.lightDirection =
    {
        -0.35f,
        -0.80f,
        -0.45f,
        0.0f
    };

    cb.material =
    {
        m_diffuseTexture
            ? 1.0f
            : 0.0f,

        m_normalTexture
            ? 1.0f
            : 0.0f,

        m_specularTexture
            ? 1.0f
            : 0.0f,

        32.0f
    };

    context->UpdateSubresource(
        m_constantBuffer,
        0,
        nullptr,
        &cb,
        0,
        0
    );

    // ========================================================
    // Textures
    // ========================================================

    ID3D11ShaderResourceView* textures[3] =
    {
        m_diffuseTexture,
        m_normalTexture,
        m_specularTexture
    };

    context->PSSetShaderResources(
        0,
        3,
        textures
    );

    context->PSSetSamplers(
        0,
        1,
        &m_sampler
    );

    // ========================================================
    // Draw
    // ========================================================

    context->DrawIndexed(
        m_indexCount,
        0,
        0
    );

    // ========================================================
    // Unbind textures
    // ========================================================

    ID3D11ShaderResourceView* nullTextures[3] =
    {
        nullptr,
        nullptr,
        nullptr
    };

    context->PSSetShaderResources(
        0,
        3,
        nullTextures
    );
}