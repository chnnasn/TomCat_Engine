#pragma once

#include <filesystem>
#include "TomCat/Renderer/Texture.h"

namespace TomCat {

	class ContentBrowserPanel
	{
	public:
		enum LayoutMode
		{
			OneColumn,
			TwoColumn
		};

		ContentBrowserPanel();

		void OnImGuiRender();
	private:
		std::filesystem::path m_CurrentDirectory;
		std::filesystem::path m_SelectedDirectory; // 存储在树状视图中选中的目录

		Ref<Texture2D> m_DirectoryIcon;
		Ref<Texture2D> m_FileIcon;

		// 缓存图片预览，避免重复加载
		std::unordered_map<std::string, Ref<Texture2D>> m_ImageCache;

		LayoutMode m_LayoutMode;

		// 递归函数，用于显示多级目录结构
		void DisplayDirectoryRecursive(const std::filesystem::path& directoryPath, bool isRoot);
	};
}

