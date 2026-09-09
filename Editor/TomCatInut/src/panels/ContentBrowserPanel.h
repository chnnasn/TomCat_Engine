#pragma once

#include <filesystem>
#include <string>
#include "TomCat/Renderer/Texture.h"
#include "TomCat/Project/Project.h"

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
		void SetProject(Ref<Project> project);
		void Serialize();

		// Editor-level layout setting (persisted inside the active user's
		// UserSettings/imgui.ini, never in the project root).
		void LoadLayoutSetting();
		bool SaveLayoutSetting();

		void OnImGuiRender();
	private:
		std::filesystem::path m_CurrentDirectory;
		std::filesystem::path m_SelectedDirectory;

		Ref<Texture2D> m_DirectoryIcon;
		Ref<Texture2D> m_FileIcon;

		std::unordered_map<std::string, Ref<Texture2D>> m_ImageCache;

		LayoutMode m_LayoutMode;
		Ref<Project> m_Project;
		std::filesystem::path m_LayoutIniPath;

		// 存储树节点的打开状态
		std::unordered_set<std::string> m_ExpandedNodes;
		// 存储Two Column模式下当前打开的文件夹
		std::filesystem::path m_TwoColumnCurrentFolder;

		void DisplayFileNode(const std::filesystem::path& path);
		void DisplayDirectoryRecursive(const std::filesystem::path& directoryPath, bool isRoot);
		void DisplayDirectoryFlat(const std::filesystem::path& directoryPath);
	};
}

