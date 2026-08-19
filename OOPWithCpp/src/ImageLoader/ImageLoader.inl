#pragma once
#include "Core.hpp"
#include "ImageLoader.hpp"
#include "Log.hpp"

#include <stb_image.h>

#include <memory>
#include <limits>


namespace OWC
{
	template <typename Type, uSize elementSize, glm::qualifier Qualifier>
	inline ImageLoader<Type, elementSize, Qualifier>::ImageLoader(std::string_view path)
	{
		//static_assert(elementSize == 3 || elementSize == 4, "Only 3 or 4 channel images are supported.");

		i32 tempWidth = 0;
		i32 tempHeight = 0;
		constexpr bool is8BitInt = std::is_same_v<Type, u8> || std::is_same_v<Type, i8>;
		using ReturnType = std::conditional_t<is8BitInt, u8, f32>;
		std::unique_ptr<ReturnType[], decltype([](ReturnType ptr[]) { stbi_image_free(ptr); })> imageDataPtr;

		if constexpr (is8BitInt)
			imageDataPtr.reset(stbi_load(path.data(), &tempWidth, &tempHeight, nullptr, elementSize));
		else
			imageDataPtr.reset(stbi_loadf(path.data(), &tempWidth, &tempHeight, nullptr, elementSize));

		if (!imageDataPtr)
		{
			std::string pathCopy(path);

			for (char& c : pathCopy)
				if (c == '\\')
					c = '/';

			if constexpr (is8BitInt)
				imageDataPtr.reset(stbi_load(pathCopy.data(), &tempWidth, &tempHeight, nullptr, elementSize));
			else
				imageDataPtr.reset(stbi_loadf(pathCopy.data(), &tempWidth, &tempHeight, nullptr, elementSize));

			if (!imageDataPtr)
				for (uSize i = 0; i < 16; i++)
				{
					pathCopy = "../" + pathCopy;
					if constexpr (is8BitInt)
						imageDataPtr.reset(stbi_load(pathCopy.data(), &tempWidth, &tempHeight, nullptr, elementSize));
					else
						imageDataPtr.reset(stbi_loadf(pathCopy.data(), &tempWidth, &tempHeight, nullptr, elementSize));

					if (imageDataPtr)
						break;
				}

			if (!imageDataPtr)
			{
				Log<LogLevel::Error>("Failed to load image from path: {}", path);
				return;
			}
		}

		m_Width = static_cast<uSize>(tempWidth);
		m_Height = static_cast<uSize>(tempHeight);

		if constexpr ((is8BitInt || std::is_same_v<Type, f32>) && (elementSize == 4 || !glm::detail::is_aligned<Qualifier>::value))
		{ // no conversion needed so just use memcpy for speed
			m_ImageData.resize(m_Width * m_Height);
			std::memcpy(m_ImageData.data(), imageDataPtr.get(), m_Height * m_Width * elementSize * sizeof(Type));
		}
		else if constexpr (std::is_same_v<Type, f64>)
		{
			m_ImageData.reserve(m_Width * m_Height);
			for (uSize i = 0; i < m_Height * m_Width * elementSize; i += elementSize)
				if constexpr (elementSize == 3)
					m_ImageData.emplace_back(
						static_cast<f64>(imageDataPtr[i + 0]),	// R
						static_cast<f64>(imageDataPtr[i + 1]),	// G
						static_cast<f64>(imageDataPtr[i + 2])	// B
					);
				else
					m_ImageData.emplace_back(
						static_cast<f64>(imageDataPtr[i + 0]),	// R
						static_cast<f64>(imageDataPtr[i + 1]),	// G
						static_cast<f64>(imageDataPtr[i + 2]),	// B
						static_cast<f64>(imageDataPtr[i + 3])	// A
					);
		}
		else if constexpr (std::is_integral_v<Type>)
		{
			using IntermediateType = std::conditional_t<is8BitInt, i16, f32>;
			m_ImageData.reserve(m_Width * m_Height);
			for (uSize i = 0; i < m_Height * m_Width * elementSize; i += elementSize)
				if constexpr (elementSize == 3)
					m_ImageData.emplace_back(
						static_cast<Type>((imageDataPtr[i + 0] * (static_cast<IntermediateType>(std::numeric_limits<Type>::max()) - static_cast<IntermediateType>(std::numeric_limits<Type>::min()))) + static_cast<IntermediateType>(std::numeric_limits<Type>::min())),	// R
						static_cast<Type>((imageDataPtr[i + 1] * (static_cast<IntermediateType>(std::numeric_limits<Type>::max()) - static_cast<IntermediateType>(std::numeric_limits<Type>::min()))) + static_cast<IntermediateType>(std::numeric_limits<Type>::min())),	// G
						static_cast<Type>((imageDataPtr[i + 2] * (static_cast<IntermediateType>(std::numeric_limits<Type>::max()) - static_cast<IntermediateType>(std::numeric_limits<Type>::min()))) + static_cast<IntermediateType>(std::numeric_limits<Type>::min()))	// B
					);
				else
					m_ImageData.emplace_back(
						static_cast<Type>((imageDataPtr[i + 0] * (static_cast<IntermediateType>(std::numeric_limits<Type>::max()) - static_cast<IntermediateType>(std::numeric_limits<Type>::min()))) + static_cast<IntermediateType>(std::numeric_limits<Type>::min())),	// R
						static_cast<Type>((imageDataPtr[i + 1] * (static_cast<IntermediateType>(std::numeric_limits<Type>::max()) - static_cast<IntermediateType>(std::numeric_limits<Type>::min()))) + static_cast<IntermediateType>(std::numeric_limits<Type>::min())),	// G
						static_cast<Type>((imageDataPtr[i + 2] * (static_cast<IntermediateType>(std::numeric_limits<Type>::max()) - static_cast<IntermediateType>(std::numeric_limits<Type>::min()))) + static_cast<IntermediateType>(std::numeric_limits<Type>::min())),	// B
						static_cast<Type>((imageDataPtr[i + 3] * (static_cast<IntermediateType>(std::numeric_limits<Type>::max()) - static_cast<IntermediateType>(std::numeric_limits<Type>::min()))) + static_cast<IntermediateType>(std::numeric_limits<Type>::min()))	// A
					);
		}
		else
		{
			OWCDebugBreak();
			std::unreachable();
		}
	}

