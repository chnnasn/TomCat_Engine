#pragma once

// Build Settings panel and Player build pipeline, extracted from
// EditorLayer. Status strings live in EditorBuildState, shared with the
// facade so project-load validation can seed the same surface.

#include "TomCat/Core/Base.h"

#include <string>

namespace TomCat {

	class EditorLayer;

	struct EditorBuildState
	{
		std::string BuildSettingsStatus;
		bool BuildSettingsSucceeded = false;
		std::string PlayerBuildStatus;
		bool PlayerBuildSucceeded = false;
        bool DevelopmentBuild = false;
	};

	class EditorBuildController
	{
		friend class EditorLayer;
	public:
		EditorBuildController(EditorBuildState& state, EditorLayer& layer)
			: m_BuildState(state), m_Layer(layer) {}

		void UI_BuildSettings();
		bool BuildPlayer(bool runAfterBuild);

	private:
		EditorBuildState& m_BuildState;
		EditorLayer& m_Layer;
	};
}
