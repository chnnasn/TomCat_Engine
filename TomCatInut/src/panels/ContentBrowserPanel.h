#pragma once

#include <filesystem>
#include "TomCat/Renderer/Texture.h"

namespace TomCat {

	class ContentBrowserPanel
{
public:
	ContentBrowserPanel();

	void OnImGuiRender();
private:
	std::filesystem::path m_CurrentDirectory;

	Ref<Texture2D> m_DirectoryIcon;
	Ref<Texture2D> m_FileIcon;

	// 缓存图片预览，避免重复加载
	std::unordered_map<std::string, Ref<Texture2D>> m_ImageCache;
};
}

