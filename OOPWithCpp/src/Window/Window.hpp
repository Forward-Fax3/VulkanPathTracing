#pragma once
#define WINDOW_HPP
#include "Core.hpp"
#include <functional>
#include <memory>

#include "BaseEvent.hpp"
#include "WindowEvent.hpp"

#ifndef GRAPHICSCONTEXT_HPP
#include "GraphicsContext.hpp"
#endif


namespace OWC::Graphics
{
	class GraphicsContext;
}

namespace OWC
{
	struct WindowProperties
	{
		std::u8string Title;
		u32 Width;
		u32 Height;
		u32 PixelWidth = NULL;
		u32 PixelHeight = NULL;
	};

	class Window
	{
	public:
		Window() = delete;
		explicit Window(const WindowProperties& properties);
		~Window();

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;
		Window(Window&&) = delete;
		Window& operator=(Window&&) = delete;

		void Update() const;

		inline void SetEventCallback(const std::function<void(BaseEvent&)>& callback) const { m_WindowEvent->SetCallback(callback); }

		[[nodiscard]] OWC_FORCE_INLINE Graphics::GraphicsContext& GetGraphicsContext() const { return *m_GraphicsContext; }
		[[nodiscard]] OWC_FORCE_INLINE std::weak_ptr<Graphics::GraphicsContext> GetGraphicsContextPtr() const { return m_GraphicsContext; }

		[[nodiscard]] OWC_FORCE_INLINE u32 GetWidth() const { return m_Properties.Width; }
		[[nodiscard]] OWC_FORCE_INLINE u32 GetHeight() const { return m_Properties.Height; }
		[[nodiscard]] OWC_FORCE_INLINE Vec2u GetWindowSize() const { return { GetWidth(), GetHeight() }; }

		[[nodiscard]] OWC_FORCE_INLINE u32 GetPixelWidth() const { return m_Properties.PixelWidth; }
		[[nodiscard]] OWC_FORCE_INLINE u32 GetPixelHeight() const { return m_Properties.PixelHeight; }
		[[nodiscard]] OWC_FORCE_INLINE Vec2u GetPixelSize() const { return { GetPixelWidth(), GetPixelHeight() }; }

		[[nodiscard]] OWC_FORCE_INLINE bool IsWindowMinimized() const { return m_IsMinimized; }

		void Resize(u32 width, u32 height);
		void Minimize();
		void Restore();
		void ToggleFullScreen() const;

		void ImGuiInit() const;
		void ImGuiShutdown() const;

		void ImGuiNewFrame() const;

	private:
		void PollEvents() const;
		void SetPixelSize();

	private:
		std::unique_ptr<WindowEvent> m_WindowEvent;
		WindowProperties m_Properties;
		std::unique_ptr<SDL_Window, decltype([](SDL_Window* windowPtr){ SDL_DestroyWindow(windowPtr); })> m_Window = nullptr;
		std::shared_ptr<Graphics::GraphicsContext> m_GraphicsContext = nullptr;
		bool m_IsMinimized = false;
	};
}
