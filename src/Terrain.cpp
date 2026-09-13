#include "Terrain.h"

#include <cmath>
#include <vector>

using namespace DirectX;

Terrain::~Terrain()
{
    Renderer::ReleaseMesh(m_mesh);
}

float Terrain::EvaluateHeight(float x, float z) const
{
    // Soft rolling desert dunes. Deliberately simple for now.
    float h =
        sinf(x * 0.032f) * cosf(z * 0.026f) * 1.00f +
        sinf((x + z) * 0.050f) * 0.42f +
        cosf((x * 0.018f) - (z * 0.041f)) * 0.26f -
        0.26f;

    return h;
}

float Terrain::HeightAt(float x, float z) const
{
    return EvaluateHeight(x, z);
}

bool Terrain::Initialize(
    Renderer& renderer,
    float size,
    int resolution,
    std::string& error)
{
    m_size = size;
    m_resolution = resolution;

    if (m_resolution < 2)
    {
        error = "Terrain resolution moet minstens 2 zijn.";
        return false;
    }

    std::vector<WorldVertex> vertices;
    std::vector<uint32_t> indices;

    vertices.resize(
        static_cast<size_t>(m_resolution) *
        static_cast<size_t>(m_resolution));

    const float half = m_size * 0.5f;
    const float step = m_size / static_cast<float>(m_resolution - 1);
    const float normalSample = 0.35f;

    for (int zIndex = 0; zIndex < m_resolution; ++zIndex)
    {
        for (int xIndex = 0; xIndex < m_resolution; ++xIndex)
        {
            float x = -half + xIndex * step;
            float z = -half + zIndex * step;
            float y = EvaluateHeight(x, z);

            float hL = EvaluateHeight(x - normalSample, z);
            float hR = EvaluateHeight(x + normalSample, z);
            float hD = EvaluateHeight(x, z - normalSample);
            float hU = EvaluateHeight(x, z + normalSample);

            XMVECTOR normalVector = XMVector3Normalize(
                XMVectorSet(
                    hL - hR,
                    normalSample * 2.0f,
                    hD - hU,
                    0.0f));

            XMFLOAT3 normal{};
            XMStoreFloat3(&normal, normalVector);

            float variation = 0.035f * sinf(x * 0.11f + z * 0.07f);

            XMFLOAT3 sandColor
            {
                0.57f + variation,
                0.45f + variation * 0.75f,
                0.29f + variation * 0.45f
            };

            size_t index =
                static_cast<size_t>(zIndex) * m_resolution +
                static_cast<size_t>(xIndex);

            vertices[index] =
            {
                {x, y, z},
                normal,
                sandColor
            };
        }
    }

    for (int zIndex = 0; zIndex < m_resolution - 1; ++zIndex)
    {
        for (int xIndex = 0; xIndex < m_resolution - 1; ++xIndex)
        {
            uint32_t i0 = static_cast<uint32_t>(
                zIndex * m_resolution + xIndex);
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + static_cast<uint32_t>(m_resolution);
            uint32_t i3 = i2 + 1;

            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);

            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }

    return renderer.CreateStaticMesh(
        vertices.data(),
        static_cast<UINT>(vertices.size()),
        indices.data(),
        static_cast<UINT>(indices.size()),
        m_mesh,
        error);
}

void Terrain::Render(
    Renderer& renderer,
    const XMMATRIX& view,
    const XMMATRIX& projection,
    const XMFLOAT3& cameraPosition)
{
    renderer.DrawMesh(
        m_mesh,
        XMMatrixIdentity(),
        view,
        projection,
        {1,1,1,1},
        cameraPosition,
        true);
}
