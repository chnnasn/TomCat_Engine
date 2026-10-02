#pragma once

// File-local helpers shared by EditorLayer and its extracted services.
// Extracted verbatim from EditorLayer.cpp; private to Editor/TomCatInut/src.

#include "EditorLayer.h"
#include "EditorPlayToolbar.h"
#include "SceneToolbarDrawing.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
#endif

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scene/Advanced2D.h"
#include "TomCat/Renderer/Font.h"
#include "TomCat/Scene/Serialization/SceneArchiveCodec.h"
#include "TomCat/Scene/Serialization/PrefabArchiveCodec.h"
#include "TomCat/Scene/Serialization/PrefabLink.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Core/ApplicationPaths.h"
#include "TomCat/Editor/EditorShortcutRouter.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PlatformUtils.h"
#include "TomCat/Utils/PathUtils.h"
#include "TomCat/Project/ProjectManager.h"
#include "TomCat/Scripting/ManagedRuntimeFactory.h"
#include "TomCat/Scripting/ScriptDiagnosticSink.h"
#include "TomCat/Scripting/ScriptEngine.h"
#include "TomCat/Runtime/RuntimeUI.h"

#include "Player/PlayerBuilder.h"
#include "ImGuizmo.h"

namespace TomCat {

	extern const std::filesystem::path g_AssetPath;

namespace EditorLayerDetail {

	namespace {
		constexpr float kDockedPanelMinimumWidthRatio = 0.08f;
		constexpr float kDockedPanelCompactMinimumWidth = 96.0f;
		constexpr float kDockedPanelExpandedMinimumWidth = 220.0f;
		constexpr std::array<const char*, 10> kMaximizableDockPanels = {
			"Scene###Scene", "Game", "Hierarchy", "Inspector", "Project", "Console",
			"Animation", "Animator", "Tile Palette", "Profiler"
		};

		struct GameViewResolutionPreset
		{
			const char* Label;
			uint32_t Width;
			uint32_t Height;
		};

		constexpr std::array<GameViewResolutionPreset, 5> kGameViewResolutions = { {
			{ "Free Aspect", 0, 0 },
			{ "HD (1280x720)", 1280, 720 },
			{ "Full HD (1920x1080)", 1920, 1080 },
			{ "QHD (2560x1440)", 2560, 1440 },
			{ "4K UHD (3840x2160)", 3840, 2160 }
		} };
		constexpr float kGameViewScaleMinimum = 0.8f;
		constexpr float kGameViewScaleMaximum = 8.8f;
		constexpr float kGameViewScaleWheelStep = 0.1f;

		bool DrawGameViewScaleSlider(float& value, float controlWidth)
		{
			const float controlHeight = ImGui::GetFrameHeight();
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const ImVec2 labelSize = ImGui::CalcTextSize("Scale");
			const float trackStartOffset = 8.0f + labelSize.x + 14.0f;
			const float trackEndOffset = std::min(trackStartOffset + 141.0f,
				controlWidth - 55.0f);

			ImGui::InvisibleButton("##GameViewScale", ImVec2(controlWidth, controlHeight));
			bool changed = false;
			if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				const float mouse = ImGui::GetIO().MousePos.x;
				const float normalized = std::clamp((mouse - origin.x - trackStartOffset)
					/ (trackEndOffset - trackStartOffset), 0.0f, 1.0f);
				const float next = kGameViewScaleMinimum
					+ normalized * (kGameViewScaleMaximum - kGameViewScaleMinimum);
				if (std::abs(next - value) > 0.0001f)
				{
					value = next;
					changed = true;
				}
			}

