#include "Sphere.hpp"

#include <glm/gtx/norm.hpp>
#include <glm/gtc/constants.hpp>


namespace OWC
{
	bool VECTORCALL Sphere::IsHit(const Ray& ray, Interval& range, HitData& hitData) const noexcept
	{
		Vec3 oc = m_Center - ray.GetOrigin();

		constexpr f32 a = 1.0f; // ray direction is normalized so the length squared will alway be 1
		f32 h = glm::dot(oc, ray.GetDirection());
		f32 c = glm::length2(oc) - m_Radius * m_Radius;

		f32 discriminant = h * h - a * c;
		if (discriminant <= 0.0f)
			return false;

		f32 sqrtDiscriminant = glm::sqrt(discriminant);

		f32 root = (h - sqrtDiscriminant) / a;
		if (!range.Contains(root))
		{
			root = (h + sqrtDiscriminant) / a;
			if (!range.Contains(root))
				return false;
		}

		range.SetMax(root);
		hitData.point = ray.GetPointAtDistance(root);

		Vec3 normal = (hitData.point - m_Center) * m_InvRadius;
		hitData.SetFaceNormal(ray, normal);
		hitData.uv = GetSphereUV(hitData.normal);

		hitData.material = m_Material.get();

		return true;
	}

	AABB Sphere::GetAABB() const
	{
		return AABB{ Interval(m_Center.x - m_Radius, m_Center.x + m_Radius), Interval(m_Center.y - m_Radius, m_Center.y + m_Radius), Interval(m_Center.z - m_Radius, m_Center.z + m_Radius) };
	}

	Vec2 Sphere::GetSphereUV(const Vec3& normal)
	{
		f32 phi = glm::atan(-normal.z, normal.x) + glm::pi<f32>();
		f32 u = phi * glm::one_over_two_pi<f32>();
		f32 theta = glm::acos(-normal.y);
		f32 v = theta * glm::one_over_pi<f32>();
		return { u, v };
	}
}