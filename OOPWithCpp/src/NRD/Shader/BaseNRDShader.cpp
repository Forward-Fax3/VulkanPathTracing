//
// Created by forwardfax3 on 19/11/2026.
//

#include "BaseNRDShader.hpp"
#include "VulkanNRDShader.hpp"


namespace OWC::Graphics
{
    std::shared_ptr<BaseNRDShader> BaseNRDShader::CreateNRDShader(const nrd::InstanceDesc& instanceDesc)
    {
        return std::make_shared<VulkanNRDShader>(instanceDesc);
    }
}
