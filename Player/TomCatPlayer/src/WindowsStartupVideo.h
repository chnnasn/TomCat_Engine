#pragma once
#define GLFW_INCLUDE_NONE
#include <GLFW/include/GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/include/GLFW/glfw3native.h>
#include <mfplay.h>
#include <wrl.h>
#include <atomic>
#include <filesystem>
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace TomCat {
	enum class StartupResult { Completed, Closed, Failed };

	class StartupPlaybackEvents final : public Microsoft::WRL::RuntimeClass<
		Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IMFPMediaPlayerCallback>
	{
	public:
		std::atomic<bool> Finished{false};
		std::atomic<HRESULT> Error{S_OK};
		void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event) override
		{
			if (FAILED(event->hrEvent)) Error.store(event->hrEvent);
			else if (event->eEventType == MFP_EVENT_TYPE_ERROR) Error.store(E_FAIL);
			else if (event->eEventType == MFP_EVENT_TYPE_PLAYBACK_ENDED) Finished.store(true);
		}
	};

	inline StartupResult PlayStartupVideo(GLFWwindow* window, const std::filesystem::path& path)
	{
		const HWND parent = glfwGetWin32Window(window);
		const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if (FAILED(com) && com != RPC_E_CHANGED_MODE) return StartupResult::Failed;
		RECT area{};
		GetClientRect(parent, &area);
		const HWND canvas = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
			0, 0, area.right, area.bottom, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
		auto events = Microsoft::WRL::Make<StartupPlaybackEvents>();
		Microsoft::WRL::ComPtr<IMFPMediaPlayer> player;
		HRESULT hr = canvas && events ? MFPCreateMediaPlayer(path.c_str(), TRUE,
			MFP_OPTION_FREE_THREADED_CALLBACK, events.Get(), canvas, &player) : E_FAIL;
		auto result = StartupResult::Failed;
		auto lastProgress = GetTickCount64();
		LONGLONG previous = -1;
		while (SUCCEEDED(hr))
		{
			glfwWaitEventsTimeout(0.05);
			if (glfwWindowShouldClose(window)) { result = StartupResult::Closed; break; }
			hr = events->Error.load();
			if (FAILED(hr)) break;
			if (events->Finished.load()) { result = StartupResult::Completed; break; }
			RECT current{};
			GetClientRect(parent, &current);
			if (!EqualRect(&area, &current))
			{
				area = current;
				MoveWindow(canvas, 0, 0, area.right, area.bottom, TRUE);
				player->UpdateVideo();
			}
			PROPVARIANT position{};
			if (SUCCEEDED(player->GetPosition(MFP_POSITIONTYPE_100NS, &position))
				&& position.vt == VT_I8 && position.hVal.QuadPart != previous)
			{
				previous = position.hVal.QuadPart;
				lastProgress = GetTickCount64();
			}
			PropVariantClear(&position);
			if (GetTickCount64() - lastProgress > 15000) { hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT); break; }
		}
		if (player) player->Shutdown();
		player.Reset();
		events.Reset();
		if (canvas) DestroyWindow(canvas);
		if (SUCCEEDED(com)) CoUninitialize();
		if (result == StartupResult::Failed)
			TC_Core_Error("Startup video playback failed (HRESULT {0})", static_cast<long>(hr));
		return result;
	}
}
