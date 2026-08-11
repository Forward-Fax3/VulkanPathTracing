//
// Created by forwardfax3 on 15/03/2026.
//
#pragma once
#include <memory>

#include "Layer.hpp"
#include "BaseShader.hpp"
#include "Renderer.hpp"
#include "UniformBuffer.hpp"
#include "InterLayerData.hpp"
#include "BaseGPUScene.hpp"


namespace OWC
{
    class GPURayTracerRenderer : public Layer
    {
    private: // structs
        struct UniformBufferObject
        {
            f32 divider = 0.0f;
            f32 invGammaValue = 0.0f;
        };

        struct GeneralGPUData
        {
            Mat4 InvProjection = 0.0f;
            Mat4 InvViewMatrix = 0.0f;
            u32 randSeed = 0;
            u32 stratifiedGridPosition = 0;
            u32 GPURefreshScreen = false;
            u32 stratifiedGridSize = 0;
        };

    public:
        GPURayTracerRenderer();
        ~GPURayTracerRenderer() override = default;
        GPURayTracerRenderer(const GPURayTracerRenderer&) = delete;
        GPURayTracerRenderer& operator=(const GPURayTracerRenderer&) = delete;
        GPURayTracerRenderer(GPURayTracerRenderer&&) = delete;
        GPURayTracerRenderer& operator=(GPURayTracerRenderer&&) = delete;

        void OnUpdate() override;
        void ImGuiRender() override;
        void OnEvent(BaseEvent&) override;

    private: // methods
        void SetUpRenderImage();
        void SetupRenderPass();
        void SetupPipeline();
        void CalculateCamera(const Vec3& movement = Vec3(0.0f));

    private: // attributes
        std::unique_ptr<Graphics::BaseShader> m_RayTracingShader = nullptr;
        std::unique_ptr<Graphics::BaseShader> m_DisplayShader = nullptr;
        std::shared_ptr<Graphics::RenderPassData> m_RayTracingRenderPass = nullptr;
        std::shared_ptr<Graphics::RenderPassData> m_DisplayRenderPass = nullptr;
        std::shared_ptr<Graphics::RenderPassData> m_ImageReleaseRenderPass = nullptr;
        std::shared_ptr<Graphics::UniformBuffer> m_UniformBuffer = nullptr;
        std::shared_ptr<Graphics::TextureBuffer> m_RenderTarget = nullptr;
        std::shared_ptr<Graphics::UniformBuffer> m_RayTracingGPUDataBuffer = nullptr;
        uSize m_NumberOfSamples = 0;

        float m_HFOV = 140.0f;
        Vec3 m_CameraPosition = Vec3(0.0f, 1.5f, 0.0f);
        Vec3 m_CameraRotation = Vec3(0.0f);
        float m_MoveSpeed = 1.0f;
        i32 m_NumberOfBounces = 8;
        u32 m_RayTracingWidth = 0;
        u32 m_RayTracingHeight = 0;
        u32 m_StratifiedGridSize = 8;
        u32 m_StratifiedPosition = 0;
        i32 m_SceneIndex = 0;
        u8 m_CameraNeedsRefreshing = false;
        bool m_ScreenNeedsRefreshing = false;

        bool m_UseWindowResolution = true;

        Vec3 m_KeyPressedOnVec3 = Vec3(0.0f);

        std::shared_ptr<BaseGPUScene> m_Scene = nullptr;
    };
} // OWC
