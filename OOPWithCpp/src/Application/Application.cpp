#include "Application.hpp"
#include "Renderer.hpp"

#include "BaseEvent.hpp"
#include "WindowCloseEvent.hpp"
#include "WindowResize.hpp"
#include "WindowMinimizeEvent.hpp"
#include "WindowRestoreEvent.hpp"
#include "KeyEvent.hpp"

#include <SDL3/SDL_keycode.h>

#include "Log.hpp"

#include "MainLayer.hpp"
#include "ImGuiLayer.hpp"


namespace OWC
{
	Application* Application::s_Instance = nullptr;

	Application::Application(std::bitset<2>& runFlags)
		: m_RunFlags(runFlags)
	{
		if (!s_Instance)
			s_Instance = this;
		else
			Log<LogLevel::Critical>("Application instance already exists!");

		WindowProperties props {
			.Title = u8"OOP With Cpp",
			.Width = 1280,
			.Height = 720
		};

		m_Window = std::make_unique<Window>(props);
		m_Window->SetEventCallback([](BaseEvent& e) { s_Instance->OnEvent(e); });
		Graphics::Renderer::Init();
		m_LayerStack = std::make_unique<LayerStack>();
		PushLayer(std::make_shared<MainLayer>());
		const auto imGui = std::make_shared<ImGuiLayer>();
		m_ImGuiLayer = imGui;
		PushOverlay(imGui);
	}

	Application::~Application()
	{
		m_LayerStack->ClearLayers();
		Graphics::Renderer::Shutdown();
		s_Instance = nullptr;
	}

	void Application::Run()
	{
		const auto imGui = m_ImGuiLayer.lock();
		while (m_RunFlags.test(0)) // While application is running
		{
			const auto now = std::chrono::high_resolution_clock::now();
			m_DeltaTime = std::chrono::duration<f32, std::milli>(now - m_LastTime).count();
			m_LastTime = now;

			m_LayerStack->OnUpdate();

			OWC::ImGuiLayer::Begin();
			m_LayerStack->ImGuiRender();
			imGui->End();

			Graphics::Renderer::FinishRender();
			m_IsFirstFrame = false;
			m_Window->Update();
		}
	}

	void Application::OnEvent(BaseEvent& event) const
	{
		EventDispatcher dispatcher(event);

		dispatcher.Dispatch<WindowCloseEvent>([](const WindowCloseEvent&) {
			s_Instance->Stop();
			return true;
			});

		dispatcher.Dispatch<WindowResize>([](const WindowResize& e) {
			s_Instance->m_Window->Resize(e.GetWidth(), e.GetHeight());
			return false;
			});

		dispatcher.Dispatch<WindowMinimize>([](const WindowMinimize&) {
			s_Instance->m_Window->Minimize();
			return false;
			});

		dispatcher.Dispatch<WindowRestore>([](const WindowRestore&) {
			s_Instance->m_Window->Restore();
			return false;
			});

		dispatcher.Dispatch<KeyPressedEvent>([](const KeyPressedEvent& e) {
			if (!s_Instance->m_KeyStates[e.GetKeycode()])
			{
				s_Instance->m_KeyStates[e.GetKeycode()] = true;
			}
			else return true; // key repeat, event handled

			// close application on pressing ESC key
			if (e.GetKeycode() == SDLK_ESCAPE)
			{
				s_Instance->Stop();
				return true;
			}
			if (e.GetKeycode() == SDLK_F5)
			{
				s_Instance->Restart();
				return true;
			}
			if (e.GetKeycode() == SDLK_F11)
			{
				s_Instance->m_Window->ToggleFullScreen();
				return true;
			}
			return false;
			});

		dispatcher.Dispatch<KeyReleased>([](const KeyReleased& e) {
			s_Instance->m_KeyStates[e.GetKeycode()] = false;
			return false;
			});

		if (event.HasBeenHandled())
			return;

		m_LayerStack->OnEvent(event);
	}
} // namespace OWC	