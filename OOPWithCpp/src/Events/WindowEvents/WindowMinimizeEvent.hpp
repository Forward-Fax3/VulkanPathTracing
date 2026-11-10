#pragma once
#include "BaseEvent.hpp"


namespace OWC
{
	class WindowMinimize : public BaseEvent
	{
	public:
		WindowMinimize() : BaseEvent(EventType::WindowMinimize) {}
		~WindowMinimize() override = default;
		WindowMinimize(const WindowMinimize&) = default;
		WindowMinimize& operator=(const WindowMinimize&) = default;
		WindowMinimize(WindowMinimize&&) = default;
		WindowMinimize& operator=(WindowMinimize&&) = default;

		static consteval EventType GetStaticType() { return EventType::WindowMinimize; }
	};
}
