#include "tcpch.h"
#include "SceneHistory.h"

#include <algorithm>
#include <utility>

namespace TomCat {

	SceneHistory::SceneHistory()
		: SceneHistory(Limits{})
	{
	}

	SceneHistory::SceneHistory(Limits limits)
		: m_Limits(limits)
	{
		m_Limits.MaximumEntries = std::max<size_t>(1, m_Limits.MaximumEntries);
		m_Limits.MaximumBytes = std::max<size_t>(1, m_Limits.MaximumBytes);
	}

	bool SceneHistory::Reset(std::string archive, uint64_t selectedEntity,
		bool isSaved)
	{
		m_Entries.clear();
		m_Cursor = 0;
		m_MemoryUsage = 0;
		m_TransactionActive = false;
		m_TransactionLabel.clear();

		Snapshot initial;
		initial.Id = m_NextStateId++;
		initial.Archive = std::make_shared<const std::string>(std::move(archive));
		initial.SelectedEntity = selectedEntity;
		initial.Label = "Initial Scene";
		m_MemoryUsage = Measure(initial);
		m_Entries.push_back(std::move(initial));
		m_SavedStateId = isSaved ? m_Entries.front().Id : 0;
		EnforceLimits();
		return true;
	}

	bool SceneHistory::BeginTransaction(std::string label)
	{
		if (m_TransactionActive || m_Entries.empty())
			return false;
		m_TransactionActive = true;
		m_TransactionLabel = label.empty() ? "Scene Edit" : std::move(label);
		return true;
	}

	bool SceneHistory::CommitTransaction(std::string archive,
		uint64_t selectedEntity)
	{
		if (!m_TransactionActive || m_Entries.empty())
			return false;

		m_TransactionActive = false;
		std::string label = std::move(m_TransactionLabel);
		m_TransactionLabel.clear();
		const Snapshot& current = m_Entries[m_Cursor];
		if (current.Archive && *current.Archive == archive)
			return false;

		TruncateRedo();
		Snapshot next;
		next.Id = m_NextStateId++;
		next.Archive = std::make_shared<const std::string>(std::move(archive));
		next.SelectedEntity = selectedEntity;
		next.Label = std::move(label);
		m_MemoryUsage += Measure(next);
		m_Entries.push_back(std::move(next));
		m_Cursor = m_Entries.size() - 1;
		EnforceLimits();
		return true;
	}

	void SceneHistory::CancelTransaction()
	{
		m_TransactionActive = false;
		m_TransactionLabel.clear();
	}

	bool SceneHistory::Undo(const RestoreCallback& restore)
	{
		if (!CanUndo() || !restore)
			return false;
		const size_t target = m_Cursor - 1;
		if (!restore(m_Entries[target]))
			return false;
		m_Cursor = target;
		return true;
	}

	bool SceneHistory::Redo(const RestoreCallback& restore)
	{
		if (!CanRedo() || !restore)
			return false;
		const size_t target = m_Cursor + 1;
		if (!restore(m_Entries[target]))
			return false;
		m_Cursor = target;
		return true;
	}

	bool SceneHistory::CanUndo() const
	{
		return !m_TransactionActive && !m_Entries.empty() && m_Cursor > 0;
	}

	bool SceneHistory::CanRedo() const
	{
		return !m_TransactionActive && !m_Entries.empty()
			&& m_Cursor + 1 < m_Entries.size();
	}

	void SceneHistory::MarkSaved()
	{
		m_SavedStateId = GetCurrentStateId();
	}

	void SceneHistory::InvalidateSavedState()
	{
		m_SavedStateId = 0;
	}

	bool SceneHistory::IsDirty() const
	{
		const StateId current = GetCurrentStateId();
		return current != 0 && current != m_SavedStateId;
	}

	SceneHistory::StateId SceneHistory::GetCurrentStateId() const
	{
		const Snapshot* current = GetCurrentSnapshot();
		return current ? current->Id : 0;
	}

	const SceneHistory::Snapshot* SceneHistory::GetCurrentSnapshot() const
	{
		return m_Entries.empty() || m_Cursor >= m_Entries.size()
			? nullptr : &m_Entries[m_Cursor];
	}

	void SceneHistory::SetCurrentSelection(uint64_t selectedEntity)
	{
		if (!m_TransactionActive && !m_Entries.empty())
			m_Entries[m_Cursor].SelectedEntity = selectedEntity;
	}

	bool SceneHistory::RefreshCurrentSnapshot(std::string archive,
		uint64_t selectedEntity)
	{
		if (m_TransactionActive || m_Entries.empty())
			return false;
		Snapshot& current = m_Entries[m_Cursor];
		m_MemoryUsage -= Measure(current);
		current.Archive =
			std::make_shared<const std::string>(std::move(archive));
		current.SelectedEntity = selectedEntity;
		m_MemoryUsage += Measure(current);
		EnforceLimits();
		return true;
	}

	size_t SceneHistory::Measure(const Snapshot& snapshot)
	{
		return (snapshot.Archive ? snapshot.Archive->size() : 0)
			+ snapshot.Label.size();
	}

	void SceneHistory::EnforceLimits()
	{
		while (m_Entries.size() > 1 && m_Cursor > 0
			&& (m_Entries.size() > m_Limits.MaximumEntries
				|| m_MemoryUsage > m_Limits.MaximumBytes))
		{
			m_MemoryUsage -= Measure(m_Entries.front());
			m_Entries.erase(m_Entries.begin());
			--m_Cursor;
		}
	}

	void SceneHistory::TruncateRedo()
	{
		if (m_Entries.empty() || m_Cursor + 1 >= m_Entries.size())
			return;
		for (size_t index = m_Cursor + 1; index < m_Entries.size(); ++index)
			m_MemoryUsage -= Measure(m_Entries[index]);
		m_Entries.erase(m_Entries.begin() + static_cast<ptrdiff_t>(m_Cursor + 1),
			m_Entries.end());
	}

}
