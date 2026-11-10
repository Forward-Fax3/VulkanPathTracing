#pragma once
#include "Core.hpp"
#include "BaseEvent.hpp"


namespace OWC
{
	class WindowResize final : public BaseEvent
	{
	public:
		WindowResize(const u32 width, const u32 height)
			: BaseEvent(EventType::WindowResize), m_NewSize(width, height) {}

		static consteval EventType GetStaticType() { return EventType::WindowResize; }

		[[nodiscard]] OWC_FORCE_INLINE u32 GetWidth() const { return m_NewSize.x; }
		[[nodiscard]] OWC_FORCE_INLINE u32 GetHeight() const { return m_NewSize.y; }
		[[nodiscard]] OWC_FORCE_INLINE Vec2u GetSize() const { return m_NewSize; }

	private:
		Vec2u m_NewSize;
	};
} // namespace OWC