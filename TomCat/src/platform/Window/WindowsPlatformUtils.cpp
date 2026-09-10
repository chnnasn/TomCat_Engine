#include "tcpch.h"
#include "TomCat/Utils/PlatformUtils.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <shobjidl.h>
#include <wrl/client.h>

#include "TomCat/Core/Application.h"

#pragma comment(lib, "Ole32.lib")

namespace TomCat {
	namespace {

		class ScopedCOM
		{
		public:
			ScopedCOM()
				: m_Result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))
			{
			}

			~ScopedCOM()
			{
				if (SUCCEEDED(m_Result))
					CoUninitialize();
			}

			bool IsUsable() const { return SUCCEEDED(m_Result) || m_Result == RPC_E_CHANGED_MODE; }

		private:
			HRESULT m_Result;
		};

		std::wstring ToWide(const std::string& text)
		{
			if (text.empty())
				return {};

			UINT codePage = CP_UTF8;
			DWORD flags = MB_ERR_INVALID_CHARS;
			int length = MultiByteToWideChar(codePage, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
			if (length == 0)
			{
				codePage = CP_ACP;
				flags = 0;
				length = MultiByteToWideChar(codePage, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
			}
			if (length == 0)
				return {};

			std::wstring result(static_cast<size_t>(length), L'\0');
			MultiByteToWideChar(codePage, flags, text.data(), static_cast<int>(text.size()), result.data(), length);
			return result;
		}

		struct DialogFilter
		{
			std::wstring Name;
			std::wstring Pattern;
			std::wstring DefaultExtension;
		};

		DialogFilter ParseFilter(const char* filter)
		{
			const std::string name = filter && *filter ? std::string(filter) : "All files (*.*)";
			std::string pattern = "*.*";

			const size_t openingParenthesis = name.rfind('(');
			const size_t closingParenthesis = name.find(')', openingParenthesis);
			if (openingParenthesis != std::string::npos && closingParenthesis != std::string::npos &&
				closingParenthesis > openingParenthesis + 1)
			{
				const std::string candidate = name.substr(openingParenthesis + 1, closingParenthesis - openingParenthesis - 1);
				if (candidate.find('*') != std::string::npos)
					pattern = candidate;
			}

			std::string extension;
			const size_t extensionStart = pattern.find("*.");
			if (extensionStart != std::string::npos)
			{
				const size_t extensionEnd = pattern.find(';', extensionStart);
				extension = pattern.substr(extensionStart + 2, extensionEnd - (extensionStart + 2));
				if (extension.find_first_of("*?") != std::string::npos)
					extension.clear();
			}

			return { ToWide(name), ToWide(pattern), ToWide(extension) };
		}

		HWND GetOwnerWindow()
		{
			return glfwGetWin32Window(static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow()));
		}

		std::filesystem::path ShowDialog(const char* filter, bool save, bool pickFolder)
		{
			ScopedCOM com;
			if (!com.IsUsable())
			{
				TC_Core_Error("Could not initialize COM for the file dialog");
				return {};
			}

			Microsoft::WRL::ComPtr<IFileDialog> dialog;
			const CLSID& dialogClass = save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
			HRESULT result = CoCreateInstance(dialogClass, nullptr, CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(dialog.GetAddressOf()));
			if (FAILED(result))
			{
				TC_Core_Error("Could not create the Windows file dialog (HRESULT 0x{0:X})", static_cast<uint32_t>(result));
				return {};
			}

			FILEOPENDIALOGOPTIONS options = 0;
			dialog->GetOptions(&options);
			options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
			if (pickFolder)
				options |= FOS_PICKFOLDERS;
			else if (save)
				options |= FOS_OVERWRITEPROMPT;
			else
				options |= FOS_FILEMUSTEXIST;
			dialog->SetOptions(options);

			DialogFilter parsedFilter;
			COMDLG_FILTERSPEC filterSpec{};
			if (!pickFolder)
			{
				parsedFilter = ParseFilter(filter);
				filterSpec = { parsedFilter.Name.c_str(), parsedFilter.Pattern.c_str() };
				dialog->SetFileTypes(1, &filterSpec);
				dialog->SetFileTypeIndex(1);
				if (save && !parsedFilter.DefaultExtension.empty())
					dialog->SetDefaultExtension(parsedFilter.DefaultExtension.c_str());
			}

			result = dialog->Show(GetOwnerWindow());
			if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
				return {};
			if (FAILED(result))
			{
				TC_Core_Warn("Windows file dialog failed (HRESULT 0x{0:X})", static_cast<uint32_t>(result));
				return {};
			}

			Microsoft::WRL::ComPtr<IShellItem> item;
			if (FAILED(dialog->GetResult(item.GetAddressOf())))
				return {};

			PWSTR widePath = nullptr;
			if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &widePath)))
				return {};

			const std::filesystem::path path(widePath);
			CoTaskMemFree(widePath);
			return path;
		}

	}

	std::filesystem::path FileDialogs::OpenFile(const char* filter)
	{
		return ShowDialog(filter, false, false);
	}

	std::filesystem::path FileDialogs::SaveFile(const char* filter)
	{
		return ShowDialog(filter, true, false);
	}

	std::filesystem::path FileDialogs::OpenFolder()
	{
		return ShowDialog(nullptr, false, true);
	}
}

