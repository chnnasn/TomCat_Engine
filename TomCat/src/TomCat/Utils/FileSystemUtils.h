#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace TomCat::FileSystem {

	// Pins every existing directory component from the filesystem root through
	// directory. On Windows the opened handles deliberately omit
	// FILE_SHARE_DELETE, so a validated parent cannot be renamed, deleted, or
	// replaced by a junction for the lifetime of the guard.
	class PinnedDirectoryChain
	{
	public:
		PinnedDirectoryChain();
		~PinnedDirectoryChain();
		PinnedDirectoryChain(PinnedDirectoryChain&& other) noexcept;
		PinnedDirectoryChain& operator=(PinnedDirectoryChain&& other) noexcept;

		PinnedDirectoryChain(const PinnedDirectoryChain&) = delete;
		PinnedDirectoryChain& operator=(const PinnedDirectoryChain&) = delete;

		[[nodiscard]] bool Acquire(const std::filesystem::path& directory,
			std::string& errorMessage);
		[[nodiscard]] bool Verify(std::string& errorMessage) const;
		void Reset();
		[[nodiscard]] bool IsAcquired() const;

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};

	// Creates directory while its complete parent chain is pinned, then returns
	// a guard that pins the new/existing directory itself. Existing reparse
	// points are rejected. created is true only when this call created it.
	[[nodiscard]] bool CreateDirectoryAndPin(const std::filesystem::path& directory,
		PinnedDirectoryChain& guard, bool& created, std::string& errorMessage);

	// Deletes one regular file or one empty regular directory by an opened handle
	// while its parent chain is pinned. Reparse points are never followed or
	// deleted. Missing paths are reported as success with removed == false.
	[[nodiscard]] bool RemovePathSafely(const std::filesystem::path& path,
		bool& removed, std::string& errorMessage);

	// Returns a non-existing path next to destination. Keeping the temporary file
	// on the same volume is required for the final replacement to be atomic.
	[[nodiscard]] std::filesystem::path MakeTemporarySiblingPath(const std::filesystem::path& destination);

	// Installs a completed sibling temporary file without exposing a partially
	// written destination. The caller retains ownership of temporary on failure.
	[[nodiscard]] bool InstallTemporaryFileAtomically(const std::filesystem::path& temporary,
		const std::filesystem::path& destination, std::string& errorMessage);

	// Writes bytes to a sibling temporary file and atomically installs it. If
	// installation fails, the completed temporary file is retained for recovery.
	[[nodiscard]] bool WriteFileAtomically(const std::filesystem::path& destination,
		std::string_view contents, std::string& errorMessage);

	// Deterministic race injection for filesystem regression tests. Production
	// code leaves this empty. The callback runs only after the mutation parent is
	// pinned and before the final mutation/verification.
	using DirectoryMutationTestHook =
		std::function<void(const std::filesystem::path& pinnedDirectory)>;
	void SetDirectoryMutationTestHookForTesting(DirectoryMutationTestHook hook);

}
