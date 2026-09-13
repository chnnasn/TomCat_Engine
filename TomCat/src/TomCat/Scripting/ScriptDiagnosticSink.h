#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace TomCat::Scripting {

	enum class ScriptDiagnosticSeverity : uint8_t
	{
		Info = 0,
		Warning,
		Error
	};

	struct ScriptDiagnostic
	{
		ScriptDiagnosticSeverity Severity = ScriptDiagnosticSeverity::Info;
		std::string Message;
		std::filesystem::path File;
		uint32_t Line = 0;
		uint32_t Column = 0;
	};

	using ScriptDiagnosticSink = std::function<void(const ScriptDiagnostic&)>;

	// There is one process-local consumer. SetScriptDiagnosticSink({}) waits for
	// an in-flight callback before returning, making EditorLayer teardown safe even
	// when diagnostics originate on a managed worker thread.
	void SetScriptDiagnosticSink(ScriptDiagnosticSink sink);
	void PublishScriptDiagnostic(const ScriptDiagnostic& diagnostic) noexcept;

}
