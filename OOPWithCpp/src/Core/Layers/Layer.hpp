#pragma once


namespace OWC
{
	class Layer
	{
	public:
		Layer() = default;
		virtual ~Layer() = default;
		Layer(const Layer&) = delete;
		Layer& operator=(const Layer&) = delete;
		Layer(Layer&&) = delete;
		Layer&& operator=(Layer&&) = delete;

		virtual void OnUpdate() { /* default empty implementation */ };
		virtual void ImGuiRender() { /* default empty implementation */ };
		virtual void OnEvent(class BaseEvent&) { /* default empty implementation */ };

		[[nodiscard]] OWC_FORCE_INLINE bool IsActive() const { return m_Active; }
		OWC_FORCE_INLINE void SetActiveState(const bool active) { m_Active = active; }

	private:
		bool m_Active = true;
	};
}
