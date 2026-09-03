#pragma once
#include "Core.hpp"
#include <vector>
#include <cstdint>
#include <bitset>
#include <glm/vec4.hpp>


namespace OWC
{
	struct InterLayerData
	{
		std::vector<Vec4> imageData{};
		uSize numberOfSamples = 0;
		Vec2u imageScreenSize{ 0, 0 };
		std::bitset<2> ImageUpdates; // bit 0: update image, bit 1: image resize

		template<typename T>
		OWC_FORCE_INLINE T GetNumberOfPixels() const requires (std::is_integral_v<T> || std::is_floating_point_v<T>)
			{ return static_cast<T>(static_cast<uSize>(imageScreenSize.x) * static_cast<uSize>(imageScreenSize.y)); }

		OWC_FORCE_INLINE u32 GetNumberOfPixels() const { return GetNumberOfPixels<u32>(); }
	};
}
