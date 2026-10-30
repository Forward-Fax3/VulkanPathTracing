#include "SplitBVH.hpp"

#include <algorithm>


namespace OWC
{
	SplitBVH::SplitBVH(const std::shared_ptr<Hitables>& hitables)
	{
		m_BackgroundFunction = hitables->GetBackgroundFunction();

		auto numberOfHitables = static_cast<iSize>(hitables->GetNumberOfObjects());

		if (numberOfHitables == 0)
		{
			m_Left = m_Right = std::make_shared<NoHit>();
			m_AABB = AABB::Empty;
			return;
		}

		auto objects = hitables->GetObjects();
		m_AABB = hitables->GetAABB();

		if (numberOfHitables == 1)
		{
			m_Left = objects[0];
			m_Right = std::make_shared<NoHit>();
			return;
		}
		if (numberOfHitables == 2)
		{
			m_Left = objects[0];
			m_Right = objects[1];
			return;
		}

		AABB::Axis splitAxis = m_AABB.LongestAxis();
		std::ranges::sort(objects, [splitAxis](const std::shared_ptr<BaseHitable>& a, const std::shared_ptr<BaseHitable>& b) noexcept {
			return BoxComparison(a, b, splitAxis);
			});

		if (numberOfHitables == 3)
		{
			m_Left = std::make_shared<SplitBVH>(objects, 0, 2, PRIVATE());
			m_Right = objects[2];
			return;
		}

		iSize midPoint = numberOfHitables / 2;
		m_Left = std::make_shared<SplitBVH>(objects, 0, midPoint, PRIVATE());
		m_Right = std::make_shared<SplitBVH>(objects, midPoint, numberOfHitables, PRIVATE());
	}

	SplitBVH::SplitBVH(std::vector<std::shared_ptr<BaseHitable>>& objects, iSize start, iSize end, SplitBVH::PRIVATE)
	{
		iSize range = end - start;

		[[unlikely]] if (range == 1) // should not happen normally but just in case
		{
			m_Left = objects[start];
			m_Right = std::make_shared<NoHit>();
			m_AABB = m_Left->GetAABB();
			return;
		}
		if (range == 2)
		{
			m_Left = objects[start];
			m_Right = objects[start + 1];
			m_AABB = AABB(m_Left->GetAABB(), m_Right->GetAABB());
			return;
		}


		m_AABB = AABB::Empty;
		for (iSize i = start; i < end; i++)
			m_AABB.Expand(objects[i]->GetAABB());

		AABB::Axis splitAxis = m_AABB.LongestAxis();
		std::sort(objects.begin() + start, objects.begin() + end, [&splitAxis](const std::shared_ptr<BaseHitable>& a, const std::shared_ptr<BaseHitable>& b) noexcept {
			return BoxComparison(a, b, splitAxis);
			});

		if (range == 3)
		{
			m_Left = std::make_shared<SplitBVH>(objects, start, start + 2, PRIVATE());
			m_Right = objects[start + 2];
			return;
		}

		iSize midPoint = range / 2;
		m_Left = std::make_shared<SplitBVH>(objects, start, start + midPoint, PRIVATE());
		m_Right = std::make_shared<SplitBVH>(objects, start + midPoint, end, PRIVATE());
	}

	bool VECTORCALL SplitBVH::IsHit(const Ray& ray, Interval& range, HitData& hitData) const noexcept
	{
		if (!m_AABB.IsHit(ray, range))
			return false;

		return IsHitChildren(ray, range, hitData);
	}

	bool VECTORCALL SplitBVH::IsHitChildren(const Ray& ray, Interval& range, HitData& hitData) const noexcept
	{
		bool hasHit = m_Left->IsHit(ray, range, hitData);
		hasHit |= m_Right->IsHit(ray, range, hitData);
		return hasHit;
	}

	bool SplitBVH::BoxComparison(const std::shared_ptr<BaseHitable>& a, const std::shared_ptr<BaseHitable>& b, AABB::Axis axis)
	{
		const Interval BoxAAxisInterval = a->GetAABB().GetAxisInterval(axis);
		const Interval BoxBAxisInterval = b->GetAABB().GetAxisInterval(axis);

		if (const f32 BoxAAxisIntervalMin = BoxAAxisInterval.GetMin(), BoxBAxisIntervalMin = BoxBAxisInterval.GetMin();
			!Interval(-1e-4f, 1e-4f).Contains(BoxAAxisIntervalMin - BoxBAxisIntervalMin))
			return BoxAAxisIntervalMin < BoxBAxisIntervalMin;

		const f32 BoxAAxisIntervalMax = BoxAAxisInterval.GetMax();
		const f32 BoxBAxisIntervalMax = BoxBAxisInterval.GetMax();

		return BoxAAxisIntervalMax < BoxBAxisIntervalMax;
	}
}