	template <typename Type, uSize elementSize, glm::qualifier Qualifier>
	ImageLoader<Type, elementSize, Qualifier>::ImageLoader(std::span<std::byte> data)
	{
		//static_assert(elementSize == 3 || elementSize == 4, "Only 3 or 4 channel images are supported.");

		i32 tempWidth = 0;
		i32 tempHeight = 0;
		constexpr bool is8BitInt = std::is_same_v<Type, u8> || std::is_same_v<Type, i8>;
		using ReturnType = std::conditional_t<is8BitInt, u8, f32>;
		std::unique_ptr<ReturnType[], decltype([](ReturnType ptr[]) { stbi_image_free(ptr); })> imageDataPtr;

		if constexpr (is8BitInt)
			imageDataPtr.reset(stbi_load_from_memory(std::bit_cast<stbi_uc*>(data.data()), data.size(), &tempWidth, &tempHeight, nullptr, elementSize));
		else
			imageDataPtr.reset(stbi_loadf_from_memory(std::bit_cast<stbi_uc*>(data.data()), data.size(), &tempWidth, &tempHeight, nullptr, elementSize));

		if (!imageDataPtr)
		{
			Log<LogLevel::Error>("Failed to load image from data");
			return;
		}

		m_Width = static_cast<uSize>(tempWidth);
		m_Height = static_cast<uSize>(tempHeight);

		if constexpr ((is8BitInt || std::is_same_v<Type, f32>) && (elementSize == 4 || !glm::detail::is_aligned<Qualifier>::value))
		{ // no conversion needed so just use memcpy for speed
			m_ImageData.resize(m_Width * m_Height);
			std::memcpy(m_ImageData.data(), imageDataPtr.get(), m_Height * m_Width * elementSize * sizeof(Type));
		}
		else if constexpr (std::is_same_v<Type, f64>)
		{
			m_ImageData.reserve(m_Width * m_Height);
			for (uSize i = 0; i < m_Height * m_Width * elementSize; i += elementSize)
				if constexpr (elementSize == 3)
					m_ImageData.emplace_back(
						static_cast<f64>(imageDataPtr[i + 0]),	// R
						static_cast<f64>(imageDataPtr[i + 1]),	// G
						static_cast<f64>(imageDataPtr[i + 2])	// B
					);
				else
					m_ImageData.emplace_back(
						static_cast<f64>(imageDataPtr[i + 0]),	// R
						static_cast<f64>(imageDataPtr[i + 1]),	// G
						static_cast<f64>(imageDataPtr[i + 2]),	// B
						static_cast<f64>(imageDataPtr[i + 3])	// A
					);
		}
		else if constexpr (std::is_integral_v<Type>)
		{
			using IntermediateType = std::conditional_t<is8BitInt, i16, f32>;
			m_ImageData.reserve(m_Width * m_Height);
			for (uSize i = 0; i < m_Height * m_Width * elementSize; i += elementSize)
				if constexpr (elementSize == 3)
					m_ImageData.emplace_back(
						static_cast<Type>((imageDataPtr[i + 0] * (static_cast<IntermediateType>(std::numeric_limits<Type>::max()) - static_cast<IntermediateType>(std::numeric_limits<Type>::min()))) + static_cast<IntermediateType>(std::numeric_limits<Type>::min())),	// R
						static_cast<Type>((imageDataPtr[i + 1] * (static_cast<IntermediateType>(std::numeric_limits<Type>::max()) - static_cast<IntermediateType>(std::numeric_limits<Type>::min()))) + static_cast<IntermediateType>(std::numeric_limits<Type>::min())),	// G
						static_cast<Type>((imageDataPtr[i + 2] * (static_cast<IntermediateType>(std::numeric_limits<Type>::max()) - static_cast<IntermediateType>(std::numeric_limits<Type>::min()))) + static_cast<IntermediateType>(std::numeric_limits<Type>::min()))	// B
					);
				else
					m_ImageData.emplace_back(
						static_cast<Type>((imageDataPtr[i + 0] * (static_cast<IntermediateType>(std::numeric_limits<Type>::max()) - static_cast<IntermediateType>(std::numeric_limits<Type>::min()))) + static_cast<IntermediateType>(std::numeric_limits<Type>::min())),	// R
						static_cast<Type>((imageDataPtr[i + 1] * (static_cast<IntermediateType>(std::numeric_limits<Type>::max()) - static_cast<IntermediateType>(std::numeric_limits<Type>::min()))) + static_cast<IntermediateType>(std::numeric_limits<Type>::min())),	// G
						static_cast<Type>((imageDataPtr[i + 2] * (static_cast<IntermediateType>(std::numeric_limits<Type>::max()) - static_cast<IntermediateType>(std::numeric_limits<Type>::min()))) + static_cast<IntermediateType>(std::numeric_limits<Type>::min())),	// B
						static_cast<Type>((imageDataPtr[i + 3] * (static_cast<IntermediateType>(std::numeric_limits<Type>::max()) - static_cast<IntermediateType>(std::numeric_limits<Type>::min()))) + static_cast<IntermediateType>(std::numeric_limits<Type>::min()))	// A
					);
		}
		else
		{
			OWCDebugBreak();
			std::unreachable();
		}
	}
}
