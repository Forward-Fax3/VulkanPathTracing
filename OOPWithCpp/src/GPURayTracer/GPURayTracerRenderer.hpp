//
// Created by forwardfax3 on 15/03/2026.
//
#pragma once
#include <memory>

#include "Layer.hpp"
#include "BaseShader.hpp"
#include "Renderer.hpp"
#include "GPUBuffers.hpp"
#include "InterLayerData.hpp"
#include "BaseGPUScene.hpp"
#include "NRDImpl.hpp"


namespace OWC
{
    class GPURayTracerRenderer : public Layer
    {
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
        void SetSigmaLightDirection() const;
        void CalculateCamera(const Vec3& movement = Vec3(0.0f));

    private: // structs
        struct UniformBufferObject
        {
            f32 denoisingRange = 0.0f;
            u32 validationEnable = 0;
            u32 SigmaOn = 0;
            u32 fullscreenPanel = 0xFFFFFFFF;
        };

        struct GeneralGPUData
        {
            Mat4 InvProjection = 0.0f;
            Mat4 InvViewMatrix = 0.0f;
            Mat4 WorldToClip = 0.0f;
            Mat4 PreWorldToClip = 0.0f;
            Mat4 WorldToView = 0.0f;
            Mat4 PreWorldToView = 0.0f;
            u32 randSeed = 0;
            u32 frameIndex = 0;
            Vec2f32 camJit = {};
            f32 tanSunAngularRadius = 0.0f;
            f32 cosSunAngularRadius = 0.0f;
        };

    private: // attributes
        std::unique_ptr<Graphics::BaseShader> m_RayTracingShader = nullptr;
        std::unique_ptr<Graphics::BaseShader> m_DisplayShader = nullptr;
        std::shared_ptr<Graphics::RenderPassData> m_RayTracingRenderPass = nullptr;
        std::shared_ptr<Graphics::RenderPassData> m_DisplayRenderPass = nullptr;
        std::shared_ptr<Graphics::UniformBuffer> m_UniformBuffer = nullptr;
        std::shared_ptr<Graphics::UniformBuffer> m_RayTracingGPUDataBuffer = nullptr;

        std::unordered_map<nrd::ResourceType, std::shared_ptr<Graphics::TextureImage>> m_NRDInputsAndOutputs{};
        std::vector<std::shared_ptr<Graphics::TextureImage>> m_DemodulatedTextures{};

        Vec3 m_CameraPosition = Vec3(1.0f, 0.2f, 0.0f);
        Vec3 m_CameraRotation = Vec3(0.0f, -90.0f, 0.0f);
        Vec2u32 m_RayTracingResolution = { 0, 0 };

        float m_HFOV = 60.0f;
        float m_MoveSpeed = 1.0f;
        i32 m_NumberOfBounces = 8;
        u32 m_StratifiedPosition = 0;
        i32 m_SceneIndex = 0;
        u32 m_NumberOfSamples = 1;

        Mat4 m_ViewToClip{};
        Mat4 m_PreViewToClip{};
        Mat4 m_WorldToView{};
        Mat4 m_PreWorldToView{};

        Vec3 m_KeyPressedOnVec3 = Vec3(0.0f);

        Vec2 m_CamJit = {};
        Vec2 m_PreCamJit = {};

        std::shared_ptr<BaseGPUScene> m_Scene = nullptr;

        std::unique_ptr<NRDImpl> m_NRDImpl{};

        u32 m_FullscreenPanel = 0xFFFFFFFF;

        u32 m_FrameIndex = 0;

        bool m_ScreenNeedsRefreshing = false;

        bool m_ValidationOn = false;

        bool m_UseWindowResolution = true;
        bool m_FirstFrame = true;
    };
} // OWC
