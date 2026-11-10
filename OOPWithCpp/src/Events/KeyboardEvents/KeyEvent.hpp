#include "BaseEvent.hpp"


namespace OWC
{
	class KeyPressedEvent : public BaseEvent
	{
	public:
		KeyPressedEvent() = delete;
		explicit KeyPressedEvent(const u32 keycode)
			: BaseEvent(EventType::KeyPressed), m_Keycode(keycode) {}

		[[nodiscard]] u32 GetKeycode() const { return m_Keycode; }

		static consteval EventType GetStaticType() { return EventType::KeyPressed; }
		
	private:
		u32 m_Keycode;
	};

	class KeyReleased : public BaseEvent
	{
		public:
		KeyReleased() = delete;
		explicit KeyReleased(const u32 keycode)
			: BaseEvent(EventType::KeyReleased), m_Keycode(keycode) {}

		[[nodiscard]] u32 GetKeycode() const { return m_Keycode; }

		static consteval EventType GetStaticType() { return EventType::KeyReleased; }

	private:
		u32 m_Keycode;
	};
}