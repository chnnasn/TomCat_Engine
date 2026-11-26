#pragma once
#define GLFW_INCLUDE_NONE
#include <GLFW/include/GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/include/GLFW/glfw3native.h>
#include <mfplay.h>
#include <evr.h>
#include <wrl.h>
#include <atomic>
#include <functional>
#include <filesystem>
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace TomCat {
	enum class StartupResult { Completed, Closed, Failed, LoadFailed };

	class StartupPlaybackEvents final : public Microsoft::WRL::RuntimeClass<
		Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IMFPMediaPlayerCallback>
	{
	public:
		std::atomic<bool> Ready{false};
		std::atomic<bool> Playing{false};
		std::atomic<bool> Finished{false};
		std::atomic<HRESULT> Error{S_OK};
		void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event) override
		{
			if (FAILED(event->hrEvent)) Error.store(event->hrEvent);
			else if (event->eEventType == MFP_EVENT_TYPE_MEDIAITEM_SET) Ready.store(true);
			else if (event->eEventType == MFP_EVENT_TYPE_PLAY) Playing.store(true);
			else if (event->eEventType == MFP_EVENT_TYPE_ERROR) Error.store(E_FAIL);
			else if (event->eEventType == MFP_EVENT_TYPE_PLAYBACK_ENDED) Finished.store(true);
		}
	};

	inline StartupResult PlayStartupVideo(GLFWwindow* window, const std::filesystem::path& path,
		const std::function<bool(bool)>& preload)
	{
		const HWND parent = glfwGetWin32Window(window);
		const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if (FAILED(com) && com != RPC_E_CHANGED_MODE) return StartupResult::Failed;
		RECT area{};
		GetClientRect(parent, &area);
		WNDCLASSW cls{};
		cls.hInstance = GetModuleHandleW(nullptr);
		cls.lpfnWndProc = DefWindowProcW;
		cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
		cls.lpszClassName = L"TomCatStartupVideo";
		RegisterClassW(&cls);
		const HWND canvas = CreateWindowExW(0, cls.lpszClassName, L"", WS_CHILD | WS_VISIBLE,
			0, 0, area.right, area.bottom, parent, nullptr, cls.hInstance, nullptr);
		if (canvas) UpdateWindow(canvas);
		glfwShowWindow(window);
		auto events = Microsoft::WRL::Make<StartupPlaybackEvents>();
		Microsoft::WRL::ComPtr<IMFPMediaPlayer> player;
		HRESULT hr = canvas && events ? MFPCreateMediaPlayer(path.c_str(), FALSE,
			MFP_OPTION_FREE_THREADED_CALLBACK, events.Get(), canvas, &player) : E_FAIL;
		auto result = StartupResult::Failed;
		auto lastProgress = GetTickCount64();
		LONGLONG previous = -1, duration = 0;
		while (SUCCEEDED(hr))
		{
			glfwWaitEventsTimeout(0.012);
			if (glfwWindowShouldClose(window)) { result = StartupResult::Closed; break; }
			hr = events->Error.load();
			if (FAILED(hr)) break;
			if (events->Ready.exchange(false))
			{
				hr = player->SetBorderColor(RGB(0, 0, 0));
				if (SUCCEEDED(hr)) hr = player->SetAspectRatioMode(MFVideoARMode_PreservePicture);
				if (SUCCEEDED(hr)) hr = player->Play();
				if (FAILED(hr)) break;
			}
			if (events->Finished.load())
			{
				result = StartupResult::Completed;
				break;
			}
			if (events->Playing.load())
			{
				if (duration == 0)
				{
					PROPVARIANT length{};
					if (SUCCEEDED(player->GetDuration(MFP_POSITIONTYPE_100NS, &length)) && (length.vt == VT_I8 || length.vt == VT_UI8))
						duration = length.hVal.QuadPart;
					PropVariantClear(&length);
				}
				// Initialize the first scene near the end so gameplay does not
				// advance throughout a long custom movie.
				if (!preload(duration <= 0 || previous < 0 || duration - previous < 15000000))
				{ result = StartupResult::LoadFailed; break; }
			}
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
				&& (position.vt == VT_I8 || position.vt == VT_UI8) && position.hVal.QuadPart != previous)
			{
				previous = position.hVal.QuadPart;
				lastProgress = GetTickCount64();
			}
			PropVariantClear(&position);
			if (GetTickCount64() - lastProgress > 15000) { hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT); break; }
		}
		if (canvas) ShowWindow(canvas, SW_HIDE);
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
