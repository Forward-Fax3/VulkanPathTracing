#pragma once
#include "UniformBuffer.hpp"
#include "BaseTLAS.hpp"

#include <string>
#include <vector>
#include <span>
#include <memory>
#include <utility>


namespace OWC::Graphics
{
	enum class ShaderType : u8
	{
		Vertex = 1<<0,
		Fragment = 1<<1,
		Compute = 1<<2,

		// ray tracing
		RayGen = 1<<3,
		RayAnyHit = 1<<4,
		RayClosestHit = 1<<5,
		RayIntersect = 1<<6,
		RayMiss = 1<<7,
	};

	constexpr auto operator<=>(ShaderType lhs, ShaderType rhs)
	{
		return std::to_underlying(lhs) <=> std::to_underlying(rhs);
	}

	constexpr ShaderType operator|(ShaderType lhs, ShaderType rhs)
	{
		return static_cast<ShaderType>(std::to_underlying(lhs) | std::to_underlying(rhs));
	}

	constexpr ShaderType operator&(ShaderType lhs, ShaderType rhs)
	{
		return static_cast<ShaderType>(std::to_underlying(lhs) & std::to_underlying(rhs));
	}

	enum class DescriptorType : u8
	{
		UniformBuffer,
		Sampler,
		CombinedImageSampler,
		StorageBuffer,
		StorageImage,
		TLAS,
		SampledImage
	};

	struct BindingDescription
	{
		u32 descriptorCount;
		u32 binding;
		DescriptorType descriptorType;
		ShaderType stageFlags;
	};

	struct ShaderData
	{
		enum class ShaderLanguage : u8
		{ // for now only SPIR-V is supported
//			GLSL,
//			HLSL,
//			Slang,
			SPIRV,

			SPV = SPIRV
		};

		OWC_FORCE_INLINE std::string ShaderTypeToString() const
		{
			switch (type)
			{
			using enum OWC::Graphics::ShaderType;
			case Vertex:
				return "Vertex";
			case Fragment:
				return "Fragment";
			case Compute:
				return "Compute";
			case RayGen:
				return "RayGen";
			case RayAnyHit:
				return "RayAnyHit";
			case RayClosestHit:
				return "RayClosestHit";
			case RayIntersect:
				return "RayIntersect";
			case RayMiss:
				return "RayMiss";
			default:
				return "Unknown";
			}
		}

//		std::string source;
		std::vector<u32>& bytecode; // for SPIR-V
		
		ShaderType type;
		ShaderLanguage language;
		std::span<BindingDescription> descriptorType;

		std::string entryPoint; // optional, if left empty "main" will be assumed
	};

	class BaseShader
	{
	public:
		BaseShader() = default;
		virtual ~BaseShader() = default;
		BaseShader(BaseShader&) = default;
		BaseShader& operator=(const BaseShader&) = default;
		BaseShader(BaseShader&&) noexcept = default;
		BaseShader& operator=(BaseShader&&) noexcept = default;

		virtual void BindUniform(u32 binding, const std::shared_ptr<UniformBuffer>& uniformBuffer) = 0;
		virtual void BindTexture(u32 binding, const std::shared_ptr<TextureBuffer>& textureBuffer) = 0;
		virtual void BindDynamicTexture(u32 binding, const std::shared_ptr<DynamicTextureBuffer>& dTextureBuffer) = 0;
		virtual void BindSampler2D(u32 binding, const std::shared_ptr<TextureArraySampler>& sampler) = 0;
		virtual void BindTextureArray(u32 binding, const std::shared_ptr<TextureArray>& textureArray) = 0;

		virtual void BindTLAS(u32 binding, const std::shared_ptr<BaseTLAS>& tlasBuffer) = 0;

		static std::unique_ptr<BaseShader> CreateShader(const std::span<ShaderData>& shaderDatas);
		static std::unique_ptr<BaseShader> CreateRTShader(const std::span<ShaderData>& shaderDatas);
	};
}
