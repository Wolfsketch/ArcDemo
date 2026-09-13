#pragma once

#include "Renderer.h"

class Terrain
{
public:
    Terrain() = default;
    ~Terrain();

    bool Initialize(
        Renderer& renderer,
        float size,
        int resolution,
        std::string& error);

    void Render(
        Renderer& renderer,
        const DirectX::XMMATRIX& view,
        const DirectX::XMMATRIX& projection,
        const DirectX::XMFLOAT3& cameraPosition);

    float HeightAt(float x, float z) const;

private:
    float EvaluateHeight(float x, float z) const;

private:
    StaticMesh m_mesh;
    float m_size = 220.0f;
    int m_resolution = 129;
};
