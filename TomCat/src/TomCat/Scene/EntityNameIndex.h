#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace TomCat {

	// A scoped index, not a persistent Scene cache: native authoring code can
	// mutate Tag strings directly. Scene copies own one while creating identities;
	// deferred projections own one alongside their controlled metadata state.
	class EntityNameIndex
	{
	public:
		void Reserve(size_t count) { m_Names.reserve(count); }
		void Add(const std::string& name) { ++m_Names[name]; }
		void Remove(const std::string& name)
		{
			const auto found = m_Names.find(name);
			if (found == m_Names.end())
				return;
			if (--found->second == 0)
			{
				m_Names.erase(found);
				// A removed name can be a hole for a nested base such as "Arc (1)".
				// Reset cursors rather than parsing user-authored suffix strings.
				m_NextSuffix.clear();
			}
		}
		std::string MakeUnique(const std::string& requestedName)
		{
			const std::string base = requestedName.empty() ? "Entity" : requestedName;
			++m_Probes;
			if (!m_Names.contains(base))
				return base;
			auto& suffix = m_NextSuffix[base];
			if (suffix == 0)
				suffix = 1;
			for (;; ++suffix)
			{
				std::string candidate = base + " (" + std::to_string(suffix) + ")";
				++m_Probes;
				if (!m_Names.contains(candidate))
					return candidate;
			}
		}
		uint64_t GetProbeCount() const { return m_Probes; }
	private:
		std::unordered_map<std::string, size_t> m_Names;
		std::unordered_map<std::string, uint32_t> m_NextSuffix;
		uint64_t m_Probes = 0;
	};
}
