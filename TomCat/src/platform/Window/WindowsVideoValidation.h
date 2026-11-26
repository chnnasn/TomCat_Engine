#pragma once
#include <filesystem>
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace TomCat {
	inline bool IsPlayableStartupVideo(const std::filesystem::path& path)
	{
		const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if (FAILED(com) && com != RPC_E_CHANGED_MODE) return false;
		bool valid = false;
		if (SUCCEEDED(MFStartup(MF_VERSION)))
		{
			using Microsoft::WRL::ComPtr;
			ComPtr<IMFSourceReader> reader;
			ComPtr<IMFMediaType> decoded;
			if (SUCCEEDED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader))
				&& SUCCEEDED(MFCreateMediaType(&decoded))
				&& SUCCEEDED(decoded->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video))
				&& SUCCEEDED(decoded->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12))
				&& SUCCEEDED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, decoded.Get())))
			{
				DWORD flags = 0;
				ComPtr<IMFSample> sample;
				while (!sample && !(flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)))
				{
					if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr,
						&flags, nullptr, sample.ReleaseAndGetAddressOf()))) break;
				}
				valid = sample != nullptr;
			}
			reader.Reset();
			decoded.Reset();
			MFShutdown();
		}
		if (SUCCEEDED(com)) CoUninitialize();
		return valid;
	}
}
