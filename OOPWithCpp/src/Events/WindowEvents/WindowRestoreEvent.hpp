#pragma once
#include "BaseEvent.hpp"


namespace OWC
{
	class WindowRestore
		: public BaseEvent
	{
	public:
		WindowRestore() : BaseEvent(EventType::WindowRestore) {}
		~WindowRestore() override = default;
		WindowRestore(const WindowRestore&) = default;
		WindowRestore& operator=(const WindowRestore&) = default;
		WindowRestore(WindowRestore&&) = default;
		WindowRestore& operator=(WindowRestore&&) = default;

		static consteval EventType GetStaticType() { return EventType::WindowRestore; }
	};
}
