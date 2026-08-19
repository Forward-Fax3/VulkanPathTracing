#pragma once
#include "Core.hpp"
#include <string_view>
#include <vector>
#include <span>

#include <glm/glm.hpp>


namespace OWC
{
	template <typename Type, uSize elementSize, glm::qualifier Qualifier>
	class ImageLoader
	{
	public:
		ImageLoader() = default;
		explicit ImageLoader(std::nullptr_t) {}
		explicit ImageLoader(std::string_view path);
		explicit ImageLoader(std::span<std::byte> data);
		virtual ~ImageLoader() = default;

		ImageLoader& operator=(std::nullptr_t) { m_ImageData.clear(); m_Width = 0; m_Height = 0; return *this; }

		ImageLoader(const ImageLoader&) = delete;
		ImageLoader& operator=(const ImageLoader&) = delete;
		ImageLoader(ImageLoader&&) = default;
		ImageLoader& operator=(ImageLoader&&) = default;

		[[nodiscard]] const std::vector<glm::vec<elementSize, Type, Qualifier>>& GetImageData() const { return m_ImageData; }
		[[nodiscard]] const glm::vec<elementSize, Type, Qualifier>& GetPixel(const uSize x, const uSize y) const { return m_ImageData[y * m_Width + x]; }
		// expects x and y to be in the range [0.0, 1.0]
		[[nodiscard]] const glm::vec<elementSize, Type, Qualifier>& GetPixel(const f32 x, const f32 y) const { return m_ImageData[static_cast<uSize>(y * static_cast<f32>(m_Height)) * m_Width + static_cast<uSize>(x * static_cast<f32>(m_Width))]; }
		[[nodiscard]] uSize GetWidth() const { return m_Width; }
		[[nodiscard]] uSize GetHeight() const { return m_Height; }

	private:
		std::vector<glm::vec<elementSize, Type, Qualifier>> m_ImageData;
		uSize m_Width = 0;
		uSize m_Height = 0;
	};
}

#include "ImageLoader.inl"
 