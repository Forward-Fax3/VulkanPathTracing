#pragma once
#include "Core.hpp"

#if defined(_WIN32) || defined(_WIN64)
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#endif


namespace OWC
{
	class alignas(64) Ray
	{
	public:
		Ray() = default;
		OWC_FORCE_INLINE explicit Ray(const Point& origin, const Vec3& direction) : m_Origin(origin), m_Direction(glm::normalize(direction)), m_InvDirection(1.0f / m_Direction)
		{
#ifdef AVX2
			const __m256i AVX2i32_FloatLoadPermutationIndex = _mm256_set_epi32(3, 3, 2, 2, 1, 1, 0, 0);

			const __m256 AVX2f32_RayOrigin = _mm256_permutevar8x32_ps(_mm256_castps128_ps256(m_Origin.data), AVX2i32_FloatLoadPermutationIndex);
			m_256_invDirection = _mm256_permutevar8x32_ps(_mm256_castps128_ps256(m_InvDirection.data), AVX2i32_FloatLoadPermutationIndex);

			m_256_OriOverDir = _mm256_mul_ps(AVX2f32_RayOrigin, m_256_invDirection);
#endif
		}

		[[nodiscard]] OWC_FORCE_INLINE const Vec3& GetOrigin() const { return m_Origin; }
		[[nodiscard]] OWC_FORCE_INLINE const Vec3& GetDirection() const { return m_Direction; }
		[[nodiscard]] OWC_FORCE_INLINE const Vec3& GetInvDirection() const { return m_InvDirection; }

		OWC_FORCE_INLINE void SetOrigin(const Vec3& origin)
		{
			m_Origin = origin;
#ifdef AVX2
			const __m256i AVX2i32_FloatLoadPermutationIndex = _mm256_set_epi32(3, 3, 2, 2, 1, 1, 0, 0);

			const __m256 AVX2f32_RayOrigin = _mm256_permutevar8x32_ps(_mm256_castps128_ps256(m_Origin.data), AVX2i32_FloatLoadPermutationIndex);
			m_256_OriOverDir = _mm256_mul_ps(AVX2f32_RayOrigin, m_256_invDirection);
#endif
		}
		OWC_FORCE_INLINE void SetNormalizedDirection(const Vec3& nDirection)
		{
			m_Direction = nDirection;
			m_InvDirection = 1.0f / m_Direction;

#ifdef AVX2
			const __m256i AVX2i32_FloatLoadPermutationIndex = _mm256_set_epi32(3, 3, 2, 2, 1, 1, 0, 0);

			m_256_invDirection = _mm256_permutevar8x32_ps(_mm256_castps128_ps256(m_InvDirection.data), AVX2i32_FloatLoadPermutationIndex);

			const __m256 AVX2f32_RayOrigin = _mm256_permutevar8x32_ps(_mm256_castps128_ps256(m_Origin.data), AVX2i32_FloatLoadPermutationIndex);
			m_256_OriOverDir = _mm256_mul_ps(AVX2f32_RayOrigin, m_256_invDirection);
#endif
		}

		OWC_FORCE_INLINE void SetDirection(const Vec3& direction) { SetNormalizedDirection(glm::normalize(direction)); }

		[[nodiscard]] Vec3 GetPointAtDistance(const f32 t) const { return ::glm::fma(Vec3(t), m_Direction, m_Origin); }

#ifdef AVX2
		[[nodiscard]] OWC_FORCE_INLINE const __m256& Get256InvDirection() const { return m_256_invDirection; }
		[[nodiscard]] OWC_FORCE_INLINE const __m256& Get256OriOverDir() const { return m_256_OriOverDir; }
#endif

	private:
		Point m_Origin = Point(0.0);
		Vec3 m_Direction = Vec3(0.0);
		Vec3 m_InvDirection = Vec3(0.0);

#ifdef AVX2
		// stores ray inverted direction into an AVX2 Register and double each axis into 64 bit lanes
		// this creates a register filled like (garbage, garbage, 1/RayDirectionZ, 1/RayDirectionZ, 1/RayDirectionY, 1/RayDirectionY, 1/RayDirectionX, 1/RayDirectionX)
		// this is an optimisation for AABB:IsHit
		__m256 m_256_invDirection = _mm256_setzero_ps();

		// stores origin/direction into an AVX2 Register and double each axis into 64 bit lanes
		// this creates a register filled like (garbage, garbage, oriZ/RayDirectionZ, oriZ/RayDirectionZ, oriY/RayDirectionY, oriY/RayDirectionY, oriX/RayDirectionX, oriX/RayDirectionX)
		// this is an optimisation for AABB:IsHit
		__m256 m_256_OriOverDir = _mm256_setzero_ps();
#endif
	};
}

#if defined(_WIN32) || defined(_WIN64)
#pragma warning(pop)
#endif
