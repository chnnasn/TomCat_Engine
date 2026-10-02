#pragma once

#include "TomCat/Core/Layer.h"
#include "TomCat/Events/KeyEvent.h"
#include "TomCat/Events/MouseEvent.h"
#include "TomCat/Events/ApplicationEvent.h"
#include <memory>
struct ImGuiStyle;

namespace TomCat {

	class ImGuiLayer : public Layer
	{
	public:
		explicit ImGuiLayer(bool editorStyling = false);
		~ImGuiLayer();

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnEvent(Event& e) override;

		 void Begin() ;
		 void End();

		void BlockEvents(bool block)
		{
			m_BlockMouseEvents = block;
			m_BlockKeyboardEvents = block;
		}
		void BlockMouseEvents(bool block) { m_BlockMouseEvents = block; }
		void BlockKeyboardEvents(bool block) { m_BlockKeyboardEvents = block; }

		 void SetDarkThemeColors();

		// Display scale of the surface the interface is drawn on: the device pixel
		// ratio on the web, the window DPI scale on the desktop. Fonts are baked and
		// ImGuiStyle is scaled by this value, so interface units stay DPI independent.
		// Returns false while the authored style has not been captured yet, in which
		// case the scale is recorded but nothing could be applied.
		bool SetUiScale(float scale);
		float GetUiScale() const { return m_DpiScale; }
		// The editor preference multiplier applied on top of the display scale.
		void SetUserScale(float scale);
		float GetUserScale() const { return m_UserScale; }
		// Reported for the host and for regression checks: the authored size in interface
		// units, and the size the atlas was actually rasterised at.
		float GetBaseFontSize() const { return m_BaseFontSize; }
		float GetBakedFontSize() const { return m_BakedFontSize; }
		float GetEffectiveScale() const { return EffectiveScale(); }
	private:
		void PrepareImGuiStyle();
		// Effective scale = display scale x user preference, clamped.
		float EffectiveScale() const;
		// Applies a scale change at the next frame boundary instead of mid-frame.
		void RequestFontRebuild();
		// Rebuilds the font atlas at the current scale and re-creates its GPU
		// texture. Must run outside an ImGui frame.
		void RebuildFonts();
		bool m_EditorStyling = false;
        bool m_BlockMouseEvents = true;
		bool m_BlockKeyboardEvents = true;
		// Kept apart so each source can change without clobbering the other.
		float m_DpiScale = 1.0f;
		float m_UserScale = 1.0f;
		std::unique_ptr<ImGuiStyle> m_BaseStyle;
		bool m_BaseStyleCaptured = false;
		bool m_FontRebuildPending = false;
		float m_BaseFontSize = 0.0f;
		float m_BakedFontSize = 0.0f;
	};


}
