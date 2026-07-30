#pragma once
#include "Core.hpp"

#include "Ray.hpp"
#include "Interval.hpp"

#define SIMD 1

#if defined(_WIN32) || defined(_WIN64)
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#endif


namespace OWC
{
	class alignas(32) AABB
	{
	public:
		enum class Axis : uint8_t { x = 0, y, z, none };

	public:
		explicit AABB() = default;
		explicit AABB(const Interval& interval);
		explicit AABB(const Interval& x, const Interval& y, const Interval& z);
		explicit AABB(const Point& a, const Point& b);
		explicit AABB(const AABB& box0, const AABB& box1);

		bool IsBigger(const AABB& otherAABB) const;

		OWC_FORCE_INLINE const Interval& GetAxisInterval(const Axis& axis) const;

		OWC_FORCE_INLINE bool VECTORCALL IsHit(const Ray& ray, Interval rayT) const;

		void Expand(const AABB& newAABB);

		[[nodiscard]] inline double GetSurfaceArea() const { return 2.0 * (m_XInterval.Size() * m_YInterval.Size() + m_XInterval.Size() * m_ZInterval.Size() + m_YInterval.Size() * m_ZInterval.Size()); }

		[[nodiscard]] OWC_FORCE_INLINE AABB::Axis LongestAxis() const { return m_LongestAxis; }

		static const AABB Empty;
		static const AABB Univers;

	private:
		void MinimumPadding();
		void SetLongestAxis();
		OWC_FORCE_INLINE void FinishIntervalSetup() { MinimumPadding(); SetLongestAxis(); }

	private:
		Interval m_XInterval;
		Interval m_YInterval;
		Interval m_ZInterval;
		Axis m_LongestAxis = Axis::none;
	};

	OWC_FORCE_INLINE AABB::Axis& operator++(AABB::Axis& axis)
	{
#if _HAS_CXX23
		axis = static_cast<AABB::Axis>(std::to_underlying(axis) + 1);
#else
		axis = static_cast<AABB::Axis>(static_cast<uint8_t>(axis) + 1);
#endif
		return axis;
	}

	OWC_FORCE_INLINE AABB::Axis operator++(AABB::Axis& axis, int)
	{
		AABB::Axis oldAxis = axis;
		++axis;
		return oldAxis;
	}

	static constexpr std::underlying_type_t<AABB::Axis> operator+(AABB::Axis axis) noexcept
	{
#if _HAS_CXX23
		return std::to_underlying(axis);
#else
		return static_cast<std::underlying_type_t<AABB::Axis>>(axis);
#endif
	}

	OWC_FORCE_INLINE const Interval& AABB::GetAxisInterval(const Axis& axis) const
	{
		switch (axis)
		{
			using enum Axis;
		case x:
			return m_XInterval;
		case y:
			return m_YInterval;
		case z:
			return m_ZInterval;
		case none:
		default:
			return Interval::Univers;
		}
	}

