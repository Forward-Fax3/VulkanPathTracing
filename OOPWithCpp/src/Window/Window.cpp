#include <SDL3/SDL.h>
#include <SDL3/SDL_version.h>
#include <backends/imgui_impl_sdl3.h>

#include "Window.hpp"

#include "Log.hpp"


namespace OWC
{
	namespace OWCG = OWC::Graphics;

	Window::Window(const WindowProperties& properties)
		: m_Properties(properties)
	{
		// print SDL version for debugging purposes
		Log<LogLevel::Debug>("SDL Version: {}", SDL_GetVersion());

		SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

		m_Window.reset(SDL_CreateWindow( // Creates the window and stores it in the unique_ptr with a custom deleter
			ToCharPtr(properties.Title),
			static_cast<int>(m_Properties.Width), static_cast<int>(m_Properties.Height),
			SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_VULKAN
		));

		if (m_Window == nullptr)
			Log<LogLevel::Critical>("unable to open window: {}", SDL_GetError());

		SDL_SyncWindow(m_Window.get());
		SetPixelSize();

		m_WindowEvent = std::make_unique<WindowEvent>(SDL_GetWindowID(m_Window.get()));
		m_GraphicsContext = OWCG::GraphicsContext::CreateGraphicsContext(*m_Window, m_Properties);
	}

	Window::~Window()
	{
		m_GraphicsContext.reset();
		m_Window.reset();
		SDL_Delay(100);
		SDL_Quit();
	}

	void Window::Update() const
	{
		PollEvents();

		if (!m_IsMinimized)
			m_GraphicsContext->SwapPresentImage();

#ifndef DIST
		m_GraphicsContext->FlushValidationMessages();
#endif
	}

	void Window::Resize(u32 width, u32 height)
	{
		if (m_Window)
		{
			width = glm::max(width, 210u);
			height = glm::max(height, 144u);
			SDL_SetWindowSize(m_Window.get(), static_cast<int>(width), static_cast<int>(height));
			m_Properties.Width = width;
			m_Properties.Height = height;
			SDL_SyncWindow(m_Window.get());
			SetPixelSize();
		}
		// always return false so that layers can handle the resize event if needed
		m_GraphicsContext->Resize();
	}

	void Window::Minimize()
	{
		m_IsMinimized = true;
		m_GraphicsContext->Minimize();
	}

	void Window::Restore()
	{
		m_IsMinimized = false;
		m_GraphicsContext->Restore();
	}

	void Window::ToggleFullScreen() const
	{
		if (m_Window)
		{
			const bool currentFullScreen = (SDL_GetWindowFlags(m_Window.get()) & SDL_WINDOW_FULLSCREEN) == SDL_WINDOW_FULLSCREEN;
			SDL_SetWindowFullscreen(m_Window.get(), !currentFullScreen);
		}
	}

	void Window::ImGuiInit() const
	{
		ImGui_ImplSDL3_InitForVulkan(m_Window.get());
		m_GraphicsContext->ImGuiInit();
	}

	void Window::ImGuiShutdown() const
	{
		m_GraphicsContext->WaitForIdle();
		m_GraphicsContext->ImGuiShutdown();
		ImGui_ImplSDL3_Shutdown();
	}

	void Window::ImGuiNewFrame() const
	{
		m_GraphicsContext->ImGuiNewFrame();
		ImGui_ImplSDL3_NewFrame();
	}

	void Window::PollEvents() const
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			m_WindowEvent->EventCall(event);
		}
	}

	void Window::SetPixelSize()
	{
		i32 x;
		i32 y;
		SDL_GetWindowSizeInPixels(m_Window.get(), &x, &y);
		m_Properties.PixelWidth = static_cast<u32>(x);
		m_Properties.PixelHeight = static_cast<u32>(y);
	}
} // namespace OWC
