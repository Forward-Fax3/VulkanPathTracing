#pragma once
#include "BaseEvent.hpp"


namespace OWC
{
	class WindowCloseEvent final : public BaseEvent
	{
	public:
		inline WindowCloseEvent() : BaseEvent(EventType::WindowClose) {}
		~WindowCloseEvent() override = default;

		static consteval EventType GetStaticType() { return EventType::WindowClose; }
	};
}