	// Ray - AABB intersection test using the "slab" method with SIMD and AVX2 optimisations
	// taken from one of my previous project but modified to work with 32-bit floats and AVX2 instead of 64-bit floats and AVX512
	// since added some further optimisations
	OWC_FORCE_INLINE bool VECTORCALL AABB::IsHit(const Ray& ray, Interval rayT) const
	{
#if (AVX2 & SIMD) == 1
		const __m256 AVX2f32_AltNegXOR = _mm256_set_ps(-0.0f, 0.0f, -0.0f, 0.0f, -0.0f, 0.0f, -0.0f, 0.0f);


		// load m_XInterval, m_YInterval and m_ZInterval into an AVX2 register
		// creates a register filled like (garbage, garbage, m_ZIntervalMax, m_ZIntervalMin, m_YIntervalMax, m_YIntervalMin, m_XIntervalMax, m_XIntervalMin)
		const __m256 AVX2f32_AxisBounds = _mm256_load_ps(std::bit_cast<f32*>(this));


		// create T in AVX2 register
		const __m256 AVX2f32_T = _mm256_fmsub_ps(AVX2f32_AxisBounds, ray.Get256InvDirection(), ray.Get256OriOverDir());

		// creates the swapped T register
		// swaps the 64 bit lanes so that AVX2f32_T128BitSwaped is (garbage, garbage, Z1, Z0, Y1, Y0, X1, X0) -> (garbage, garbage, Z0, Z1, Y0, Y1, X0, X1)
#if AVX512 == 1
		const __m256 AVX2f32_T64BitSwaped = _mm256_permute_ps(AVX2f32_T, _MM_PERM_CDAB);
#else // !AVX512
		const __m256 AVX2f32_T64BitSwaped = _mm256_permute_ps(AVX2f32_T, _MM_SHUFFLE(2, 3, 0, 1));
#endif // AVX512


		// performs an xor to negate max in AVX2f32_T and min in AVX2f32_T64BitSwaped so that min and max operations can be performed
		const __m256 AVX2f32_AltNegT = _mm256_xor_ps(AVX2f32_T, AVX2f32_AltNegXOR);
		const __m256 AVX2f32_AltNegSwapedT = _mm256_xor_ps(AVX2f32_T64BitSwaped, AVX2f32_AltNegXOR);

		// minimums AVX2f32_AltNegT and AVX2f32_AltNegSwapedT so that AVX2f32_MinNegMaxT is (garbage, garbage, -Zmax, Zmin, -Ymax, Ymin, -Xmax, Xmin)
		const __m256 AVX2f32_MinNegMaxT = _mm256_min_ps(AVX2f32_AltNegT, AVX2f32_AltNegSwapedT);


		// creates 2 variables for pair 1 and pair 2 of AVX2f32_MinNegMaxT to perform max operation on all 3 axis
		// pair 0 is implicit as it is already in the correct position
		// pair 4 is garbage data so we don't care about this data
#if AVX512 == 1
		const __m128 SSEf32_pair1MinMax = _mm_permute_ps(_mm256_castps256_ps128(AVX2f32_MinNegMaxT), _MM_PERM_BADC);
#else // !AVX512
		const __m128 SSEf32_pair1MinMax = _mm_permute_ps(_mm256_castps256_ps128(AVX2f32_MinNegMaxT), _MM_SHUFFLE(1, 0, 3, 2));
#endif // AVX512
		const __m128 SSEf32_pair2MinMax = _mm256_extractf128_ps(AVX2f32_MinNegMaxT, 1);

		// perform max operation on all 3 axis to get the final MinNegMaxT value
		const __m128 SSEf32_AltNegMinMaxPairs01 = _mm_max_ps(_mm256_castps256_ps128(AVX2f32_MinNegMaxT), SSEf32_pair1MinMax);
		const __m128 SSEf32_AltNegMinMaxPairs012 = _mm_max_ps(SSEf32_AltNegMinMaxPairs01, SSEf32_pair2MinMax);

		// get rayT and perform max with that as well to make sure that SSEf32_AltNegMinMaxlane012 is inside the rays boundary
		const __m128 SSEf32_IntevalLimits = _mm_set_ps(0.0f, 0.0f, rayT.GetMax(), rayT.GetMin());
		const __m128 SSEf32_AltNegIntevalLimits = _mm_xor_ps(SSEf32_IntevalLimits, _mm256_castps256_ps128(AVX2f32_AltNegXOR));
		const __m128 SSEf32_AltNegMinMaxFinal = _mm_max_ps(SSEf32_AltNegMinMaxPairs012, SSEf32_AltNegIntevalLimits);

		// inverts the values back for final comparison
		const __m128 SSEf32_Test = _mm_xor_ps(SSEf32_AltNegMinMaxFinal, _mm256_castps256_ps128(AVX2f32_AltNegXOR));

		// test the boundary of the SSEf32_Test minimum and maximum to make sure max is not smaller than or equal to the minimum bound
		// using _mm_permute_ps and _mm_comixx_ss, _mm_comilt_ss in this case, intrinsics
#if AVX512 == 1
		const __m128 SSEf32_maxTest = _mm_permute_ps(SSEf32_Test, _MM_PERM_DCAB);
#else
		const __m128 SSEf32_maxTest = _mm_permute_ps(SSEf32_Test, _MM_SHUFFLE(3, 2, 0, 1));
#endif
		return _mm_comilt_ss(SSEf32_Test, SSEf32_maxTest);

#else // !(AVX2 & SIMD)
		for (Axis axis = Axis::x; axis <= Axis::z; axis++)
		{
			const Interval& axisBounds = GetAxisInterval(axis);
			const f32 rAxisOrigin = ray.GetOrigin()[+axis];
			const f32 rAxisInvDirection = ray.GetInvDirection()[+axis];

			Vec2 t = (axisBounds.GetAsVector() - rAxisOrigin) * rAxisInvDirection;

			if (t.x > t.y)
				std::swap(t.x, t.y);

#if SIMD == 1
			const __m128 SSEf32_InvertValue = _mm_set_ps(-0.0f, 0.0f, -0.0f, 0.0f);

			// load t into SSE register
			__m128 SSEf32_T = _mm_set_ps(0.0f, 0.0f, t.y, t.x);
			// negate y so that the same comparison can be computed on x and y
			__m128 SSEf32_AltNegT = _mm_xor_ps(SSEf32_InvertValue, SSEf32_T);
			// rayT into SSE register
			__m128 SSEf32_IntevalLimits = _mm_set_ps(0.0f, 0.0f, rayT.GetMax(), rayT.GetMin());
			// negating y so that the same comparison can be computed on x and y
			__m128 SSEf32_AltNegRayT = _mm_xor_ps(SSEf32_InvertValue, SSEf32_IntevalLimits);

			// perform comparison
			__m128 SSEf32_BitMask = _mm_cmpgt_ps(SSEf32_AltNegT, SSEf32_AltNegRayT);

			// load based on mask rayT or T into register
			rayT.SetMinMax(_mm_blendv_ps(SSEf32_IntevalLimits, SSEf32_T, SSEf32_BitMask));

#else // !SIMD
			if (t.x > rayT.GetMin())
				rayT.SetMin(t.x);
			if (t.y < rayT.GetMax())
				rayT.SetMax(t.y);
#endif // SIMD

			if (rayT.GetMin() >= rayT.GetMax())
				return false;
		}
		return true;
#endif // AVX512 & SIMD
	}
}

#undef SIMD

#if defined(_WIN32) || defined(_WIN64)
#pragma warning(pop)
#endif
