//
// Created by forwardfax3 on 19/11/2026.
//

#pragma once
#include "Core.hpp"

#include "GPUBuffers.hpp"

#include "NRD.h"
#include "NRDDescs.h"

#include <memory>
#include <string_view>
#include <span>


namespace OWC::Graphics
{
	constexpr u32 g_RangeTextures = 0;
	constexpr u32 g_RangeStorage = 1;

	struct NRDShaderData
	{
		const std::span<const nrd::PipelineDesc> pipelines;
		const std::string_view entryPoint;
		const decltype(nrd::DescriptorPoolDesc::perSetTexturesMaxNum) perSetTexturesMaxNum;
		const decltype(nrd::DescriptorPoolDesc::perSetStorageTexturesMaxNum) perSetStorageTexturesMaxNum;
	};

	class BaseNRDShader
	{
	public:
		virtual ~BaseNRDShader() = default;

		virtual void BindHostVisibleUniformBuffer(const nrd::InstanceDesc& instanceDesc, const std::shared_ptr<const Graphics::HostVisibleUniformBuffer>& constantBuffer) = 0;

		static std::shared_ptr<BaseNRDShader> CreateNRDShader(const nrd::InstanceDesc& instanceDesc);

	protected:
		BaseNRDShader() = default;
	};
}
