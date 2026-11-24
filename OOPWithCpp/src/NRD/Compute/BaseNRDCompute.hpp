//
// Created by forwardfax3 on 21/11/2026.
//

#pragma once
#include "NRD.h"
#include "BaseNRDShader.hpp"
#include "GPUBuffers.hpp"
#include <span>

namespace OWC::Compute
{
    class BaseNRDCompute
    {
    public:
        virtual ~BaseNRDCompute() = default;
        virtual void NRDDenoiserPass(const std::span<const nrd::DispatchDesc>& dispatchDescs, const std::shared_ptr<const Graphics::BaseNRDShader>& shaders, const std::shared_ptr<Graphics::HostVisibleUniformBuffer>& constantUniformBuffer, const std::vector<std::shared_ptr<Graphics::TextureImage>>& textureImages, const std::vector<std::shared_ptr<Graphics::TextureImage>>& transientTextureImages, const std::unordered_map<nrd::ResourceType, std::shared_ptr<Graphics::TextureImage>>& userImages, const std::span<const std::string_view>& waitSemaphoreNames, const std::span<const std::string_view>& startSemaphoreNames, const std::span<const std::string_view>& waitPreviousFrameSemaphoreNames) = 0;


        static std::shared_ptr<BaseNRDCompute> CreateNRDComputePass();

    protected:
        BaseNRDCompute() = default;
    };
} // OWC::Compute
