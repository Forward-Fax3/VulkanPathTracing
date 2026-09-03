#pragma once
#include "BaseHittable.hpp"
#include "Ray.hpp"
#include "AABB.hpp"

#include <memory>

#if ((defined(_WIN32) || defined(_WIN64)) && defined(_MSC_VER))
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#endif


namespace OWC
{
	class alignas(64) Sphere : public BaseHitable
	{
	public:
		Sphere() = delete;
		OWC_FORCE_INLINE Sphere(const Vec3& center, f32 radius, const std::shared_ptr<BaseMaterial>& mat) : m_Radius(radius), m_InvRadius(1.0f / radius), m_Center(center), m_Material(mat) {}
		~Sphere() override = default;

		Sphere(const Sphere&) = delete;
		Sphere& operator=(const Sphere&) = delete;
		Sphere(Sphere&&) = delete;
		Sphere& operator=(Sphere&&) = delete;

		bool VECTORCALL IsHit(const Ray& ray, Interval& range, HitData& hitData) const override;

		AABB GetAABB() const override;

	private:
		static Vec2 GetSphereUV(const Vec3& point);

	private:
		f32 m_Radius;
		f32 m_InvRadius;
		Vec3 m_Center;
		std::shared_ptr<BaseMaterial> m_Material;
	};
}

#if ((defined(_WIN32) || defined(_WIN64)) && defined(_MSC_VER))
#pragma warning(pop)
#endif