			value = std::clamp(value, kGameViewScaleMinimum,
				kGameViewScaleMaximum);
			const float normalized = (value - kGameViewScaleMinimum)
				/ (kGameViewScaleMaximum - kGameViewScaleMinimum);
			const float centerY = origin.y + controlHeight * 0.5f;
			const float trackStart = origin.x + trackStartOffset;
			const float trackEnd = origin.x + trackEndOffset;
			const float grabX = trackStart + (trackEnd - trackStart) * normalized;
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImGuiCol backgroundColor = ImGui::IsItemActive()
				? ImGuiCol_ButtonActive
				: (ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Tab);
			drawList->AddRectFilled(origin,
				ImVec2(origin.x + controlWidth, origin.y + controlHeight),
				ImGui::GetColorU32(backgroundColor));
			drawList->AddLine(ImVec2(origin.x, origin.y + controlHeight),
				ImVec2(origin.x + controlWidth, origin.y + controlHeight),
				ImGui::GetColorU32(ImGuiCol_BorderShadow));
			drawList->AddText(ImVec2(origin.x + 8.0f,
				centerY - labelSize.y * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), "Scale");
			drawList->AddRectFilled(ImVec2(trackStart, centerY - 2.0f),
				ImVec2(trackEnd, centerY + 2.0f),
				IM_COL32(92, 92, 92, 255), 2.0f);
			const ImU32 grabColor = ImGui::IsItemActive()
				? IM_COL32(215, 215, 215, 255)
				: (ImGui::IsItemHovered()
					? IM_COL32(180, 180, 180, 255)
					: IM_COL32(155, 155, 155, 255));
			drawList->AddCircleFilled(ImVec2(grabX, centerY), 7.0f, grabColor);
			drawList->AddCircle(ImVec2(grabX, centerY), 7.0f,
				IM_COL32(70, 70, 70, 255), 0, 1.0f);

			char valueText[16]{};
			if (std::abs(value - std::round(value)) < 0.01f)
				std::snprintf(valueText, sizeof(valueText), "%.0fx", value);
			else
				std::snprintf(valueText, sizeof(valueText), "%.1fx", value);
			const ImVec2 valueSize = ImGui::CalcTextSize(valueText);
			drawList->AddText(ImVec2(origin.x + trackEndOffset + 13.0f,
				centerY - valueSize.y * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), valueText);
			return changed;
		}

