#include "tcpch.h"
#include "ScriptDiagnosticSink.h"

#include "TomCat/Core/Log.h"

#include <mutex>
#include <utility>

namespace TomCat::Scripting {

	namespace {
		std::mutex s_SinkMutex;
		ScriptDiagnosticSink s_Sink;
	}

	void SetScriptDiagnosticSink(ScriptDiagnosticSink sink)
	{
		std::lock_guard<std::mutex> lock(s_SinkMutex);
		s_Sink = std::move(sink);
	}

	void PublishScriptDiagnostic(const ScriptDiagnostic& diagnostic) noexcept
	{
		// Invoke under the registration lock. Unregistration therefore acts as a
		// lifetime barrier for callbacks that capture an Editor panel by reference.
		std::lock_guard<std::mutex> lock(s_SinkMutex);
		if (!s_Sink)
			return;
		try
		{
			s_Sink(diagnostic);
		}
		catch (const std::exception& error)
		{
			TC_Core_Error("Script diagnostic sink failed: {0}", error.what());
		}
		catch (...)
		{
			TC_Core_Error("Script diagnostic sink failed with an unknown exception");
		}
	}

}
