#pragma once
#include "Core.hpp"
#include "BaseHittable.hpp"

#include <memory>
#include <vector>


namespace OWC
{
	class Hitables : public BaseHitable
	{
	public:
		Hitables() = default;

		void AddObject(const std::shared_ptr<BaseHitable>& newHittable);
		void AddObjects(const std::vector<std::shared_ptr<BaseHitable>>& newHittables);
		void AddObjects(const std::shared_ptr<Hitables>& newHittables);

		OWC_FORCE_INLINE void Reserve(uSize numberOfObjects) { m_Hitables.reserve(numberOfObjects); }
		OWC_FORCE_INLINE uSize GetNumberOfObjects() const { return m_Hitables.size(); }
		OWC_FORCE_INLINE std::vector<std::shared_ptr<BaseHitable>>& GetObjects() { return m_Hitables; }
		OWC_FORCE_INLINE void ClearObjects() { m_Hitables.clear(); }
		OWC_FORCE_INLINE const std::function<Colour(const Ray& ray)>& GetBackgroundFunction() const { return m_BackgroundFunction; }

		bool VECTORCALL IsHit(const Ray& ray, Interval& range, HitData& hitData) const noexcept override;

		AABB GetAABB() const override { return m_AABB; }

		Colour BackgroundColour(const Ray& ray) const override
		{
			return m_BackgroundFunction ? 
				m_BackgroundFunction(ray) : 
				BaseHitable::BackgroundColour(ray);
		}

		OWC_FORCE_INLINE void SetBackgroundFunction(const std::function<Colour(const Ray& ray)>& backgroundFunction)
		{
			m_BackgroundFunction = backgroundFunction;
		}

	private:
		std::vector<std::shared_ptr<BaseHitable>> m_Hitables;
		std::function<Colour(const Ray& ray)> m_BackgroundFunction;
		AABB m_AABB;
	};
}
