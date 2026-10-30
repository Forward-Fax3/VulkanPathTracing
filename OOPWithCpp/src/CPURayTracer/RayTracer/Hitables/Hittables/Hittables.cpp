#include "Core.hpp"
#include "Hittables.hpp"


namespace OWC
{
	void Hitables::AddObject(const std::shared_ptr<BaseHitable>& newHittable)
	{
		if (const auto hittables = std::dynamic_pointer_cast<Hitables>(newHittable))
			AddObjects(hittables);
		else
		{
			m_Hitables.emplace_back(newHittable);
			m_AABB.Expand(newHittable->GetAABB());
		}
	}

	void Hitables::AddObjects(const std::vector<std::shared_ptr<BaseHitable>>& newHittables)
	{
		uSize newSize = m_Hitables.size() + newHittables.size();

		if (m_Hitables.capacity() < newSize)
			m_Hitables.reserve(newSize);

		for (const auto& hittable : newHittables)
		{
			AddObject(hittable);
			m_AABB.Expand(hittable->GetAABB());
		}
	}

	void Hitables::AddObjects(const std::shared_ptr<Hitables>& newHittables)
	{
		m_AABB.Expand(newHittables->GetAABB());

		uSize newSize = m_Hitables.size() + newHittables->GetNumberOfObjects();

		if (m_Hitables.capacity() < newSize)
			m_Hitables.reserve(newSize);

		for (uSize i = 0; i < newHittables->GetNumberOfObjects(); i++)
			AddObject(newHittables->m_Hitables[i]);
	}

	bool VECTORCALL Hitables::IsHit(const Ray& ray, Interval& range, HitData& hitData) const noexcept
	{
		bool hasAnyHit = false;
		
		for (const auto& hittable : m_Hitables)
			hasAnyHit |= hittable->IsHit(ray, range, hitData);

		return hasAnyHit;
	}
}