#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace TomCat {

	class SceneHistory final
	{
	public:
		using StateId = uint64_t;

		struct Limits
		{
			size_t MaximumEntries = 128;
			size_t MaximumBytes = 64 * 1024 * 1024;
		};

		struct Snapshot
		{
			StateId Id = 0;
			std::shared_ptr<const std::string> Archive;
			uint64_t SelectedEntity = 0;
			std::string Label;
		};

		using RestoreCallback = std::function<bool(const Snapshot&)>;

		SceneHistory();
		explicit SceneHistory(Limits limits);

		bool Reset(std::string archive, uint64_t selectedEntity, bool isSaved);
		bool BeginTransaction(std::string label);
		bool CommitTransaction(std::string archive, uint64_t selectedEntity);
		void CancelTransaction();

		bool Undo(const RestoreCallback& restore);
		bool Redo(const RestoreCallback& restore);
		bool CanUndo() const;
		bool CanRedo() const;
		bool HasActiveTransaction() const { return m_TransactionActive; }

		void MarkSaved();
		void InvalidateSavedState();
		bool IsDirty() const;
		StateId GetCurrentStateId() const;
		StateId GetSavedStateId() const { return m_SavedStateId; }
		size_t GetEntryCount() const { return m_Entries.size(); }
		size_t GetMemoryUsage() const { return m_MemoryUsage; }
		const Snapshot* GetCurrentSnapshot() const;
		void SetCurrentSelection(uint64_t selectedEntity);

		// Saving a scene may canonicalize metadata such as its name. Refresh the
		// current immutable archive without manufacturing a user-edit transaction.
		bool RefreshCurrentSnapshot(std::string archive, uint64_t selectedEntity);

	private:
		static size_t Measure(const Snapshot& snapshot);
		void EnforceLimits();
		void TruncateRedo();

		Limits m_Limits;
		std::vector<Snapshot> m_Entries;
		size_t m_Cursor = 0;
		size_t m_MemoryUsage = 0;
		StateId m_NextStateId = 1;
		StateId m_SavedStateId = 0;
		bool m_TransactionActive = false;
		std::string m_TransactionLabel;
	};

}
