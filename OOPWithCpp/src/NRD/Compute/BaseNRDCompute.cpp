//
// Created by forwardfax3 on 21/11/2026.
//

#include "BaseNRDCompute.hpp"
#include "Renderer.hpp"
#include <expected>
#include "VulkanNRDCompute.hpp"

namespace OWC::Compute
{
    std::shared_ptr<BaseNRDCompute> BaseNRDCompute::CreateNRDComputePass()
    {
        using namespace Graphics;
        switch (Renderer::GetAPI())
        {
        case Graphics::RendererAPI::Vulkan:
            return std::make_shared<VulkanNRDCompute>();
        default:
            std::unreachable();
        }
    }
} // OWC::Compute