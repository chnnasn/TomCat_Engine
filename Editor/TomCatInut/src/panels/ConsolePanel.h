#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace TomCat {

	enum class ConsoleMessageSeverity : uint8_t
	{
		Trace = 0,
		Info,
		Warning,
		Error
	};

	// Diagnostics are kept as structured data so compiler locations and managed
	// stack traces remain useful to the UI instead of being flattened into a log
	// line. Push() is safe to call from a background compiler or runtime thread.
	struct ConsoleMessage
	{
		uint64_t Sequence = 0;
		ConsoleMessageSeverity Severity = ConsoleMessageSeverity::Info;
		std::string Source;
		std::string Code;
		std::string Text;
		std::filesystem::path File;
		uint32_t Line = 0;
		uint32_t Column = 0;
		std::string StackTrace;
	};

	class ConsolePanel
	{
	public:
		void Push(ConsoleMessage message);
		void Push(ConsoleMessageSeverity severity, std::string text,
			std::string source = {});
        void SetOpenSourceCallback(std::function<void(const std::filesystem::path&)> callback) { m_OpenSource=std::move(callback); }
        void Clear();
		void OnPlayStarted();
		std::vector<ConsoleMessage> Snapshot() const;

		void OnImGuiRender(bool* open = nullptr);
		bool IsFocused() const { return m_Focused; }
		bool IsDocked() const { return m_Docked; }

	private:
		bool IsVisible(ConsoleMessageSeverity severity) const;

	private:
        std::function<void(const std::filesystem::path&)> m_OpenSource;
		mutable std::mutex m_Mutex;
		std::vector<ConsoleMessage> m_Messages;
		uint64_t m_NextSequence = 1;
		bool m_ShowTrace = true;
		bool m_ShowInfo = true;
		bool m_ShowWarnings = true;
		bool m_ShowErrors = true;
        char m_Search[256]{};
        uint64_t m_SelectedSequence = 0;
		bool m_Collapse = false;
		bool m_ClearOnPlay = true;
		bool m_AutoScroll = true;
		bool m_ScrollToBottom = false;
		bool m_Focused = false;
		bool m_Docked = true;
	};

}
