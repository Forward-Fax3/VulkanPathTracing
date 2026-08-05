#pragma once
#include <cstdint>
#include <string>
#include <bit>
#include <glm/glm.hpp>

#define OWC_FORCE_INLINE GLM_INLINE

#if defined(_MSC_VER)
	#include <intrin.h>
	#define OWC_NO_OP __nop()
#elif defined(__clang__) || defined(__GNUC__)
	#define OWC_NO_OP __asm__ __volatile__("nop\n\t")
#else
	#error "OWC_NO_OP is not defined for this compiler!"
#endif

#if defined(_WIN32) || defined(_WIN64)
	namespace OWC
	{
		OWC_FORCE_INLINE void OWCDebugBreak()
		{
			__debugbreak();
		}
	}
#elif defined(__linux__)
	#include <csignal>
	namespace OWC
	{
		OWC_FORCE_INLINE void OWCDebugBreak()
		{
//			raise(SIGTRAP);
			__builtin_trap();
		}
	}
#else
	#error "DebugBreak is not implemented for this platform!"
#endif

#if defined(_WIN32) || defined(_WIN64)
#define VECTORCALL __vectorcall
#else
// disable vectorcall on non-Windows platforms, as it is not supported by GCC or Clang
// #define VECTORCALL __attribute__((vectorcall))
#define VECTORCALL
#endif

namespace OWC
{
	using i8 = std::int8_t;
	using i16 = std::int16_t;
	using i32 = std::int32_t;
	using i64 = std::int64_t;

	using u8 = std::uint8_t;
	using u16 = std::uint16_t;
	using u32 = std::uint32_t;
	using u64 = std::uint64_t;

	using iSize = std::ptrdiff_t;
	using uSize = std::size_t;

	using f32 = float;
	using f64 = double;

	using bool32 = u32;

	using Vec2f32 = glm::vec<2, f32>;
	using Vec3f32 = glm::vec<3, f32>;
	using Vec4f32 = glm::vec<4, f32>;

	using Vec2pf32 = glm::vec<2, f32, glm::packed_highp>;
	using Vec3pf32 = glm::vec<3, f32, glm::packed_highp>;
	using Vec4pf32 = glm::vec<4, f32, glm::packed_highp>;

	using Mat2f32 = glm::mat<2, 2, f32>;
	using Mat3f32 = glm::mat<3, 3, f32>;
	using Mat4f32 = glm::mat<4, 4, f32>;

	using Vec2f64 = glm::vec<2, f64>;
	using Vec3f64 = glm::vec<3, f64>;
	using Vec4f64 = glm::vec<4, f64>;

	using Mat2f64 = glm::mat<2, 2, f64>;
	using Mat3f64 = glm::mat<3, 3, f64>;
	using Mat4f64 = glm::mat<4, 4, f64>;

	using Vec2i8 = glm::vec<2, i8>;
	using Vec3i8 = glm::vec<3, i8>;
	using Vec4i8 = glm::vec<4, i8>;

	using Vec2u8 = glm::vec<2, u8>;
	using Vec3u8 = glm::vec<3, u8>;
	using Vec4u8 = glm::vec<4, u8>;

	using Vec2i16 = glm::vec<2, i16>;
	using Vec3i16 = glm::vec<3, i16>;
	using Vec4i16 = glm::vec<4, i16>;

	using Vec2u16 = glm::vec<2, u16>;
	using Vec3u16 = glm::vec<3, u16>;
	using Vec4u16 = glm::vec<4, u16>;

	using Vec2i32 = glm::vec<2, i32>;
	using Vec3i32 = glm::vec<3, i32>;
	using Vec4i32 = glm::vec<4, i32>;

	using Vec2u32 = glm::vec<2, u32>;
	using Vec3u32 = glm::vec<3, u32>;
	using Vec4u32 = glm::vec<4, u32>;

	using Vec2i64 = glm::vec<2, i64>;
	using Vec3i64 = glm::vec<3, i64>;
	using Vec4i64 = glm::vec<4, i64>;

	using Vec2u64 = glm::vec<2, u64>;
	using Vec3u64 = glm::vec<3, u64>;
	using Vec4u64 = glm::vec<4, u64>;

	using Vec2uSize = glm::vec<2, uSize>;
	using Vec3uSize = glm::vec<3, uSize>;
	using Vec4uSize = glm::vec<4, uSize>;

	using Vec2iSize = glm::vec<2, iSize>;
	using Vec3iSize = glm::vec<3, iSize>;
	using Vec4iSize = glm::vec<4, iSize>;

	using Vec2i = Vec2i32;
	using Vec3i = Vec3i32;
	using Vec4i = Vec4i32;

	using Vec2u = Vec2u32;
	using Vec3u = Vec3u32;
	using Vec4u = Vec4u32;

	using Vec2 = Vec2f32;
	using Vec3 = Vec3f32;
	using Vec4 = Vec4f32;

	using Vec2p = Vec2pf32;
	using Vec3p = Vec3pf32;
	using Vec4p = Vec4pf32;

	using Vec2us = Vec2uSize;
	using Vec3us = Vec3uSize;
	using Vec4us = Vec4uSize;

	using Vec2is = Vec2iSize;
	using Vec3is = Vec3iSize;
	using Vec4is = Vec4iSize;

	using Mat2 = Mat2f32;
	using Mat3 = Mat3f32;
	using Mat4 = Mat4f32;

	using Point = Vec3;
	using Colour = Vec4;
	using ColourX2 = glm::mat<2, 4, f32>;

    [[nodiscard]] consteval bool IsDebugMode() noexcept
    {
    #ifdef NDEBUG
        return false;
    #else
        return true;
    #endif
    }

    [[nodiscard]] consteval bool IsDistributionMode() noexcept
	{
    #ifdef DIST
		return true;
    #else
		return false;
    #endif
	}

    [[nodiscard]] constexpr const char* ToCharPtr(const std::u8string& str)
    {
		return std::bit_cast<char const*>(str.c_str());
    }

	template <typename T>
	concept HasPipeOperator = requires(T a, T b) {
	    { a | b };
	};

	template <typename T>
	concept HasAndOperator = requires(T a, T b) {
	    { a & b };
	};

	template <typename T>
	concept HasEqualOperator = requires(T a, T b) {
		{ a == b };
	};

	template <typename T>
	[[nodiscard]] constexpr bool testBitMask(T value, T bitMask) noexcept requires std::is_integral_v<T> || (std::is_enum_v<T> && HasAndOperator<T> && HasEqualOperator<T>)
	{
		return (value & bitMask) == bitMask;
	}

	template <typename T>
	[[nodiscard]] constexpr T alignUp(T value, size_t alignment) noexcept
	{
		return ((value + alignment - 1) & ~(alignment - 1));
	}
}