		ImTextureID ToImGuiTextureID(const Ref<Texture2D>& texture)
		{
			return texture
				? reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture->GetRendererID()))
				: nullptr;
		}

		void DrawEditorIcon(ImDrawList* drawList, const Ref<EditorIconSet>& icons,
			EditorIcon icon, const ImVec2& minimum, const ImVec2& maximum,
			ImU32 tint = IM_COL32_WHITE)
		{
			if (!drawList || !icons)
				return;
			const Ref<Texture2D>& texture = icons->Get(icon);
			if (!texture)
				return;
			drawList->AddImage(ToImGuiTextureID(texture), minimum, maximum,
				ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), tint);
		}

		uint32_t ToFramebufferExtent(float value,
			float screenToFramebufferScale = 1.0f)
		{
			if (!std::isfinite(value) || value <= 0.0f
				|| !std::isfinite(screenToFramebufferScale)
				|| screenToFramebufferScale <= 0.0f)
				return 0;
			return static_cast<uint32_t>(std::round(std::clamp(
				value * screenToFramebufferScale, 1.0f,
				static_cast<float>(Framebuffer::MaxFramebufferSize))));
		}

		std::filesystem::path AbsoluteLexicalPath(const std::filesystem::path& path)
		{
			if (path.empty())
				return {};
			std::error_code error;
			const std::filesystem::path absolute = std::filesystem::absolute(path, error);
			return (error ? path : absolute).lexically_normal();
		}

		std::string SanitizePrefabFileStem(std::string value)
		{
			for (char& character : value)
			{
				const unsigned char byte = static_cast<unsigned char>(character);
				if (byte < 0x20 || character == '<' || character == '>'
					|| character == ':' || character == '"' || character == '/'
					|| character == '\\' || character == '|' || character == '?'
					|| character == '*')
					character = '_';
			}
			while (!value.empty() && (value.front() == ' ' || value.front() == '.'))
				value.erase(value.begin());
			while (!value.empty() && (value.back() == ' ' || value.back() == '.'))
				value.pop_back();
			if (value.empty())
				value = "Prefab";

			std::string deviceName = value.substr(0, value.find('.'));
			std::transform(deviceName.begin(), deviceName.end(), deviceName.begin(),
				[](unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});
			bool reserved = deviceName == "con" || deviceName == "prn"
				|| deviceName == "aux" || deviceName == "nul";
			if (!reserved && deviceName.size() == 4
				&& (deviceName.rfind("com", 0) == 0
					|| deviceName.rfind("lpt", 0) == 0)
				&& deviceName[3] >= '1' && deviceName[3] <= '9')
				reserved = true;
			if (reserved)
				value.insert(value.begin(), '_');
			return value;
		}

		std::filesystem::path MakeUniquePrefabPath(
			const std::filesystem::path& directory, const std::string& entityName)
		{
			const std::string stem = SanitizePrefabFileStem(entityName);
			for (uint32_t index = 0; index < 10000; ++index)
			{
				std::string fileName = stem;
				if (index > 0)
					fileName += " (" + std::to_string(index) + ")";
				const std::filesystem::path candidate =
					directory / UTF8ToPath(fileName + ".tcprefab");
				std::error_code error;
				const bool assetExists = std::filesystem::exists(candidate, error);
				if (error)
					return {};
				const bool metadataExists = std::filesystem::exists(
					AssetRegistry::GetMetadataPath(candidate), error);
				if (error)
					return {};
				if (!assetExists && !metadataExists)
					return candidate;
			}
			return {};
		}

		bool TryGetRelativeWithin(const std::filesystem::path& root,
			const std::filesystem::path& candidate, std::filesystem::path& relative)
		{
			const std::filesystem::path normalizedRoot = AbsoluteLexicalPath(root);
			const std::filesystem::path normalizedCandidate = AbsoluteLexicalPath(candidate);
			if (normalizedRoot.empty() || normalizedCandidate.empty())
				return false;
			relative = normalizedCandidate.lexically_relative(normalizedRoot);
			if (relative.empty() || relative.is_absolute())
				return false;
			for (const auto& part : relative)
			{
				if (part == "..")
					return false;
			}
			if (relative == ".")
				relative.clear();
			return true;
		}

		std::string TrimASCIIWhitespace(std::string value)
		{
			auto isWhitespace = [](unsigned char character) { return std::isspace(character) != 0; };
			value.erase(value.begin(), std::find_if(value.begin(), value.end(),
				[&](unsigned char character) { return !isWhitespace(character); }));
			value.erase(std::find_if(value.rbegin(), value.rend(),
				[&](unsigned char character) { return !isWhitespace(character); }).base(), value.end());
			return value;
		}

		bool IsImGuiManagedIniSection(const std::string& header)
		{
			return header.rfind("[Window][", 0) == 0 ||
				header.rfind("[Table][", 0) == 0 ||
				header.rfind("[Docking][", 0) == 0;
		}

		std::string::size_type FindIniSectionHeader(const std::string& ini,
			std::string_view sectionName)
		{
			std::string::size_type position = 0;
			while ((position = ini.find(sectionName, position)) != std::string::npos)
			{
				const bool lineStart = position == 0 || ini[position - 1] == '\n';
				const size_t end = position + sectionName.size();
				const bool lineEnd = end == ini.size() || ini[end] == '\n' || ini[end] == '\r';
				if (lineStart && lineEnd)
					return position;
				position = end;
			}
			return std::string::npos;
		}

		std::filesystem::path GetDefaultEditorLayoutPath()
		{
			std::error_code error;
			const std::filesystem::path currentDirectory = std::filesystem::current_path(error);
			return error ? std::filesystem::path("imgui.ini") : currentDirectory / "imgui.ini";
		}

		std::filesystem::path GetEditorLayoutPath(const Ref<Project>& project)
		{
			if (project)
			{
				if (project->GetProjectPath().empty())
				{
					TC_Core_Error("Cannot resolve an Editor layout for a project with an empty path");
					return {};
				}
				return project->GetProjectPath().parent_path() / "UserSettings" / "imgui.ini";
			}

			const std::optional<std::filesystem::path> settingsRoot =
				ApplicationPaths::GetProductDataRoot(ApplicationProduct::Editor);
			return settingsRoot ? *settingsRoot / "editor-layout.ini" : std::filesystem::path{};
		}

		bool EnsureSettingsDirectory(const std::filesystem::path& settingsPath)
		{
			if (settingsPath.empty())
				return false;
			const std::filesystem::path directory = settingsPath.parent_path();
			if (directory.empty())
				return true;

			std::error_code error;
			std::filesystem::create_directories(directory, error);
			if (!error)
				return true;

			TC_Core_Error("Failed to create editor settings directory '{0}': {1}",
				PathToUTF8(directory), error.message());
			return false;
		}

		bool LoadImGuiSettings(const std::filesystem::path& path)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
				return false;
			std::ostringstream contents;
			contents << input.rdbuf();
			if (input.bad())
				return false;
			const std::string settings = contents.str();
			std::istringstream lines(settings);
			std::string line;
			bool hasManagedSection = false;
			while (std::getline(lines, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (IsImGuiManagedIniSection(line))
				{
					hasManagedSection = true;
					break;
				}
			}
			// A user file may initially contain only custom toolbar/browser sections.
			// Loading that through ImGui would clear the default Docking settings.
			if (!hasManagedSection)
				return false;
			ImGui::LoadIniSettingsFromMemory(settings.data(), settings.size());
			return true;
		}

		bool ReadCustomIniSections(const std::filesystem::path& path,
			std::vector<std::string>& sections)
		{
			sections.clear();
			std::error_code existsError;
			const bool exists = std::filesystem::exists(path, existsError);
			if (existsError)
			{
				TC_Core_Error("Could not inspect ImGui settings '{0}': {1}",
					PathToUTF8(path), existsError.message());
				return false;
			}

			std::ifstream input(path, std::ios::binary);
			if (exists && !input)
			{
				TC_Core_Error("Could not read ImGui settings '{0}'", PathToUTF8(path));
				return false;
			}
			if (!input)
				return true;
			std::ostringstream contents;
			contents << input.rdbuf();
			if (input.bad())
			{
				TC_Core_Error("Failed while reading ImGui settings '{0}'", PathToUTF8(path));
				return false;
			}
			const std::string ini = contents.str();

			std::string::size_type sectionStart = 0;
			while (sectionStart < ini.size())
			{
				if (ini[sectionStart] != '[' || (sectionStart > 0 && ini[sectionStart - 1] != '\n'))
				{
					sectionStart = ini.find("\n[", sectionStart);
					if (sectionStart == std::string::npos)
						break;
					++sectionStart;
				}
				const std::string::size_type headerEnd = ini.find('\n', sectionStart);
				const std::string header = ini.substr(sectionStart,
					headerEnd == std::string::npos ? std::string::npos : headerEnd - sectionStart);
				const std::string::size_type nextMarker = headerEnd == std::string::npos
					? std::string::npos : ini.find("\n[", headerEnd);
				const std::string::size_type sectionEnd = nextMarker == std::string::npos
					? ini.size() : nextMarker + 1;
				if (!IsImGuiManagedIniSection(header))
					sections.push_back(ini.substr(sectionStart, sectionEnd - sectionStart));
				if (nextMarker == std::string::npos)
					break;
				sectionStart = nextMarker + 1;
			}
			return true;
		}

		bool SaveImGuiSettingsPreservingCustomSections(const std::filesystem::path& path)
		{
			if (!EnsureSettingsDirectory(path))
				return false;

			std::vector<std::string> customSections;
			if (!ReadCustomIniSections(path, customSections))
				return false;
			size_t size = 0;
			const char* settings = ImGui::SaveIniSettingsToMemory(&size);
			if (!settings)
				return false;

			std::string ini(settings, size);
			for (const std::string& section : customSections)
			{
				if (!ini.empty() && ini.back() != '\n')
					ini.push_back('\n');
				ini += section;
			}
			std::string writeError;
			if (FileSystem::WriteFileAtomically(path, ini, writeError))
				return true;
			TC_Core_Error("Failed to save ImGui settings '{0}': {1}", PathToUTF8(path), writeError);
			return false;
		}


		bool ReadBinaryFileForBuild(const std::filesystem::path& path,
			std::vector<uint8_t>& bytes, std::string& errorMessage)
		{
			bytes.clear();
			std::error_code error;
			const uintmax_t fileSize = std::filesystem::file_size(path, error);
			if (error || fileSize > static_cast<uintmax_t>((std::numeric_limits<size_t>::max)()))
			{
				errorMessage = "Could not inspect '" + PathToUTF8(path) + "': " +
					(error ? error.message() : "file is too large");
				return false;
			}
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				errorMessage = "Could not open '" + PathToUTF8(path) + "'.";
				return false;
			}
			bytes.resize(static_cast<size_t>(fileSize));
			if (!bytes.empty())
				input.read(reinterpret_cast<char*>(bytes.data()),
					static_cast<std::streamsize>(bytes.size()));
			if (!input || input.peek() != std::char_traits<char>::eof())
			{
				errorMessage = "Could not read '" + PathToUTF8(path) + "' completely.";
				bytes.clear();
				return false;
			}
			return true;
		}


	}

}
}
