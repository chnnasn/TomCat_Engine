#include "TomCat/Asset/AssetDatabase.h"
#include "TomCat/Asset/Advanced2DAuthoringAssets.h"
#include "TomCat/Asset/AssetImportCoordinator.h"
#include "TomCat/Asset/AssetJobSystem.h"
#include "TomCat/Asset/ArtifactKey.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/AssetRegistry.h"
#include "TomCat/Asset/ContentHash.h"
#include "TomCat/Asset/MaterialArtifact.h"
#include "TomCat/Asset/MeshArtifact.h"
#include "TomCat/Asset/ShaderArtifact.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Asset/TextureArtifact.h"
#include "TomCat/Audio/AudioClip.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Core/UUID.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Renderer/Shader.h"
#include "TomCat/Runtime/RuntimeCompatibility.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scene/Serialization/PrefabArchiveCodec.h"
#include "TomCat/Scene/Serialization/PrefabLink.h"

#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace {

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	class HiddenOpenGLContext final
	{
	public:
		HiddenOpenGLContext()
		{
			if (glfwInit() != GLFW_TRUE)
			{
				m_UnavailableReason = "GLFW initialization is unavailable";
				return;
			}
			m_GLFWInitialized = true;
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
			m_Window = glfwCreateWindow(32, 32,
				"TomCat Shader Artifact Regression", nullptr, nullptr);
			if (!m_Window)
			{
				m_UnavailableReason = "an OpenGL 4.6 context is unavailable";
				Cleanup();
				return;
			}
			glfwMakeContextCurrent(m_Window);
			if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(
				glfwGetProcAddress)) == 0 || !GLAD_GL_VERSION_4_6
				|| !glSpecializeShader)
			{
				m_UnavailableReason =
					"OpenGL SPIR-V specialization is unavailable";
				Cleanup();
				return;
			}
			m_Available = true;
		}

		~HiddenOpenGLContext() { Cleanup(); }

		bool IsAvailable() const { return m_Available; }
		const std::string& GetUnavailableReason() const
		{
			return m_UnavailableReason;
		}

		HiddenOpenGLContext(const HiddenOpenGLContext&) = delete;
		HiddenOpenGLContext& operator=(const HiddenOpenGLContext&) = delete;

	private:
		void Cleanup()
		{
			TomCat::AssetManager::Get().ReleaseAll();
			m_Available = false;
			if (m_Window)
			{
				glfwDestroyWindow(m_Window);
				m_Window = nullptr;
			}
			if (m_GLFWInitialized)
			{
				glfwTerminate();
				m_GLFWInitialized = false;
			}
		}

		GLFWwindow* m_Window = nullptr;
		bool m_GLFWInitialized = false;
		bool m_Available = false;
		std::string m_UnavailableReason;
	};

	void TestBoundedAssetJobSystem()
	{
		using namespace std::chrono_literals;
		auto& jobs = TomCat::AssetJobSystem::Get();
		constexpr uint64_t memoryBudget = 1024 * 1024;
		jobs.Configure({ 4, 4, memoryBudget });
		std::atomic_uint32_t active = 0;
		std::atomic_uint32_t maximum = 0;
		std::vector<std::future<void>> futures;
		for (uint32_t index = 0; index < 4; ++index)
		{
			futures.push_back(jobs.Submit(768 * 1024, [&]()
			{
				const uint32_t count = ++active;
				uint32_t observed = maximum.load();
				while (observed < count
					&& !maximum.compare_exchange_weak(observed, count)) {}
				std::this_thread::sleep_for(10ms);
				--active;
			}));
		}
		for (auto& future : futures)
			future.get();
		Require(maximum.load() == 1,
			"asset job memory reservations did not bound concurrency");

		// A reservation above the configured budget must make progress without
		// allowing any other reserved task to overlap it.
		jobs.Configure({ 2, 4, memoryBudget });
		std::promise<void> oversizedStarted;
		std::future<void> oversizedStartedFuture = oversizedStarted.get_future();
		std::promise<void> releaseOversized;
		std::shared_future<void> releaseOversizedFuture =
			releaseOversized.get_future().share();
		std::future<void> oversized = jobs.Submit(memoryBudget * 2,
			[&]()
			{
				oversizedStarted.set_value();
				releaseOversizedFuture.wait();
			});
		oversizedStartedFuture.wait();
		std::atomic_bool smallRan = false;
		std::future<void> small;
		std::exception_ptr smallSubmitFailure;
		std::thread submitter([&]()
		{
			try
			{
				small = jobs.Submit(1, [&]() { smallRan = true; });
			}
			catch (...)
			{
				smallSubmitFailure = std::current_exception();
			}
		});
		std::this_thread::sleep_for(25ms);
		const bool oversizedStayedExclusive = !smallRan.load();
		releaseOversized.set_value();
		oversized.get();
		submitter.join();
		if (smallSubmitFailure)
			std::rethrow_exception(smallSubmitFailure);
		small.get();
		Require(oversizedStayedExclusive,
			"an over-budget asset job did not run exclusively");

		// Queue capacity is independent of worker count and must remain bounded
		// while the only worker is occupied.
		jobs.Configure({ 1, 1, memoryBudget });
		std::promise<void> blockerStarted;
		std::future<void> blockerStartedFuture = blockerStarted.get_future();
		std::promise<void> releaseBlocker;
		std::shared_future<void> releaseBlockerFuture =
			releaseBlocker.get_future().share();
		std::promise<void> queuedFinished;
		std::future<void> queuedFinishedFuture = queuedFinished.get_future();
		std::future<void> blocker = jobs.Submit(0, [&]()
		{
			blockerStarted.set_value();
			releaseBlockerFuture.wait();
		});
		blockerStartedFuture.wait();
		const bool acceptedQueued = jobs.TrySchedule(0,
			[&]() { queuedFinished.set_value(); });
		const bool rejectedOverflow = !jobs.TrySchedule(0, []() {});
		releaseBlocker.set_value();
		blocker.get();
		if (acceptedQueued)
			queuedFinishedFuture.wait();
		Require(acceptedQueued && rejectedOverflow,
			"asset job queue capacity was not enforced");

		// Dependent work submitted by the sole worker executes inline. It may
		// share the parent's reservation but may not claim additional capacity.
		jobs.Configure({ 1, 4, memoryBudget });
		std::future<int> nested = jobs.Submit(4096, [&jobs]()
		{
			std::future<int> dependency = jobs.Submit(2048, []() { return 17; });
			return dependency.get();
		});
		Require(nested.get() == 17,
			"single-worker nested asset submission deadlocked or lost its result");
		std::future<bool> transitiveBudgetRejected = jobs.Submit(4096, [&jobs]()
		{
			std::future<bool> child = jobs.Submit(2048, [&jobs]()
			{
				try { (void)jobs.Submit(3072, []() {}); }
				catch (const std::runtime_error&) { return true; }
				return false;
			});
			return child.get();
		});
		Require(transitiveBudgetRejected.get(),
			"transitive nested work exceeded its direct parent reservation");
		std::future<bool> nestedBudgetRejected = jobs.Submit(0, [&jobs]()
		{
			try
			{
				std::future<void> invalid = jobs.Submit(1, []() {});
				invalid.get();
			}
			catch (const std::runtime_error&)
			{
				return !jobs.TrySchedule(1, []() {});
			}
			return false;
		});
		Require(nestedBudgetRejected.get(),
			"nested asset work bypassed its parent memory reservation");

		std::future<int> exceptional = jobs.Submit(0, []() -> int
		{
			throw std::runtime_error("asset job sentinel");
		});
		bool exceptionPropagated = false;
		try
		{
			(void)exceptional.get();
		}
		catch (const std::runtime_error& exception)
		{
			exceptionPropagated = std::string_view(exception.what())
				== "asset job sentinel";
		}
		Require(exceptionPropagated,
			"asset job exception was not preserved by its future");
		std::promise<void> fireAndForgetEntered;
		std::future<void> fireAndForgetEnteredFuture =
			fireAndForgetEntered.get_future();
		Require(jobs.TrySchedule(0, [&]()
		{
			fireAndForgetEntered.set_value();
			throw std::runtime_error("expected fire-and-forget test exception");
		}), "fire-and-forget exception probe was not scheduled");
		fireAndForgetEnteredFuture.wait();
		Require(jobs.Submit(0, []() { return 19; }).get() == 19,
			"fire-and-forget exception terminated the asset worker");

		std::future<uint32_t> lifecycleRejected = jobs.Submit(0, [&jobs]()
		{
			uint32_t rejected = 0;
			try { jobs.Shutdown(); }
			catch (const std::logic_error&) { rejected |= 1; }
			try { jobs.Configure({ 1, 1, memoryBudget }); }
			catch (const std::logic_error&) { rejected |= 2; }
			return rejected;
		});
		Require(lifecycleRejected.get() == 3,
			"asset worker was allowed to join or reconfigure its own pool");

		// Configure and Shutdown both release the state mutex while joining. Their
		// wider lifecycle transaction must still be serialized against each other.
		for (uint32_t iteration = 0; iteration < 4; ++iteration)
		{
			std::promise<void> beginLifecycleRace;
			std::shared_future<void> beginLifecycleRaceFuture =
				beginLifecycleRace.get_future().share();
			std::exception_ptr configureFailure;
			std::exception_ptr shutdownFailure;
			std::thread configureThread([&]()
			{
				beginLifecycleRaceFuture.wait();
				try { jobs.Configure({ 1, 4, memoryBudget }); }
				catch (...) { configureFailure = std::current_exception(); }
			});
			std::thread shutdownThread([&]()
			{
				beginLifecycleRaceFuture.wait();
				try { jobs.Shutdown(); }
				catch (...) { shutdownFailure = std::current_exception(); }
			});
			beginLifecycleRace.set_value();
			configureThread.join();
			shutdownThread.join();
			if (configureFailure)
				std::rethrow_exception(configureFailure);
			if (shutdownFailure)
				std::rethrow_exception(shutdownFailure);
			jobs.Configure({ 1, 4, memoryBudget });
			Require(jobs.Submit(0, []() { return 23; }).get() == 23,
				"executor did not recover after concurrent lifecycle operations");
		}

		std::atomic_uint32_t drained = 0;
		std::vector<std::future<void>> draining;
		for (uint32_t index = 0; index < 3; ++index)
		{
			draining.push_back(jobs.Submit(0, [&]()
			{
				std::this_thread::sleep_for(2ms);
				++drained;
			}));
		}
		jobs.Shutdown();
		for (auto& future : draining)
			future.get();
		Require(drained.load() == draining.size(),
			"AssetJobSystem::Shutdown abandoned queued work");
		bool stoppedSubmitRejected = false;
		try { (void)jobs.Submit(0, []() {}); }
		catch (const std::runtime_error&) { stoppedSubmitRejected = true; }
		Require(stoppedSubmitRejected && !jobs.TrySchedule(0, []() {}),
			"a stopped asset job system accepted new work");
		jobs.Configure({});
	}

	void WriteBigEndian16(std::vector<uint8_t>& bytes, size_t offset,
		uint16_t value)
	{
		Require(offset <= bytes.size() && bytes.size() - offset >= 2,
			"test BE16 write is out of range");
		bytes[offset] = static_cast<uint8_t>(value >> 8);
		bytes[offset + 1] = static_cast<uint8_t>(value);
	}

	void WriteBigEndian32(std::vector<uint8_t>& bytes, size_t offset,
		uint32_t value)
	{
		Require(offset <= bytes.size() && bytes.size() - offset >= 4,
			"test BE32 write is out of range");
		bytes[offset] = static_cast<uint8_t>(value >> 24);
		bytes[offset + 1] = static_cast<uint8_t>(value >> 16);
		bytes[offset + 2] = static_cast<uint8_t>(value >> 8);
		bytes[offset + 3] = static_cast<uint8_t>(value);
	}

	void WriteLittleEndian16(std::vector<uint8_t>& bytes, size_t offset,
		uint16_t value)
	{
		Require(offset <= bytes.size() && bytes.size() - offset >= 2,
			"test LE16 write is out of range");
		bytes[offset] = static_cast<uint8_t>(value);
		bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
	}

	void WriteLittleEndian32(std::vector<uint8_t>& bytes, size_t offset,
		uint32_t value)
	{
		Require(offset <= bytes.size() && bytes.size() - offset >= 4,
			"test LE32 write is out of range");
		for (uint32_t index = 0; index < 4; ++index)
			bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
	}

	uint32_t ReadLittleEndian32(std::span<const uint8_t> bytes, size_t offset)
	{
		Require(offset <= bytes.size() && bytes.size() - offset >= 4,
			"test LE32 read is out of range");
		uint32_t value = 0;
		for (uint32_t index = 0; index < 4; ++index)
			value |= static_cast<uint32_t>(bytes[offset + index]) << (index * 8);
		return value;
	}

	uint64_t ReadLittleEndian64(std::span<const uint8_t> bytes, size_t offset)
	{
		Require(offset <= bytes.size() && bytes.size() - offset >= 8,
			"test LE64 read is out of range");
		uint64_t value = 0;
		for (uint32_t index = 0; index < 8; ++index)
			value |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8);
		return value;
	}

	std::vector<uint8_t> MakeFourByFourBMP()
	{
		constexpr uint32_t width = 4;
		constexpr uint32_t height = 4;
		constexpr uint32_t pixelOffset = 54;
		constexpr uint32_t pixelBytes = width * height * 3;
		std::vector<uint8_t> bytes(pixelOffset + pixelBytes, 0);
		bytes[0] = 'B'; bytes[1] = 'M';
		WriteLittleEndian32(bytes, 2, static_cast<uint32_t>(bytes.size()));
		WriteLittleEndian32(bytes, 10, pixelOffset);
		WriteLittleEndian32(bytes, 14, 40);
		WriteLittleEndian32(bytes, 18, width);
		WriteLittleEndian32(bytes, 22, height);
		WriteLittleEndian16(bytes, 26, 1);
		WriteLittleEndian16(bytes, 28, 24);
		WriteLittleEndian32(bytes, 34, pixelBytes);
		for (uint32_t y = 0; y < height; ++y)
		for (uint32_t x = 0; x < width; ++x)
		{
			const size_t pixel = pixelOffset + (static_cast<size_t>(y) * width + x) * 3;
			bytes[pixel] = static_cast<uint8_t>(32 + x * 48);
			bytes[pixel + 1] = static_cast<uint8_t>(24 + y * 56);
			bytes[pixel + 2] = static_cast<uint8_t>(16 + (x + y) * 24);
		}
		return bytes;
	}

	std::vector<uint8_t> MakeEightBitWave()
	{
		std::vector<uint8_t> bytes;
		auto fourCC = [&](const char* value)
		{
			bytes.insert(bytes.end(), value, value + 4);
		};
		auto u16 = [&](uint16_t value)
		{
			const size_t offset = bytes.size(); bytes.resize(offset + 2);
			WriteLittleEndian16(bytes, offset, value);
		};
		auto u32 = [&](uint32_t value)
		{
			const size_t offset = bytes.size(); bytes.resize(offset + 4);
			WriteLittleEndian32(bytes, offset, value);
		};
		constexpr uint32_t sampleRate = 8000;
		constexpr uint32_t sampleCount = 4;
		fourCC("RIFF"); u32(36 + sampleCount); fourCC("WAVE");
		fourCC("fmt "); u32(16); u16(1); u16(1); u32(sampleRate);
		u32(sampleRate); u16(1); u16(8);
		fourCC("data"); u32(sampleCount);
		bytes.insert(bytes.end(), { 0, 64, 128, 255 });
		return bytes;
	}

	void WriteBytes(const std::filesystem::path& path, std::string_view bytes)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		Require(static_cast<bool>(output), "could not create test file");
		output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		Require(static_cast<bool>(output), "could not write complete test file");
	}

	void WriteBinary(const std::filesystem::path& path,
		std::span<const uint8_t> bytes)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		Require(static_cast<bool>(output), "could not create binary test file");
		output.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		Require(static_cast<bool>(output), "could not write complete binary test file");
	}

	std::vector<uint8_t> ReadBinary(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		Require(static_cast<bool>(input), "could not open binary test file");
		const std::streamoff end = input.tellg();
		Require(end >= 0, "binary test file has an invalid size");
		std::vector<uint8_t> bytes(static_cast<size_t>(end));
		input.seekg(0, std::ios::beg);
		if (!bytes.empty())
			input.read(reinterpret_cast<char*>(bytes.data()), end);
		Require(static_cast<bool>(input) || bytes.empty(),
			"could not read complete binary test file");
		return bytes;
	}

	std::vector<uint8_t> MakeManagedAssemblyFixture(const std::string& manifest)
	{
		std::vector<uint8_t> assembly = { 'M', 'Z' };
		assembly.reserve(assembly.size() + manifest.size() * 2);
		for (const unsigned char character : manifest)
		{
			assembly.push_back(character);
			assembly.push_back(0);
		}
		return assembly;
	}

	std::string ReadText(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		Require(static_cast<bool>(input), "could not open test file");
		const std::streamoff size = input.tellg();
		Require(size >= 0, "test file size is invalid");
		std::string result(static_cast<size_t>(size), '\0');
		input.seekg(0, std::ios::beg);
		if (!result.empty())
			input.read(result.data(), size);
		Require(static_cast<bool>(input) || result.empty(), "could not read test file");
		return result;
	}

	std::string MakeDependencyShader(std::string_view revision)
	{
		return "// " + std::string(revision) + "\n"
			"#type vertex\n#version 450 core\n"
			"layout(location=0) in vec3 a_Position;\n"
			"void main(){ gl_Position=vec4(a_Position,1.0); }\n"
			"#type fragment\n#version 450 core\n"
			"layout(location=0) out vec4 o_Color;\n"
			"void main(){ o_Color=vec4(1.0); }\n";
	}

	class TemporaryProject final
	{
	public:
		TemporaryProject()
		{
			Root = std::filesystem::temp_directory_path() /
				("tomcat_importers_" +
					std::to_string(static_cast<uint64_t>(TomCat::UUID())));
			Assets = Root / "Assets";
			Library = Root / "Library";
			std::filesystem::create_directories(Assets);
		}

		~TemporaryProject()
		{
			std::error_code ignored;
			std::filesystem::remove_all(Root, ignored);
		}

		std::filesystem::path Root;
		std::filesystem::path Assets;
		std::filesystem::path Library;
	};

	class CountingTextureImporter final : public TomCat::IAssetImporter
	{
	public:
		std::string_view GetID() const noexcept override
		{
			return "regression.texture.counting";
		}
		uint32_t GetVersion() const noexcept override { return 7; }
		TomCat::AssetType GetAssetType() const noexcept override
		{
			return TomCat::AssetType::Texture2D;
		}

		TomCat::AssetImportResult Import(
			const TomCat::AssetImportRequest& request) const override
		{
			++Invocations;
			const uint32_t active = ActiveInvocations.fetch_add(1) + 1;
			uint32_t maximum = MaxConcurrentInvocations.load();
			while (maximum < active && !MaxConcurrentInvocations.compare_exchange_weak(
				maximum, active)) {}
			std::this_thread::sleep_for(std::chrono::milliseconds(
				DelayMilliseconds.load()));
			TomCat::AssetImportResult result;
			if (request.IsCancellationRequested())
			{
				result.Error = "cancelled";
				ActiveInvocations.fetch_sub(1);
				return result;
			}
			result.Format = "regression-texture/v7";
			result.ArtifactBytes.assign(request.SourceBytes.begin(), request.SourceBytes.end());
			const auto setting = request.Settings.find("quality");
			if (setting != request.Settings.end())
				result.ArtifactBytes.insert(result.ArtifactBytes.end(),
					setting->second.begin(), setting->second.end());
			result.SubAssets.push_back({ "sprite:hero", "Hero", TomCat::AssetType::Texture2D });
			ActiveInvocations.fetch_sub(1);
			return result;
		}

		mutable std::atomic_uint32_t Invocations = 0;
		mutable std::atomic_uint32_t DelayMilliseconds = 15;
		mutable std::atomic_uint32_t ActiveInvocations = 0;
		mutable std::atomic_uint32_t MaxConcurrentInvocations = 0;
	};


	class BlockingSnapshotTextureImporter final : public TomCat::IAssetImporter
	{
	public:
		std::string_view GetID() const noexcept override
		{
			return "regression.texture.blocking-snapshot";
		}
		uint32_t GetVersion() const noexcept override { return 1; }
		TomCat::AssetType GetAssetType() const noexcept override
		{
			return TomCat::AssetType::Texture2D;
		}

		void Arm(TomCat::AssetHandle handle)
		{
			std::scoped_lock lock(m_Mutex);
			m_BlockedHandle = handle;
			m_Armed = true;
			m_Entered = false;
			m_Released = false;
		}

		bool WaitUntilBlocked() const
		{
			std::unique_lock lock(m_Mutex);
			return m_Wake.wait_for(lock, std::chrono::seconds(5),
				[this]() { return m_Entered; });
		}

		void Release()
		{
			{
				std::scoped_lock lock(m_Mutex);
				m_Released = true;
			}
			m_Wake.notify_all();
		}

		TomCat::AssetImportResult Import(
			const TomCat::AssetImportRequest& request) const override
		{
			Invocations.fetch_add(1);
			{
				std::unique_lock lock(m_Mutex);
				if (m_Armed && request.Handle == m_BlockedHandle)
				{
					m_Entered = true;
					m_Wake.notify_all();
					m_Wake.wait(lock, [this]() { return m_Released; });
					m_Armed = false;
				}
			}

			TomCat::AssetImportResult result;
			if (request.IsCancellationRequested())
			{
				result.Error = "cancelled";
				return result;
			}
			result.Format = "regression-texture/snapshot-v1";
			result.ArtifactBytes.assign(request.SourceBytes.begin(),
				request.SourceBytes.end());
			std::string childName = "initial";
			if (const auto setting = request.Settings.find("variant");
				setting != request.Settings.end())
				childName = setting->second;
			result.ArtifactBytes.insert(result.ArtifactBytes.end(),
				childName.begin(), childName.end());
			result.SubAssets.push_back({ "sprite:generated", childName,
				TomCat::AssetType::Texture2D });
			return result;
		}

		mutable std::atomic_uint32_t Invocations = 0;

	private:
		mutable std::mutex m_Mutex;
		mutable std::condition_variable m_Wake;
		mutable TomCat::AssetHandle m_BlockedHandle = TomCat::AssetHandle(0);
		mutable bool m_Armed = false;
		mutable bool m_Entered = false;
		mutable bool m_Released = false;
	};

	TomCat::AssetHandle RequireHandle(TomCat::AssetRegistry& registry,
		const std::filesystem::path& path, TomCat::AssetType expected);

    void TestScopedPrefabProperties()
    {
        using namespace TomCat;
        TemporaryProject project;
        auto& assets=AssetManager::Get(); assets.Shutdown();
        Require(assets.Initialize(project.Assets,project.Library),"Cannot initialize scoped prefab test");
        struct Shutdown { ~Shutdown(){AssetManager::Get().Shutdown();} } shutdown;
        auto source=CreateRef<Scene>(); Entity templateRoot=source->CreateEntity("Enemy");
        templateRoot.AddComponent<HealthComponent>();
        PrefabArchive archive; std::string error,document;
        auto checked=[&](bool success){if(!success) throw std::runtime_error(error);};
        checked(PrefabArchiveCodec::CaptureSubtree(source,templateRoot,archive,error));
        checked(PrefabArchiveCodec::Encode(archive,document,error));
        const auto path=project.Assets/"Enemy.tcprefab"; WriteBytes(path,document);
        AssetHandle asset=assets.ImportAsset(path); Require(static_cast<uint64_t>(asset)!=0,"Cannot import scoped prefab");
        auto scene=CreateRef<Scene>(); PrefabInstantiateOptions options; options.ResolveAssets=false;
        PrefabInstantiationResult first,second;
        checked(PrefabArchiveCodec::Instantiate(archive,*scene,options,first,error));
        checked(PrefabLinkedInstance::Attach(scene,asset,archive,first,error));
        checked(PrefabArchiveCodec::Instantiate(archive,*scene,options,second,error));
        checked(PrefabLinkedInstance::Attach(scene,asset,archive,second,error));
        const UUID one=first.Root.GetUUID(),two=second.Root.GetUUID();
        first.Root.GetComponent<HealthComponent>().Maximum=150;
        first.Root.GetComponent<HealthComponent>().Invulnerable=true;
        std::vector<PrefabPropertyOverride> overrides;
        checked(PrefabLinkedInstance::GetPropertyOverrides(scene,one,overrides,error));
        Require(overrides.size()==2,"Expected two property overrides");
        checked(PrefabLinkedInstance::ApplyProperty(scene,one,one,UUID(ComponentIds::Health),UUID(ComponentIds::HealthProperties::Maximum),error));
        Require(scene->FindEntityByUUID(two).GetComponent<HealthComponent>().Maximum==150,"Property Apply did not update sibling instance");
        Require(!scene->FindEntityByUUID(two).GetComponent<HealthComponent>().Invulnerable,"Property Apply leaked another override to sibling");
        Require(scene->FindEntityByUUID(one).GetComponent<HealthComponent>().Invulnerable,"Property Apply lost another local override");
        checked(PrefabLinkedInstance::GetPropertyOverrides(scene,one,overrides,error));
        Require(overrides.size()==1 && overrides.front().PropertyID==UUID(ComponentIds::HealthProperties::Invulnerable),"Applied property still appears overridden");
        PrefabArchive saved; checked(PrefabArchiveCodec::Load(path,saved,error));
        auto savedRoot=saved.TemplateScene->FindEntityByUUID(UUID(saved.RootLocalID));
        Require(savedRoot.GetComponent<HealthComponent>().Maximum==150 && !savedRoot.GetComponent<HealthComponent>().Invulnerable,"Source asset contains unrelated edits");
        checked(PrefabLinkedInstance::RevertProperty(scene,one,one,UUID(ComponentIds::Health),UUID(ComponentIds::HealthProperties::Invulnerable),error));
        Require(!scene->FindEntityByUUID(one).GetComponent<HealthComponent>().Invulnerable,"Scoped Revert did not restore baseline");
        const std::string before=YAML::Dump(YAML::LoadFile(path.string()));
        Require(!PrefabLinkedInstance::ApplyProperty(scene,one,one,UUID(ComponentIds::Health),UUID(999),error),"Unknown property was accepted");
        Require(YAML::Dump(YAML::LoadFile(path.string()))==before,"Rejected Apply modified source file");
        std::cout<<"PASS scoped Prefab Apply/Revert preserves unrelated overrides and propagates only selected property\n";
    }

	void TestAsyncAssetOwnerLifecycle()
	{
		using namespace std::chrono_literals;
		TemporaryProject first;
		TemporaryProject replacement;
		const std::filesystem::path firstPath = first.Assets / "slow-first.png";
		const std::filesystem::path replacementPath =
			replacement.Assets / "slow-replacement.png";
		WriteBytes(firstPath, "first-owner-lifecycle");
		WriteBytes(replacementPath, "replacement-owner-lifecycle");

		auto& jobs = TomCat::AssetJobSystem::Get();
		jobs.Configure({ 1, 8, 64ULL * 1024ULL * 1024ULL });
		TomCat::AssetManager& manager = TomCat::AssetManager::Get();
		manager.Shutdown();
		const TomCat::Ref<TomCat::Project> firstProject =
			TomCat::CreateRef<TomCat::Project>(first.Root / "First.tcproj");
		const TomCat::Ref<TomCat::Project> replacementProject =
			TomCat::CreateRef<TomCat::Project>(replacement.Root / "Replacement.tcproj");
		Require(manager.SetProject(firstProject),
			"could not initialize the first async owner project");
		const TomCat::AssetHandle firstHandle = RequireHandle(manager.GetRegistry(),
			firstPath, TomCat::AssetType::Texture2D);
		auto firstImporter = std::make_shared<CountingTextureImporter>();
		firstImporter->DelayMilliseconds = 0;
		Require(manager.GetDatabase().GetImporters().Register(firstImporter, true),
			"could not install the first async owner importer");

		std::atomic_bool decoderEntered = false;
		std::atomic_bool decoderFinished = false;
		{
			std::future<std::optional<size_t>> dropped =
				manager.GetDatabase().LoadAsync<size_t>(firstHandle,
					[&](const TomCat::ImportedArtifact& artifact)
						-> std::optional<size_t>
					{
						decoderEntered = true;
						std::this_thread::sleep_for(150ms);
						decoderFinished = true;
						return artifact.Bytes.size();
					});
			const auto deadline = std::chrono::steady_clock::now() + 2s;
			while (!decoderEntered.load() && std::chrono::steady_clock::now() < deadline)
				std::this_thread::yield();
			Require(decoderEntered.load(),
				"the dropped database future did not enter its decoder");
		}
		Require(manager.SetProject(replacementProject),
			"SetProject failed after dropping an in-flight database future");
		Require(decoderFinished.load(),
			"SetProject cleared the database before its dropped future completed");

		const TomCat::AssetHandle replacementHandle = RequireHandle(
			manager.GetRegistry(), replacementPath, TomCat::AssetType::Texture2D);
		auto replacementImporter = std::make_shared<CountingTextureImporter>();
		replacementImporter->DelayMilliseconds = 150;
		Require(manager.GetDatabase().GetImporters().Register(
			replacementImporter, true),
			"could not install the replacement async owner importer");

		// Both the database and manager acquire an owner count before Submit. A
		// stopped executor forces that call to throw; Shutdown below would hang if
		// either exception path forgot to release its count.
		manager.GetImportCoordinator().Stop();
		jobs.Shutdown();
		bool submitRejected = false;
		try
		{
			std::future<TomCat::AssetLoadResult> rejected =
				manager.LoadImportedArtifactAsync(replacementHandle);
		}
		catch (const std::runtime_error&)
		{
			submitRejected = true;
		}
		Require(submitRejected,
			"a stopped asset executor unexpectedly accepted an async load");
		jobs.Configure({ 1, 8, 64ULL * 1024ULL * 1024ULL });

		std::future<bool> workerProjectChange = jobs.Submit(0,
			[&manager, replacementProject]()
			{
				return manager.SetProject(replacementProject);
			});
		Require(!workerProjectChange.get() && manager.IsInitialized(),
			"an asset worker entered a lifecycle wait or changed the active project");

		{
			std::future<TomCat::AssetLoadResult> dropped =
				manager.LoadImportedArtifactAsync(replacementHandle);
			const auto deadline = std::chrono::steady_clock::now() + 2s;
			while (replacementImporter->ActiveInvocations.load() == 0
				&& std::chrono::steady_clock::now() < deadline)
				std::this_thread::yield();
			Require(replacementImporter->ActiveInvocations.load() == 1,
				"the dropped manager future did not enter its importer");
		}
		manager.Shutdown();
		Require(replacementImporter->ActiveInvocations.load() == 0,
			"AssetManager shutdown returned while a dropped async load was active");
		const TomCat::AssetLoadResult afterShutdown =
			manager.LoadImportedArtifactAsync(replacementHandle).get();
		Require(afterShutdown.Status == TomCat::AssetLoadStatus::NotInitialized,
			"AssetManager accepted new async work after shutdown began");
		jobs.Configure({});
	}

	void TestOfflineTextureArtifacts()
	{
		TomCat::ImporterRegistry registry;
		registry.RegisterBuiltInImporters();
		const std::shared_ptr<const TomCat::IAssetImporter> importer =
			registry.Find(TomCat::AssetType::Texture2D);
		Require(importer && importer->GetVersion() >= 3,
			"offline texture importer is not registered");

		const std::vector<uint8_t> source = MakeFourByFourBMP();
		uint64_t textureReservation = 0;
		uint32_t inspectedWidth = 0, inspectedHeight = 0;
		std::string error;
		Require(TomCat::EstimateTextureBuildMemory(source, textureReservation,
			error, &inspectedWidth, &inspectedHeight)
			&& inspectedWidth == 4 && inspectedHeight == 4
			&& textureReservation > source.size() * 6,
			"texture reservation still scales only with compressed source bytes");
		TomCat::AssetImportRequest request;
		request.Type = TomCat::AssetType::Texture2D;
		request.SourceBytes = source;
		request.Platform = "editor";
		TomCat::AssetImportResult rgbaResult = importer->Import(request);
		Require(rgbaResult.Succeeded() && rgbaResult.Format == "texture/tctx-v1",
			"editor texture import did not emit a texture artifact");
		TomCat::TextureArtifactView rgbaView;
		Require(TomCat::ParseTextureArtifact(rgbaResult.ArtifactBytes, rgbaView, error)
			&& rgbaView.Width == 4 && rgbaView.Height == 4 && rgbaView.SRGB
			&& rgbaView.Format == TomCat::TextureArtifactFormat::RGBA8
			&& rgbaView.Mips.size() == 3
			&& rgbaView.Mips[1].Width == 2 && rgbaView.Mips[2].Width == 1,
			"RGBA texture artifact lost dimensions, color space, or mip chain");

		request.Platform = "windows-x64";
		TomCat::AssetImportResult bc3Result = importer->Import(request);
		TomCat::TextureArtifactView bc3View;
		Require(bc3Result.Succeeded()
			&& TomCat::ParseTextureArtifact(bc3Result.ArtifactBytes, bc3View, error)
			&& bc3View.Format == TomCat::TextureArtifactFormat::BC3
			&& bc3View.Mips.size() == 3,
			"Windows texture import did not emit a BC3 mip chain");
		std::vector<uint8_t> decoded;
		Require(TomCat::DecompressTextureMip(bc3View.Mips.front(), bc3View.Format,
			decoded, error) && decoded.size() == 4 * 4 * 4,
			"BC3 texture artifact could not use the runtime fallback decoder");

		request.Platform = "editor";
		request.Settings = { { "colorSpace", "Linear" }, { "mipmaps", "false" },
			{ "compression", "RGBA8" } };
		TomCat::AssetImportResult linearResult = importer->Import(request);
		TomCat::TextureArtifactView linearView;
		Require(linearResult.Succeeded()
			&& TomCat::ParseTextureArtifact(linearResult.ArtifactBytes, linearView, error)
			&& !linearView.SRGB && linearView.Mips.size() == 1,
			"linear single-mip import settings were ignored");

		linearResult.ArtifactBytes.pop_back();
		Require(!TomCat::ParseTextureArtifact(linearResult.ArtifactBytes,
			linearView, error) && !error.empty(),
			"truncated texture artifact was accepted");

		std::vector<uint8_t> oversizedHeader = source;
		WriteLittleEndian32(oversizedHeader, 18, 65'535);
		WriteLittleEndian32(oversizedHeader, 22, 65'535);
		request.SourceBytes = oversizedHeader;
		Require(!importer->Import(request).Succeeded(),
			"oversized texture dimensions reached full pixel decoding");
	}

	void TestOfflineShaderArtifacts()
	{
		TomCat::ImporterRegistry registry;
		registry.RegisterBuiltInImporters();
		const auto importer = registry.Find(TomCat::AssetType::Shader);
		Require(importer && importer->GetID() == "tomcat.shader.spirv"
			&& importer->GetVersion() >= 2,
			"compiled shader importer is not registered");
		const std::string source =
			"#type vertex\n"
			"#version 450 core\n"
			"layout(location = 0) in vec3 a_Position;\n"
			"uniform mat4 u_ViewProjection;\n"
			"void main() { gl_Position = u_ViewProjection * vec4(a_Position, 1.0); }\n"
			"#type fragment\n"
			"#version 450 core\n"
			"layout(location = 0) out vec4 o_Color;\n"
			"uniform sampler2D u_Albedo;\n"
			"void main() { o_Color = texture(u_Albedo, vec2(0.5)); }\n";
		TomCat::AssetImportRequest request;
		request.Type = TomCat::AssetType::Shader;
		request.SourcePath = "production.glsl";
		request.SourceBytes = std::span<const uint8_t>(
			reinterpret_cast<const uint8_t*>(source.data()), source.size());
		request.Backend = "opengl";
		const TomCat::AssetImportResult first = importer->Import(request);
		const TomCat::AssetImportResult second = importer->Import(request);
		Require(first.Succeeded() && first.Format == "shader/spirv-reflection-v1"
			&& first.ArtifactBytes == second.ArtifactBytes
			&& first.ArtifactBytes != std::vector<uint8_t>(request.SourceBytes.begin(),
				request.SourceBytes.end()),
			"shader import did not produce deterministic compiled output");
		TomCat::ShaderArtifactView view;
		std::string error;
		Require(TomCat::ParseShaderArtifact(first.ArtifactBytes, view, error)
			&& view.Target == TomCat::ShaderArtifactTarget::OpenGL
			&& view.Stages.size() == 2
			&& view.Stages[0].Stage == TomCat::ShaderArtifactStage::Vertex
			&& view.Stages[1].Stage == TomCat::ShaderArtifactStage::Fragment
			&& std::all_of(view.Stages.begin(), view.Stages.end(),
				[](const TomCat::ShaderArtifactStageView& stage)
				{ return stage.EntryPoint == "main" && stage.Spirv.size() >= 20; }),
			"compiled shader artifact lost its SPIR-V stages");
		Require(std::any_of(view.Resources.begin(), view.Resources.end(),
			[](const TomCat::ShaderResourceView& resource)
			{ return resource.Name == "u_Albedo"; })
			&& std::any_of(view.Resources.begin(), view.Resources.end(),
				[](const TomCat::ShaderResourceView& resource)
				{ return resource.Name == "u_ViewProjection"; }),
			"offline shader reflection omitted declared resources");

		std::vector<uint8_t> truncated = first.ArtifactBytes;
		truncated.pop_back();
		Require(!TomCat::ParseShaderArtifact(truncated, view, error),
			"truncated shader artifact was accepted");
		const std::string broken = "#type vertex\n#version 450\nvoid main( {\n";
		request.SourceBytes = std::span<const uint8_t>(
			reinterpret_cast<const uint8_t*>(broken.data()), broken.size());
		Require(!importer->Import(request).Succeeded(),
			"syntactically invalid shader source was accepted");
		Require(TomCat::AssetTypeFromPath("unsupported.comp") == TomCat::AssetType::Other
			&& TomCat::AssetTypeFromPath("unsupported.hlsl") == TomCat::AssetType::Other,
			"unsupported shader languages are still advertised as production assets");
		Require(TomCat::AssetTypeFromPath("Walk.tcanim") == TomCat::AssetType::AnimationClip
			&& TomCat::AssetTypeFromPath("Player.tccontroller")
				== TomCat::AssetType::AnimatorController
			&& TomCat::AssetTypeFromPath("Ground.tctilepalette")
				== TomCat::AssetType::TilePalette,
			"2D authoring asset extensions were not assigned stable asset types");
	}

	void TestMixedPrefixAssetImport()
	{
#ifdef TC_PLATFORM_WINDOWS
		TemporaryProject project;
		const auto scene = project.Assets / "sample.tomcat";
		const auto outside = project.Root / "AssetsOutside" / "sample.tomcat";
		WriteBytes(scene, "Scene: PrefixTest\nEntities: []\n");
		std::filesystem::create_directories(outside.parent_path());
		WriteBytes(outside, "Scene: Outside\nEntities: []\n");
		auto extended = [](const std::filesystem::path& path) {
			return std::filesystem::path(L"\\\\?\\" + path.wstring());
		};
		for (bool extendedRoot : { false, true })
		{
			TomCat::AssetRegistry registry;
			Require(registry.Initialize(extendedRoot ? extended(project.Assets) : project.Assets,
				extendedRoot ? extended(project.Library) : project.Library), "mixed-prefix registry initialization failed");
			const auto normalHandle = RequireHandle(registry, scene, TomCat::AssetType::Scene);
			const auto extendedHandle = RequireHandle(registry, extended(scene), TomCat::AssetType::Scene);
			Require(normalHandle == extendedHandle, "path prefixes created different identities for the same asset");
			Require(static_cast<uint64_t>(registry.ImportAsset(outside)) == 0 &&
				static_cast<uint64_t>(registry.ImportAsset(extended(outside))) == 0,
				"mixed-prefix containment accepted an asset outside Assets");
			Require(static_cast<uint64_t>(registry.ImportAsset(scene.string() + ".tcmeta")) == 0,
				"mixed-prefix containment imported metadata as an asset");
			registry.Shutdown();
		}
#endif
	}

	void TestCookedShaderRuntimeConsumption()
	{
		TemporaryProject project;
		const std::filesystem::path shaderPath =
			project.Assets / "Runtime Artifact.glsl";
		WriteBytes(shaderPath,
			"#type vertex\n#version 450 core\n"
			"layout(location=0) in vec3 a_Position;\n"
			"void main(){ gl_Position=vec4(a_Position,1.0); }\n"
			"#type fragment\n#version 450 core\n"
			"layout(location=0) out vec4 o_Color;\n"
			"uniform vec4 u_Tint;\n"
			"void main(){ o_Color=u_Tint; }\n");

		TomCat::AssetRegistry registry;
		Require(registry.Initialize(project.Assets, project.Library),
			"shader runtime registry did not initialize");
		const TomCat::AssetHandle shaderHandle = RequireHandle(registry,
			shaderPath, TomCat::AssetType::Shader);
		registry.Shutdown();

		TomCat::AssetJobSystem::Get().Configure(
			{ 1, 8, 64ULL * 1024ULL * 1024ULL });
		TomCat::AssetManager& manager = TomCat::AssetManager::Get();
		manager.Shutdown();
		struct ManagerCleanup final
		{
			~ManagerCleanup() { TomCat::AssetManager::Get().Shutdown(); }
		} managerCleanup;
		Require(manager.Initialize(project.Assets, project.Library),
			"shader runtime AssetManager did not initialize");
		const std::filesystem::path package =
			project.Root / "Cooked Shader Runtime.tcpak";
		Require(manager.CookToPackage(package, TomCat::AssetHandle(0)),
			"shader artifact did not cook into an asset-only package");
		manager.Shutdown();
		std::error_code removeError;
		Require(std::filesystem::remove(shaderPath, removeError) && !removeError,
			"could not remove source GLSL before the cooked runtime test");
		Require(manager.MountCookedPackage(package),
			"shader artifact package did not mount without its source GLSL");

		const TomCat::ShaderLoadResult artifact =
			manager.LoadTypedArtifact<TomCat::AssetType::Shader>(shaderHandle);
		TomCat::ShaderArtifactView parsed;
		std::string parseError;
		Require(artifact.Succeeded()
			&& TomCat::ParseShaderArtifact(artifact.Artifact.Bytes, parsed, parseError)
			&& parsed.Target == TomCat::ShaderArtifactTarget::OpenGL
			&& parsed.Stages.size() == 2,
			"cooked shader bytes were not a validated OpenGL artifact");

		TomCat::CookedAssetRange cookedRange;
		Require(manager.TryGetCookedAssetRange(shaderHandle, cookedRange)
			&& cookedRange.HasSHA256Digest && cookedRange.Size != 0,
			"tcpak v7 did not expose the Shader payload digest and byte range");
		manager.UnmountCookedPackage();

		const std::vector<uint8_t> packageBytes = ReadBinary(package);
		const uint32_t packageVersion = ReadLittleEndian32(packageBytes, 8);
		const uint32_t headerSize = ReadLittleEndian32(packageBytes, 12);
		const uint64_t entryCount = ReadLittleEndian64(packageBytes, 16);
		const uint64_t entrySize =
			TomCat::RuntimeCompatibility::TcpakEntrySizeForVersion(packageVersion);
		Require(packageVersion == TomCat::RuntimeCompatibility::TcpakVersion
			&& TomCat::RuntimeCompatibility::TcpakHasEntryDigests(packageVersion)
			&& headerSize <= packageBytes.size()
			&& entryCount <= (packageBytes.size() - headerSize) / entrySize,
			"cooked Shader package has an invalid tcpak v7 index");
		size_t shaderDigestOffset = packageBytes.size();
		for (uint64_t index = 0; index < entryCount; ++index)
		{
			const size_t indexOffset = headerSize
				+ static_cast<size_t>(index * entrySize);
			if (ReadLittleEndian64(packageBytes, indexOffset)
				== static_cast<uint64_t>(shaderHandle))
			{
				shaderDigestOffset = indexOffset
					+ static_cast<size_t>(
						TomCat::RuntimeCompatibility::TcpakLegacyEntrySize);
				break;
			}
		}
		Require(shaderDigestOffset < packageBytes.size()
			&& cookedRange.Offset <= packageBytes.size()
			&& cookedRange.Size <= packageBytes.size() - cookedRange.Offset,
			"cooked Shader index omitted its payload or digest");

		std::vector<uint8_t> damagedPayload = packageBytes;
		damagedPayload[static_cast<size_t>(cookedRange.Offset
			+ cookedRange.Size / 2)] ^= 0x01;
		const std::filesystem::path damagedPayloadPackage =
			project.Root / "Damaged Shader Payload.tcpak";
		WriteBinary(damagedPayloadPackage, damagedPayload);
		Require(!manager.MountCookedPackage(damagedPayloadPackage),
			"tcpak v7 mounted a Shader whose payload no longer matched its SHA-256");

		std::vector<uint8_t> damagedDigest = packageBytes;
		damagedDigest[shaderDigestOffset] ^= 0x01;
		const std::filesystem::path damagedDigestPackage =
			project.Root / "Damaged Shader Digest.tcpak";
		WriteBinary(damagedDigestPackage, damagedDigest);
		Require(!manager.MountCookedPackage(damagedDigestPackage),
			"tcpak v7 mounted a Shader whose index digest was corrupted");
		Require(manager.MountCookedPackage(package),
			"clean tcpak v7 did not remount after integrity rejection");

		HiddenOpenGLContext context;
		if (!context.IsAvailable())
		{
			manager.UnmountCookedPackage();
			std::cout << "SKIP cooked Shader GPU publication: "
				<< context.GetUnavailableReason() << '\n';
			return;
		}
		std::string preloadError;
		if (!manager.PreloadCookedShaders(preloadError))
			throw std::runtime_error("packaged Shader preload failed: " + preloadError);
		const TomCat::Ref<TomCat::Shader> shader = manager.LoadShader(shaderHandle);
		Require(shader && manager.LoadShader(shaderHandle) == shader,
			"cooked Shader artifact was not published or cached");
		shader->Bind();
		GLint program = 0;
		glGetIntegerv(GL_CURRENT_PROGRAM, &program);
		const auto tintResource = std::find_if(parsed.Resources.begin(),
			parsed.Resources.end(), [](const TomCat::ShaderResourceView& resource)
			{
				return resource.Kind == TomCat::ShaderResourceKind::PlainUniform
					&& resource.Name == "u_Tint";
			});
		Require(program != 0 && tintResource != parsed.Resources.end()
			&& tintResource->Location != UINT32_MAX,
			"artifact-backed OpenGL program lost its reflected uniform location");
		shader->SetFloat4("u_Tint", { 0.25f, 0.5f, 0.75f, 1.0f });
		GLfloat tint[4] = {};
		glGetUniformfv(static_cast<GLuint>(program),
			static_cast<GLint>(tintResource->Location), tint);
		Require(tint[0] == 0.25f && tint[1] == 0.5f
			&& tint[2] == 0.75f && tint[3] == 1.0f,
			"artifact reflection did not address the optimized OpenGL uniform");
		shader->Unbind();
		glGetIntegerv(GL_CURRENT_PROGRAM, &program);
		Require(program == 0,
			"artifact-backed OpenGL program did not unbind cleanly");

		manager.Release(shaderHandle);
		const TomCat::Ref<TomCat::Shader> rebuilt =
			manager.LoadShader(shaderHandle);
		Require(rebuilt && rebuilt != shader,
			"Shader release did not invalidate the artifact-backed GPU cache");
		manager.UnmountCookedPackage();
		std::cout << "PASS cooked Shader artifact published directly to OpenGL\n";
	}

	void TestCanonicalMaterialArtifacts()
	{
		TomCat::ImporterRegistry registry;
		registry.RegisterBuiltInImporters();
		const auto importer = registry.Find(TomCat::AssetType::Material);
		Require(importer && importer->GetID() == "tomcat.material.canonical"
			&& importer->GetVersion() >= 2,
			"canonical material importer is not registered");
		const std::string source =
			"SchemaVersion: 1\n"
			"Shader: 101\n"
			"Textures:\n"
			"  Normal: 303\n"
			"  Albedo: 202\n"
			"Parameters:\n"
			"  Tint:\n    Type: Float4\n    Value: [1.0, 0.5, 0.25, 1.0]\n"
			"  Roughness:\n    Type: Float\n    Value: 0.75\n"
			"  TwoSided:\n    Type: Bool\n    Value: true\n";
		TomCat::AssetImportRequest request;
		request.Type = TomCat::AssetType::Material;
		request.SourcePath = "surface.tcmat";
		request.SourceBytes = std::span<const uint8_t>(
			reinterpret_cast<const uint8_t*>(source.data()), source.size());
		const TomCat::AssetImportResult result = importer->Import(request);
		Require(result.Succeeded() && result.Format == "material/canonical-v1",
			"material source was not normalized into a canonical artifact");
		TomCat::MaterialArtifactView view;
		std::string error;
		Require(TomCat::ParseMaterialArtifact(result.ArtifactBytes, view, error)
			&& static_cast<uint64_t>(view.Shader) == 101
			&& view.Textures.size() == 2 && view.Textures[0].Name == "Albedo"
			&& static_cast<uint64_t>(view.Textures[0].Texture) == 202
			&& view.Parameters.size() == 3
			&& view.Parameters[0].Name == "Roughness"
			&& std::abs(view.Parameters[0].AsFloat() - 0.75f) < 0.0001f
			&& view.Parameters[1].Name == "Tint"
			&& view.Parameters[1].ComponentCount() == 4
			&& view.Parameters[2].Name == "TwoSided"
			&& view.Parameters[2].AsBool(),
			"canonical material artifact lost sorted bindings or typed values");
		std::vector<TomCat::TypedAssetDependency> dependencies;
		Require(TomCat::ParseMaterialSourceDependencies(request.SourceBytes,
			dependencies, error) && dependencies.size() == 3
			&& dependencies[0].ExpectedType == TomCat::AssetType::Shader
			&& dependencies[1].Name == "Albedo"
			&& dependencies[1].ExpectedType == TomCat::AssetType::Texture2D,
			"material source did not expose typed shader/texture dependencies");
		std::vector<uint8_t> truncated = result.ArtifactBytes;
		truncated.pop_back();
		Require(!TomCat::ParseMaterialArtifact(truncated, view, error),
			"truncated material artifact was accepted");

		const std::string reordered =
			"Shader: 101\nSchemaVersion: 1\n"
			"Parameters:\n"
			"  TwoSided: { Value: true, Type: Bool }\n"
			"  Roughness: { Value: 0.75, Type: Float }\n"
			"  Tint: { Value: [1.0, 0.5, 0.25, 1.0], Type: Float4 }\n"
			"Textures: { Albedo: 202, Normal: 303 }\n";
		request.SourceBytes = std::span<const uint8_t>(
			reinterpret_cast<const uint8_t*>(reordered.data()), reordered.size());
		Require(importer->Import(request).ArtifactBytes == result.ArtifactBytes,
			"material normalization depends on YAML key order or formatting");
		const std::string invalid =
			"SchemaVersion: 1\nShader: 101\nUnknown: true\n";
		request.SourceBytes = std::span<const uint8_t>(
			reinterpret_cast<const uint8_t*>(invalid.data()), invalid.size());
		Require(!importer->Import(request).Succeeded(),
			"material with an unknown schema field was accepted");
	}

	void TestTypedMaterialDependencyLoading()
	{
		TemporaryProject project;
		const std::filesystem::path shaderPath = project.Assets / "surface.glsl";
		const std::filesystem::path texturePath = project.Assets / "surface.bmp";
		const std::filesystem::path materialPath = project.Assets / "surface.tcmat";
		const std::filesystem::path meshPath = project.Assets / "surface.obj";
		const std::filesystem::path audioPath = project.Assets / "surface.wav";
		WriteBytes(shaderPath,
			"#type vertex\n#version 450 core\n"
			"layout(location=0) in vec3 a_Position;\n"
			"void main(){ gl_Position=vec4(a_Position,1.0); }\n"
			"#type fragment\n#version 450 core\n"
			"layout(location=0) out vec4 o_Color;\n"
			"void main(){ o_Color=vec4(1.0); }\n");
		const std::vector<uint8_t> bitmap = MakeFourByFourBMP();
		WriteBinary(texturePath, bitmap);
		WriteBytes(meshPath,
			"v 0 0 0\nv 1 0 0\nv 0 1 0\n"
			"vt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n");
		WriteBinary(audioPath, MakeEightBitWave());

		TomCat::AssetRegistry registry;
		Require(registry.Initialize(project.Assets, project.Library),
			"typed material test registry did not initialize");
		const TomCat::AssetHandle shader = RequireHandle(registry,
			shaderPath, TomCat::AssetType::Shader);
		const TomCat::AssetHandle texture = RequireHandle(registry,
			texturePath, TomCat::AssetType::Texture2D);
		const TomCat::AssetHandle mesh = RequireHandle(registry,
			meshPath, TomCat::AssetType::Mesh);
		const TomCat::AssetHandle audio = RequireHandle(registry,
			audioPath, TomCat::AssetType::Audio);
		const std::string materialSource =
			"SchemaVersion: 1\nShader: " + std::to_string(static_cast<uint64_t>(shader))
			+ "\nTextures:\n  Albedo: "
			+ std::to_string(static_cast<uint64_t>(texture))
			+ "\nParameters:\n  Exposure: { Type: Float, Value: 1.0 }\n";
		WriteBytes(materialPath, materialSource);
		const TomCat::AssetHandle material = RequireHandle(registry,
			materialPath, TomCat::AssetType::Material);
		registry.Shutdown();

		TomCat::AssetManager& manager = TomCat::AssetManager::Get();
		manager.Shutdown();
		TomCat::AssetJobSystem::Get().Configure({ 1, 8, 64ULL * 1024ULL * 1024ULL });
		Require(manager.Initialize(project.Assets, project.Library),
			"typed material test manager did not initialize");
		std::vector<TomCat::AssetHandle> expected = { shader, texture };
		std::sort(expected.begin(), expected.end(), [](TomCat::AssetHandle left,
			TomCat::AssetHandle right)
			{ return static_cast<uint64_t>(left) < static_cast<uint64_t>(right); });
		Require(manager.GetDatabase().GetDependencies(material) == expected,
			"typed material references were not discovered by the dependency graph");
		// Submit every production artifact type through the same one-worker pool.
		// Each typed continuation is queued only after its untyped producer, so this
		// also guards the FIFO condition that prevents continuation deadlock.
		std::future<TomCat::DecodedMaterialLoadResult> materialFuture =
			manager.LoadMaterialAsync(material);
		std::future<TomCat::ShaderLoadResult> shaderFuture =
			manager.LoadTypedArtifactAsync<TomCat::AssetType::Shader>(shader);
		std::future<TomCat::TypedAssetLoadResult<TomCat::AssetType::Texture2D>>
			textureFuture = manager.LoadTypedArtifactAsync<
				TomCat::AssetType::Texture2D>(texture);
		std::future<TomCat::DecodedMeshLoadResult> meshFuture =
			manager.LoadMeshAsync(mesh);
		std::future<TomCat::AudioLoadResult> audioFuture =
			manager.LoadTypedArtifactAsync<TomCat::AssetType::Audio>(audio);
		TomCat::DecodedMaterialLoadResult loadedMaterial = materialFuture.get();
		TomCat::ShaderLoadResult loadedShader = shaderFuture.get();
		auto loadedTexture = textureFuture.get();
		TomCat::DecodedMeshLoadResult loadedMesh = meshFuture.get();
		TomCat::AudioLoadResult loadedAudio = audioFuture.get();
		TomCat::ShaderArtifactView parsedShader;
		TomCat::TextureArtifactView parsedTexture;
		std::string error;
		Require(loadedMaterial.Succeeded()
			&& loadedMaterial.Format == "material/canonical-v1"
			&& loadedMaterial.DependencyKeys.size() == 2
			&& loadedMaterial.Asset.Shader == shader
			&& loadedMaterial.Asset.Textures.size() == 1
			&& loadedMaterial.Asset.Textures[0].Name == "Albedo"
			&& loadedMaterial.Asset.Textures[0].Texture == texture
			&& loadedMaterial.Asset.Parameters.size() == 1
			&& loadedMaterial.Asset.Parameters[0].Name == "Exposure"
			&& loadedMaterial.Asset.Parameters[0].AsFloat() == 1.0f,
			"typed async material load did not import dependencies and decode its artifact");
		Require(loadedShader.Succeeded()
			&& loadedShader.Artifact.Format == "shader/spirv-reflection-v1"
			&& TomCat::ParseShaderArtifact(loadedShader.Artifact.Bytes,
				parsedShader, error)
			&& parsedShader.Stages.size() == 2,
			"typed async shader load did not return a validated SPIR-V artifact");
		Require(loadedTexture.Succeeded()
			&& TomCat::ParseTextureArtifact(loadedTexture.Artifact.Bytes,
				parsedTexture, error)
			&& parsedTexture.Width == 4 && parsedTexture.Height == 4,
			"typed async texture load did not return an offline texture artifact");
		Require(loadedMesh.Succeeded()
			&& loadedMesh.Format == "mesh/packed-p3n3uv2-u32-v1"
			&& loadedMesh.Asset.GetVertexCount() == 3
			&& loadedMesh.Asset.GetIndexCount() == 3
			&& loadedMesh.Asset.GetPackedVertices().size()
				== 3 * TomCat::MeshArtifactVertexStride
			&& loadedMesh.Asset.GetPackedIndices().size() == 3 * sizeof(uint32_t),
			"typed async mesh load did not return validated packed geometry");
		const TomCat::Ref<TomCat::AudioClip> parsedAudio = TomCat::AudioClip::Decode(
			loadedAudio.Artifact.Bytes, error);
		Require(loadedAudio.Succeeded()
			&& loadedAudio.Artifact.Format == "audio/wav-pcm16-stream-v1"
			&& parsedAudio && parsedAudio->GetBitsPerSample() == 16,
			"typed async audio load did not return canonical streaming WAV data");
		const TomCat::MeshLoadResult wrongType =
			manager.LoadTypedArtifact<TomCat::AssetType::Mesh>(material);
		Require(!wrongType.Succeeded()
			&& wrongType.Status == TomCat::AssetLoadStatus::UnsupportedType,
			"typed loader accepted an artifact of the wrong asset type");
		const TomCat::DecodedMeshLoadResult wrongDecodedType =
			manager.LoadMesh(material);
		Require(!wrongDecodedType.Succeeded()
			&& wrongDecodedType.Status == TomCat::AssetLoadStatus::UnsupportedType,
			"decoded mesh loader accepted a Material artifact");
		manager.Shutdown();
		TomCat::AssetJobSystem::Get().Configure({});
	}

	void TestCookTraversesDatabaseDependencyClosure()
	{
		TemporaryProject environment;
		const std::filesystem::path projectRoot =
			environment.Root / "CookProject";
		TomCat::ProjectConfig config;
		config.Name = "Cook Dependency Closure Regression";
		config.Template = "2D";
		config.AssetDirectory = "Assets";
		config.StartScene = "Main.tomcat";
		const TomCat::Ref<TomCat::Project> project = TomCat::Project::CreateNew(
			projectRoot / "Project.tcproj", config);
		Require(project != nullptr,
			"could not create project for cook dependency-closure regression");

		const std::filesystem::path scriptPath =
			project->GetAssetPath() / "CookClosureProbe.cs";
		const std::filesystem::path shaderPath =
			project->GetAssetPath() / "surface.glsl";
		const std::filesystem::path texturePath =
			project->GetAssetPath() / "surface.bmp";
		const std::filesystem::path materialOnlyTexturePath =
			project->GetAssetPath() / "material-only.bmp";
		const std::filesystem::path unusedTexturePath =
			project->GetAssetPath() / "unused.bmp";
		const std::filesystem::path materialPath =
			project->GetAssetPath() / "surface.tcmat";
		WriteBytes(scriptPath,
			"using TomCat; public sealed class CookClosureProbe : TomCatBehaviour {}\n");
		WriteBytes(shaderPath, MakeDependencyShader("cook-dependency-closure"));
		const std::vector<uint8_t> bitmap = MakeFourByFourBMP();
		WriteBinary(texturePath, bitmap);
		WriteBinary(materialOnlyTexturePath, bitmap);
		WriteBinary(unusedTexturePath, bitmap);

		TomCat::AssetManager& manager = TomCat::AssetManager::Get();
		manager.Shutdown();
		Require(manager.SetProject(project),
			"could not initialize project asset manager for dependency-closure cook");
		const TomCat::AssetHandle script = RequireHandle(manager.GetRegistry(),
			scriptPath, TomCat::AssetType::CSharpScript);
		const TomCat::AssetHandle shader = RequireHandle(manager.GetRegistry(),
			shaderPath, TomCat::AssetType::Shader);
		const TomCat::AssetHandle texture = RequireHandle(manager.GetRegistry(),
			texturePath, TomCat::AssetType::Texture2D);
		const TomCat::AssetHandle materialOnlyTexture = RequireHandle(
			manager.GetRegistry(), materialOnlyTexturePath,
			TomCat::AssetType::Texture2D);
		const TomCat::AssetHandle unusedTexture = RequireHandle(manager.GetRegistry(),
			unusedTexturePath, TomCat::AssetType::Texture2D);
		const TomCat::AssetImportSettings spriteSettings = {
			{ "SpriteMode", "Multiple" },
			{ "SpriteAtlasSchema", "2" },
			{ "Sprite.surface.Name", "Surface Slice" },
			{ "Sprite.surface.Rect", "0,0,2,2" },
			{ "Sprite.surface.Pivot", "0.5,0.5" },
			{ "Sprite.surface.PixelsPerUnit", "100" },
			{ "Sprite.surface.Border", "0,0,0,0" }
		};
		Require(manager.GetRegistry().SetImportSettings(texture, spriteSettings),
			"could not configure sprite-atlas fixture");
		TomCat::AssetLoadOptions atlasOptions;
		atlasOptions.Platform = "windows-x64";
		atlasOptions.Backend = "opengl";
		const TomCat::AssetLoadResult atlasArtifact =
			manager.GetDatabase().LoadArtifact(texture, std::move(atlasOptions));
		const TomCat::AssetMetadata* atlasMetadata =
			manager.GetRegistry().GetMetadata(texture);
		Require(atlasArtifact.Succeeded() && atlasMetadata
			&& atlasMetadata->SubAssets.size() == 1,
			"could not import sprite sub-asset fixture");
		const TomCat::AssetHandle textureSlice =
			atlasMetadata->SubAssets.front().Handle;
		Require(static_cast<uint64_t>(textureSlice) != 0,
			"sprite sub-asset fixture did not receive a stable handle");
		WriteBytes(materialPath,
			"SchemaVersion: 1\nShader: "
			+ std::to_string(static_cast<uint64_t>(shader))
			+ "\nTextures:\n  Albedo: "
			+ std::to_string(static_cast<uint64_t>(textureSlice))
			+ "\n  MaterialOnly: "
			+ std::to_string(static_cast<uint64_t>(materialOnlyTexture))
			+ "\nParameters:\n  Exposure: { Type: Float, Value: 1.0 }\n");
		auto prefabSource = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity prefabRoot = prefabSource->CreateEntity("Direct Sprite Prefab");
		prefabRoot.AddComponent<TomCat::SpriteRenderer>().SpriteHandle = textureSlice;
		TomCat::PrefabArchive prefabArchive;
		std::string prefabError;
		Require(TomCat::PrefabArchiveCodec::CaptureSubtree(prefabSource, prefabRoot,
			prefabArchive, prefabError),
			"could not capture direct Prefab -> Sprite dependency fixture");
		std::string prefabDocument;
		Require(TomCat::PrefabArchiveCodec::Encode(prefabArchive, prefabDocument,
			prefabError),
			"could not encode direct Prefab -> Sprite dependency fixture");
		const std::filesystem::path prefabPath =
			project->GetAssetPath() / "DirectSprite.tcprefab";
		WriteBytes(prefabPath, prefabDocument);
		Require(manager.Refresh(),
			"could not discover material and Prefab fixtures for dependency-closure cook");
		const TomCat::AssetHandle material = RequireHandle(manager.GetRegistry(),
			materialPath, TomCat::AssetType::Material);
		const TomCat::AssetHandle prefab = RequireHandle(manager.GetRegistry(),
			prefabPath, TomCat::AssetType::Prefab);

		auto scene = TomCat::CreateRef<TomCat::Scene>();
		scene->SetSceneName("Material dependency closure");
		TomCat::Entity entity = scene->CreateEntity("Material owner");
		entity.AddComponent<TomCat::SpriteRenderer>().SpriteHandle = textureSlice;
		auto& scripts = entity.AddComponent<TomCat::CSharpScripts>();
		TomCat::CSharpScriptEntry attachment;
		attachment.ScriptAsset = script;
		attachment.LastKnownClassName = "CookClosureProbe";
		attachment.Fields.emplace_back("11111111111111111111111111111111",
			"Surface", TomCat::ScriptFieldType::AssetRef,
			static_cast<uint64_t>(material),
			"TomCat.AssetRef<TomCat.MaterialAsset>");
		attachment.Fields.emplace_back("22222222222222222222222222222222",
			"Template", TomCat::ScriptFieldType::AssetRef,
			static_cast<uint64_t>(prefab), "TomCat.PrefabAsset");
		scripts.Scripts.push_back(std::move(attachment));
		const std::filesystem::path scenePath =
			project->GetAssetPath() / "Main.tomcat";
		TomCat::SceneSerializer sceneWriter(scene);
		Require(sceneWriter.Serialize(scenePath),
			"could not serialize Scene -> Material dependency fixture");
		const TomCat::AssetHandle sceneHandle = RequireHandle(manager.GetRegistry(),
			scenePath, TomCat::AssetType::Scene);
		Require(project->SetStartSceneHandle(sceneHandle) && project->Save(),
			"could not configure the dependency-closure entry scene");
		Require(manager.Refresh(),
			"could not rebuild Scene and Material dependency graphs before cook");

		std::vector<TomCat::AssetHandle> expectedSceneDependencies = {
			script, material, prefab, textureSlice
		};
		std::sort(expectedSceneDependencies.begin(), expectedSceneDependencies.end(),
			[](TomCat::AssetHandle left, TomCat::AssetHandle right)
			{
				return static_cast<uint64_t>(left) < static_cast<uint64_t>(right);
			});
		std::vector<TomCat::AssetHandle> expectedMaterialDependencies = {
			shader, textureSlice, materialOnlyTexture
		};
		std::sort(expectedMaterialDependencies.begin(),
			expectedMaterialDependencies.end(),
			[](TomCat::AssetHandle left, TomCat::AssetHandle right)
			{
				return static_cast<uint64_t>(left) < static_cast<uint64_t>(right);
			});
		const TomCat::AssetDependencySnapshot sceneSnapshot =
			manager.GetDatabase().GetDependencySnapshot(sceneHandle);
		const TomCat::AssetDependencySnapshot prefabSnapshot =
			manager.GetDatabase().GetDependencySnapshot(prefab);
		Require(sceneSnapshot.Dependencies == expectedSceneDependencies
			&& manager.GetDatabase().GetDependencies(material)
				== expectedMaterialDependencies
			&& prefabSnapshot.Dependencies
				== std::vector<TomCat::AssetHandle>{ textureSlice }
			&& std::find(sceneSnapshot.ArtifactDependencies.begin(),
				sceneSnapshot.ArtifactDependencies.end(), texture)
				!= sceneSnapshot.ArtifactDependencies.end()
			&& prefabSnapshot.ArtifactDependencies
				== std::vector<TomCat::AssetHandle>{ texture },
			"database did not preserve logical Scene/Prefab Sprite handles while resolving their atlas artifact owner");
		const std::vector<TomCat::AssetHandle> atlasDependents =
			manager.GetDatabase().GetDependents(texture);
		Require(std::find(atlasDependents.begin(), atlasDependents.end(), material)
				!= atlasDependents.end()
			&& std::find(atlasDependents.begin(), atlasDependents.end(), sceneHandle)
				!= atlasDependents.end()
			&& std::find(atlasDependents.begin(), atlasDependents.end(), prefab)
				!= atlasDependents.end(),
			"atlas source changes no longer invalidate logical Sprite dependents");
		const TomCat::AssetDependencySnapshot materialSnapshot =
			manager.GetDatabase().GetDependencySnapshot(material);
		std::vector<TomCat::AssetHandle> expectedArtifactDependencies = {
			shader, texture, materialOnlyTexture
		};
		std::sort(expectedArtifactDependencies.begin(),
			expectedArtifactDependencies.end(),
			[](TomCat::AssetHandle left, TomCat::AssetHandle right)
			{
				return static_cast<uint64_t>(left) < static_cast<uint64_t>(right);
			});
		Require(materialSnapshot.Dependencies == expectedMaterialDependencies
			&& materialSnapshot.ArtifactDependencies
				== expectedArtifactDependencies
			&& materialSnapshot.SourceSHA256.size() == 64
			&& materialSnapshot.Revision != 0,
			"Material dependency graph did not retain its source snapshot");
		TomCat::AssetLoadOptions materialOptions;
		materialOptions.Platform = "windows-x64";
		materialOptions.Backend = "opengl";
		const TomCat::AssetLoadResult materialArtifact =
			manager.GetDatabase().LoadArtifact(material, std::move(materialOptions));
		const TomCat::AssetDependencySnapshot postImportSnapshot =
			manager.GetDatabase().GetDependencySnapshot(material);
		Require(materialArtifact.Succeeded()
			&& materialArtifact.Artifact.SourceSHA256
				== materialSnapshot.SourceSHA256
			&& postImportSnapshot.Revision == materialSnapshot.Revision
			&& materialArtifact.Artifact.DependencyKeys.size() == 3
			&& std::find(materialArtifact.Artifact.DependencyKeys.begin(),
				materialArtifact.Artifact.DependencyKeys.end(),
				atlasArtifact.Artifact.ArtifactKey)
				!= materialArtifact.Artifact.DependencyKeys.end(),
			"Material import did not use one dependency/source snapshot or resolve its Sprite to the atlas artifact key");

		Require(manager.GetDatabase().SetDependencies(material,
			expectedMaterialDependencies),
			"could not install a temporary explicit Material dependency graph");
		const TomCat::AssetDependencySnapshot explicitSnapshot =
			manager.GetDatabase().GetDependencySnapshot(material);
		Require(explicitSnapshot.Revision != postImportSnapshot.Revision
			&& explicitSnapshot.SourceSHA256.empty(),
			"explicit dependency mutation did not advance graph revision");
		Require(manager.Refresh(),
			"could not restore source-discovered Material dependencies");
		const TomCat::AssetDependencySnapshot restoredSnapshot =
			manager.GetDatabase().GetDependencySnapshot(material);
		Require(restoredSnapshot.Revision != explicitSnapshot.Revision
			&& restoredSnapshot.Dependencies == expectedMaterialDependencies
			&& restoredSnapshot.ArtifactDependencies
				== expectedArtifactDependencies
			&& restoredSnapshot.SourceSHA256 == materialSnapshot.SourceSHA256,
			"source dependency rebuild did not advance revision and restore its exact snapshot");

		std::ostringstream manifest;
		manifest << "{\"version\":1,\"scripts\":[{\"assetHandle\":"
			<< static_cast<uint64_t>(script)
			<< ",\"typeName\":\"CookClosureProbe\",\"executionOrder\":0,"
				"\"disallowMultiple\":false,\"lifecycle\":0,\"fields\":[]}]}";
		const std::string manifestJson = manifest.str();
		Require(manager.SetManagedCookPayload(
			MakeManagedAssemblyFixture(manifestJson), manifestJson,
			"cook-dependency-closure"),
			"could not install managed payload for dependency-closure cook");
		const std::filesystem::path package =
			projectRoot / "Build" / "DependencyClosure.tcpak";

		// Exercise every Cook metadata read while Refresh repeatedly updates the
		// Registry container. Cook must use values copied under AssetDatabase's
		// metadata gate and either validate that snapshot or reject it cleanly.
		const std::filesystem::path refreshStaging = projectRoot / "RefreshStaging";
		std::filesystem::create_directories(refreshStaging);
		std::vector<std::filesystem::path> stagedRefreshAssets;
		for (uint32_t index = 0; index < 12; ++index)
		{
			const std::filesystem::path staged =
				refreshStaging / ("refresh-" + std::to_string(index) + ".bmp");
			WriteBinary(staged, bitmap);
			stagedRefreshAssets.push_back(staged);
		}
		std::atomic_bool beginRefresh = false;
		std::atomic_bool refreshFailed = false;
		std::atomic_uint32_t refreshCount = 0;
		std::thread metadataRefresher([&]()
		{
			while (!beginRefresh.load())
				std::this_thread::yield();
			for (uint32_t index = 0; index < stagedRefreshAssets.size(); ++index)
			{
				std::error_code moveError;
				std::filesystem::rename(stagedRefreshAssets[index],
					project->GetAssetPath()
						/ ("refresh-" + std::to_string(index) + ".bmp"),
					moveError);
				if (moveError || !manager.GetDatabase().RefreshRegistry())
				{
					refreshFailed = true;
					return;
				}
				++refreshCount;
			}
		});
		beginRefresh = true;
		const bool cooked = manager.CookToPackage(package);
		metadataRefresher.join();
		Require(!refreshFailed && refreshCount != 0,
			"concurrent metadata Refresh fixture did not execute successfully");
		Require(cooked,
			"cook rejected the Scene -> Material closure during safe metadata Refresh");
		manager.Shutdown();

		Require(manager.MountCookedPackage(package),
			"could not mount dependency-closure package");
		std::vector<uint8_t> bytes;
		TomCat::AssetType type = TomCat::AssetType::None;
		Require(manager.ReadAssetBytes(sceneHandle, bytes, &type)
			&& type == TomCat::AssetType::Scene,
			"dependency-closure package omitted its Scene root");
		Require(manager.ReadAssetBytes(material, bytes, &type)
			&& type == TomCat::AssetType::Material,
			"dependency-closure package omitted its Material");
		TomCat::MaterialArtifactView cookedMaterial;
		std::string artifactError;
		Require(TomCat::ParseMaterialArtifact(bytes, cookedMaterial, artifactError)
			&& cookedMaterial.Textures.size() == 2
			&& std::any_of(cookedMaterial.Textures.begin(),
				cookedMaterial.Textures.end(), [textureSlice](const auto& binding)
				{ return binding.Texture == textureSlice; })
			&& std::any_of(cookedMaterial.Textures.begin(),
				cookedMaterial.Textures.end(),
				[materialOnlyTexture](const auto& binding)
				{ return binding.Texture == materialOnlyTexture; }),
			"cooked Material did not preserve all logical Texture handles");
		Require(manager.ReadAssetBytes(prefab, bytes, &type)
			&& type == TomCat::AssetType::Prefab,
			"dependency-closure package omitted its direct Sprite Prefab");
		Require(manager.ReadAssetBytes(shader, bytes, &type)
			&& type == TomCat::AssetType::Shader,
			"dependency-closure package omitted its Shader");
		Require(manager.ReadAssetBytes(textureSlice, bytes, &type)
			&& type == TomCat::AssetType::Texture2D,
			"dependency-closure package omitted its exact Sprite sub-asset");
		TomCat::ResolvedSpriteAsset cookedSprite;
		std::span<const uint8_t> cookedAtlas;
		Require(TomCat::ParseCookedSpriteSubAsset(bytes, cookedSprite, cookedAtlas)
			&& cookedSprite.TextureHandle == texture && !cookedAtlas.empty(),
			"packaged Sprite sub-asset did not retain its source atlas payload");
		Require(manager.ReadAssetBytes(materialOnlyTexture, bytes, &type)
			&& type == TomCat::AssetType::Texture2D,
			"dependency-closure package omitted the Material-only Texture");
		Require(!manager.ReadAssetBytes(texture, bytes),
			"Sprite source atlas leaked into the strict logical-handle closure");
		Require(!manager.ReadAssetBytes(unusedTexture, bytes),
			"unreferenced texture leaked into strict dependency-closure package");
		manager.Shutdown();
	}

	void TestExplicitDependencyRefreshesTransferredSpriteOwner()
	{
		TemporaryProject project;
		const std::filesystem::path dependentPath =
			project.Assets / "explicit-owner-probe.cs";
		const std::filesystem::path shaderPath =
			project.Assets / "explicit-owner.glsl";
		const std::filesystem::path firstOwnerPath =
			project.Assets / "owner-a.bmp";
		const std::filesystem::path secondOwnerPath =
			project.Assets / "owner-b.bmp";
		WriteBytes(dependentPath,
			"public sealed class ExplicitOwnerProbe {}\n");
		WriteBytes(shaderPath, MakeDependencyShader("explicit-owner"));
		const std::vector<uint8_t> bitmap = MakeFourByFourBMP();
		WriteBinary(firstOwnerPath, bitmap);
		WriteBinary(secondOwnerPath, bitmap);

		TomCat::AssetRegistry registry;
		Require(registry.Initialize(project.Assets, project.Library),
			"owner-transfer registry initialization failed");
		const TomCat::AssetHandle dependent = RequireHandle(registry,
			dependentPath, TomCat::AssetType::CSharpScript);
		const TomCat::AssetHandle shader = RequireHandle(registry,
			shaderPath, TomCat::AssetType::Shader);
		const TomCat::AssetHandle firstOwner = RequireHandle(registry,
			firstOwnerPath, TomCat::AssetType::Texture2D);
		const TomCat::AssetHandle secondOwner = RequireHandle(registry,
			secondOwnerPath, TomCat::AssetType::Texture2D);

		TomCat::AssetSubAsset requested;
		requested.PersistentID = "sprite:transferred";
		requested.Name = "Transferred Slice";
		requested.Type = TomCat::AssetType::Texture2D;
		std::vector<TomCat::AssetSubAsset> assigned;
		Require(registry.SynchronizeSubAssets(firstOwner, { requested }, &assigned)
			&& assigned.size() == 1,
			"could not seed the transferred Sprite child");
		const TomCat::AssetHandle slice = assigned.front().Handle;

		TomCat::AssetDatabase database;
		Require(database.Initialize(registry, project.Library),
			"owner-transfer asset database initialization failed");
		Require(database.SetDependencies(dependent, { slice }),
			"could not set a non-discoverable logical Sprite dependency");
		Require(database.SetDependencies(slice, { shader }),
			"could not use a live Sprite child as an explicit graph source");
		Require(database.GetDependencySnapshot(dependent).ArtifactDependencies ==
				std::vector<TomCat::AssetHandle>{ firstOwner }
			&& database.GetDependencySnapshot(slice).Dependencies ==
				std::vector<TomCat::AssetHandle>{ shader },
			"initial explicit dependency graph did not resolve the Sprite owner");

		const auto writeOwnerMetadata = [&](const std::filesystem::path& source,
			TomCat::AssetHandle owner, bool ownsSlice)
		{
			std::string document =
				"SchemaVersion: 2\nAsset:\n  Handle: "
				+ std::to_string(static_cast<uint64_t>(owner))
				+ "\n  Type: Texture2D\n  ImportSettings: {}\n  SubAssets:";
			if (ownsSlice)
			{
				document += "\n    - Handle: "
					+ std::to_string(static_cast<uint64_t>(slice))
					+ "\n      PersistentID: sprite:transferred"
						"\n      Name: Transferred Slice"
						"\n      Type: Texture2D\n";
			}
			else
				document += " []\n";
			WriteBytes(TomCat::AssetRegistry::GetMetadataPath(source), document);
		};
		writeOwnerMetadata(firstOwnerPath, firstOwner, false);
		writeOwnerMetadata(secondOwnerPath, secondOwner, true);
		Require(database.RefreshRegistry(),
			"registry refresh rejected the valid Sprite owner transfer");

		const TomCat::AssetSubAsset* transferredChild = nullptr;
		const TomCat::AssetMetadata* transferredOwner =
			registry.GetSubAssetOwner(slice, &transferredChild);
		const TomCat::AssetDependencySnapshot transferred =
			database.GetDependencySnapshot(dependent);
		const TomCat::AssetDependencySnapshot childSource =
			database.GetDependencySnapshot(slice);
		const std::vector<TomCat::AssetHandle> oldOwnerDependents =
			database.GetDependents(firstOwner);
		const std::vector<TomCat::AssetHandle> newOwnerDependents =
			database.GetDependents(secondOwner);
		Require(transferredOwner && transferredChild
			&& transferredOwner->Handle == secondOwner
			&& transferred.Dependencies ==
				std::vector<TomCat::AssetHandle>{ slice }
			&& transferred.ArtifactDependencies ==
				std::vector<TomCat::AssetHandle>{ secondOwner }
			&& childSource.Dependencies ==
				std::vector<TomCat::AssetHandle>{ shader }
			&& std::find(oldOwnerDependents.begin(), oldOwnerDependents.end(),
				dependent) == oldOwnerDependents.end()
			&& std::find(newOwnerDependents.begin(), newOwnerDependents.end(),
				dependent) != newOwnerDependents.end(),
			"refresh did not re-resolve explicit dependencies to the new Sprite owner");

		database.Shutdown();
		registry.Shutdown();
		Require(registry.Initialize(project.Assets, project.Library),
			"owner-transfer registry restart failed");
		Require(database.Initialize(registry, project.Library),
			"owner-transfer dependency cache restart failed");
		Require(database.GetDependencySnapshot(slice).Dependencies ==
				std::vector<TomCat::AssetHandle>{ shader }
			&& database.GetDependencySnapshot(dependent).ArtifactDependencies ==
				std::vector<TomCat::AssetHandle>{ secondOwner },
			"dependency cache dropped the live Sprite graph source or its new owner");

		writeOwnerMetadata(secondOwnerPath, secondOwner, false);
		Require(database.RefreshRegistry()
			&& registry.GetSubAssetOwner(slice) == nullptr,
			"registry refresh did not remove the transferred Sprite child");
		const TomCat::AssetDependencySnapshot removedChildSource =
			database.GetDependencySnapshot(slice);
		Require(removedChildSource.Dependencies.empty()
			&& removedChildSource.ArtifactDependencies.empty(),
			"refresh retained an explicit dependency graph whose Sprite source disappeared");
		database.Shutdown();
		registry.Shutdown();
	}

	void TestPartialArtifactOwnerRefreshMergesResolvedAndMissingAliases()
	{
		TemporaryProject project;
		const std::filesystem::path dependentPath =
			project.Assets / "partial-owner-probe.cs";
		const std::filesystem::path ownerAPath =
			project.Assets / "partial-owner-a.bmp";
		const std::filesystem::path ownerBPath =
			project.Assets / "partial-owner-b.bmp";
		const std::filesystem::path ownerCPath =
			project.Assets / "partial-owner-c.bmp";
		WriteBytes(dependentPath,
			"public sealed class PartialOwnerProbe {}\n");
		const std::vector<uint8_t> bitmap = MakeFourByFourBMP();
		WriteBinary(ownerAPath, bitmap);
		WriteBinary(ownerBPath, bitmap);
		WriteBinary(ownerCPath, bitmap);

		TomCat::AssetRegistry registry;
		Require(registry.Initialize(project.Assets, project.Library),
			"partial owner registry initialization failed");
		const TomCat::AssetHandle dependent = RequireHandle(registry,
			dependentPath, TomCat::AssetType::CSharpScript);
		const TomCat::AssetHandle ownerA = RequireHandle(registry,
			ownerAPath, TomCat::AssetType::Texture2D);
		const TomCat::AssetHandle ownerB = RequireHandle(registry,
			ownerBPath, TomCat::AssetType::Texture2D);
		const TomCat::AssetHandle ownerC = RequireHandle(registry,
			ownerCPath, TomCat::AssetType::Texture2D);

		TomCat::AssetSubAsset requestedA;
		requestedA.PersistentID = "sprite:partial-a";
		requestedA.Name = "Partial Slice A";
		requestedA.Type = TomCat::AssetType::Texture2D;
		TomCat::AssetSubAsset requestedB;
		requestedB.PersistentID = "sprite:partial-b";
		requestedB.Name = "Partial Slice B";
		requestedB.Type = TomCat::AssetType::Texture2D;
		std::vector<TomCat::AssetSubAsset> assignedA;
		std::vector<TomCat::AssetSubAsset> assignedB;
		Require(registry.SynchronizeSubAssets(ownerA, { requestedA }, &assignedA)
			&& assignedA.size() == 1
			&& registry.SynchronizeSubAssets(ownerB, { requestedB }, &assignedB)
			&& assignedB.size() == 1,
			"could not seed both partial owner Sprite children");
		const TomCat::AssetHandle sliceA = assignedA.front().Handle;
		const TomCat::AssetHandle sliceB = assignedB.front().Handle;
		std::vector<TomCat::AssetHandle> logicalDependencies{ sliceA, sliceB };
		std::sort(logicalDependencies.begin(), logicalDependencies.end(),
			[](TomCat::AssetHandle left, TomCat::AssetHandle right)
			{
				return static_cast<uint64_t>(left)
					< static_cast<uint64_t>(right);
			});

		TomCat::AssetDatabase database;
		Require(database.Initialize(registry, project.Library),
			"partial owner asset database initialization failed");
		Require(database.SetDependencies(dependent, logicalDependencies),
			"could not set the two logical Sprite dependencies");

		const auto writeOwnerMetadata = [&](const std::filesystem::path& source,
			TomCat::AssetHandle owner, TomCat::AssetHandle child,
			std::string_view persistentID, std::string_view name)
		{
			std::string document =
				"SchemaVersion: 2\nAsset:\n  Handle: "
				+ std::to_string(static_cast<uint64_t>(owner))
				+ "\n  Type: Texture2D\n  ImportSettings: {}\n  SubAssets:";
			if (static_cast<uint64_t>(child) != 0)
			{
				document += "\n    - Handle: "
					+ std::to_string(static_cast<uint64_t>(child))
					+ "\n      PersistentID: " + std::string(persistentID)
					+ "\n      Name: " + std::string(name)
					+ "\n      Type: Texture2D\n";
			}
			else
				document += " []\n";
			WriteBytes(TomCat::AssetRegistry::GetMetadataPath(source), document);
		};
		writeOwnerMetadata(ownerAPath, ownerA, TomCat::AssetHandle(0), {}, {});
		writeOwnerMetadata(ownerBPath, ownerB, TomCat::AssetHandle(0), {}, {});
		writeOwnerMetadata(ownerCPath, ownerC, sliceB,
			"sprite:partial-b", "Partial Slice B");
		Require(database.RefreshRegistry(),
			"registry refresh rejected the partial owner change");

		const TomCat::AssetSubAsset* currentB = nullptr;
		const TomCat::AssetMetadata* currentBOwner =
			registry.GetSubAssetOwner(sliceB, &currentB);
		const TomCat::AssetDependencySnapshot refreshed =
			database.GetDependencySnapshot(dependent);
		const auto containsHandle = [](const std::vector<TomCat::AssetHandle>& handles,
			TomCat::AssetHandle handle)
		{
			return std::find(handles.begin(), handles.end(), handle) != handles.end();
		};
		Require(registry.GetSubAssetOwner(sliceA) == nullptr
			&& currentBOwner && currentB && currentBOwner->Handle == ownerC
			&& refreshed.Dependencies == logicalDependencies
			&& containsHandle(refreshed.ArtifactDependencies, ownerA)
			&& containsHandle(refreshed.ArtifactDependencies, ownerC)
			&& containsHandle(database.GetDependents(ownerA), dependent)
			&& containsHandle(database.GetDependents(ownerC), dependent),
			"partial refresh did not retain missing owner A while adding owner C");

		database.Shutdown();
		registry.Shutdown();
		Require(registry.Initialize(project.Assets, project.Library),
			"partial owner registry restart failed");
		Require(database.Initialize(registry, project.Library),
			"partial owner dependency cache restart failed");
		const TomCat::AssetDependencySnapshot reloaded =
			database.GetDependencySnapshot(dependent);
		Require(reloaded.Dependencies == logicalDependencies
			&& containsHandle(reloaded.ArtifactDependencies, ownerA)
			&& containsHandle(reloaded.ArtifactDependencies, ownerC)
			&& containsHandle(database.GetDependents(ownerA), dependent)
			&& containsHandle(database.GetDependents(ownerC), dependent),
			"dependency cache lost the mixed missing and transferred owner aliases");
		database.Shutdown();
		registry.Shutdown();
	}

	void TestDependencyCacheRetainsMissingSpriteOwnerAlias()
	{
		TemporaryProject project;
		const std::filesystem::path shaderPath = project.Assets / "cached.glsl";
		const std::filesystem::path texturePath = project.Assets / "cached.bmp";
		const std::filesystem::path materialPath = project.Assets / "cached.tcmat";
		WriteBytes(shaderPath, MakeDependencyShader("cached-sprite-owner"));
		WriteBinary(texturePath, MakeFourByFourBMP());

		TomCat::AssetRegistry registry;
		Require(registry.Initialize(project.Assets, project.Library),
			"cache reload registry initialization failed");
		const TomCat::AssetHandle shader = RequireHandle(registry, shaderPath,
			TomCat::AssetType::Shader);
		const TomCat::AssetHandle texture = RequireHandle(registry, texturePath,
			TomCat::AssetType::Texture2D);
		const TomCat::AssetImportSettings spriteSettings = {
			{ "SpriteMode", "Multiple" },
			{ "SpriteAtlasSchema", "2" },
			{ "Sprite.cached.Name", "Cached Slice" },
			{ "Sprite.cached.Rect", "0,0,2,2" },
			{ "Sprite.cached.Pivot", "0.5,0.5" },
			{ "Sprite.cached.PixelsPerUnit", "100" },
			{ "Sprite.cached.Border", "0,0,0,0" }
		};
		Require(registry.SetImportSettings(texture, spriteSettings),
			"could not configure cached sprite atlas");

		TomCat::AssetDatabase database;
		Require(database.Initialize(registry, project.Library),
			"cache reload asset database initialization failed");
		TomCat::AssetLoadOptions atlasOptions;
		atlasOptions.Platform = "windows-x64";
		atlasOptions.Backend = "opengl";
		Require(database.LoadArtifact(texture, atlasOptions).Succeeded(),
			"could not import cached sprite atlas");
		const TomCat::AssetMetadata* atlas = registry.GetMetadata(texture);
		Require(atlas && atlas->SubAssets.size() == 1,
			"cached sprite atlas did not publish one slice");
		const TomCat::AssetHandle slice = atlas->SubAssets.front().Handle;

		WriteBytes(materialPath,
			"SchemaVersion: 1\nShader: "
			+ std::to_string(static_cast<uint64_t>(shader))
			+ "\nTextures:\n  Albedo: "
			+ std::to_string(static_cast<uint64_t>(slice))
			+ "\nParameters: {}\n");
		Require(database.RefreshRegistry(),
			"could not discover cached Material dependencies");
		const TomCat::AssetHandle material = RequireHandle(registry, materialPath,
			TomCat::AssetType::Material);
		std::vector<TomCat::AssetHandle> expectedLogical{ shader, slice };
		std::vector<TomCat::AssetHandle> expectedArtifacts{ shader, texture };
		const auto sortHandles = [](std::vector<TomCat::AssetHandle>& handles)
		{
			std::sort(handles.begin(), handles.end(),
				[](TomCat::AssetHandle left, TomCat::AssetHandle right)
				{
					return static_cast<uint64_t>(left)
						< static_cast<uint64_t>(right);
				});
		};
		sortHandles(expectedLogical);
		sortHandles(expectedArtifacts);
		const TomCat::AssetDependencySnapshot beforeRemoval =
			database.GetDependencySnapshot(material);
		Require(beforeRemoval.Dependencies == expectedLogical
			&& beforeRemoval.ArtifactDependencies == expectedArtifacts,
			"Material graph did not separate logical slice and atlas artifact dependencies");

		Require(registry.SetImportSettings(texture, { { "SpriteMode", "Single" } }),
			"could not remove cached sprite slicing settings");
		Require(database.LoadArtifact(texture, atlasOptions).Succeeded()
			&& registry.GetSubAssetOwner(slice) == nullptr,
			"switching the atlas to Single did not remove its old slice");
		const TomCat::AssetDependencySnapshot afterRemoval =
			database.GetDependencySnapshot(material);
		const std::vector<TomCat::AssetHandle> ownerDependents =
			database.GetDependents(texture);
		Require(afterRemoval.Dependencies == expectedLogical
			&& afterRemoval.ArtifactDependencies == expectedArtifacts
			&& std::find(ownerDependents.begin(), ownerDependents.end(), material)
				!= ownerDependents.end(),
			"missing Sprite slice discarded its persisted atlas-owner invalidation edge");
		const std::filesystem::path cachePath =
			project.Library / "AssetDependencies.yaml";
		Require(ReadText(cachePath).find("SchemaVersion: 2") != std::string::npos
			&& ReadText(cachePath).find("ArtifactDependencies") != std::string::npos,
			"dependency cache did not persist artifact-owner handles in schema v2");

		database.Shutdown();
		registry.Shutdown();

		TomCat::AssetRegistry reloadedRegistry;
		Require(reloadedRegistry.Initialize(project.Assets, project.Library),
			"could not reload registry after Sprite slice removal");
		TomCat::AssetDatabase reloadedDatabase;
		Require(reloadedDatabase.Initialize(reloadedRegistry, project.Library),
			"could not reload dependency cache after Sprite slice removal");
		const TomCat::AssetDependencySnapshot reloaded =
			reloadedDatabase.GetDependencySnapshot(material);
		const std::vector<TomCat::AssetHandle> reloadedOwnerDependents =
			reloadedDatabase.GetDependents(texture);
		Require(reloaded.Dependencies == expectedLogical
			&& reloaded.ArtifactDependencies == expectedArtifacts
			&& std::find(reloadedOwnerDependents.begin(),
				reloadedOwnerDependents.end(), material)
				!= reloadedOwnerDependents.end(),
			"dependency cache reload lost the missing slice -> atlas owner reverse edge");
		reloadedDatabase.Shutdown();
		reloadedRegistry.Shutdown();
	}

	void TestPackedObjMeshArtifacts()
	{
		TomCat::ImporterRegistry registry;
		registry.RegisterBuiltInImporters();
		const auto importer = registry.Find(TomCat::AssetType::Mesh);
		Require(importer && importer->GetID() == "tomcat.mesh.obj"
			&& importer->GetVersion() >= 2,
			"OBJ mesh importer is not registered");
		const std::string source =
			"v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
			"vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
			"f -4/-4 -3/-3 -2/-2 -1/-1\n";
		TomCat::AssetImportRequest request;
		request.Type = TomCat::AssetType::Mesh;
		request.SourcePath = "quad.obj";
		request.SourceBytes = std::span<const uint8_t>(
			reinterpret_cast<const uint8_t*>(source.data()), source.size());
		const TomCat::AssetImportResult first = importer->Import(request);
		const TomCat::AssetImportResult second = importer->Import(request);
		Require(first.Succeeded() && first.Format == "mesh/packed-p3n3uv2-u32-v1"
			&& first.ArtifactBytes == second.ArtifactBytes,
			"OBJ import did not emit deterministic packed geometry");
		TomCat::MeshArtifactView view;
		std::string error;
		TomCat::MeshArtifactVertex vertex;
		uint32_t lastIndex = UINT32_MAX;
		Require(TomCat::ParseMeshArtifact(first.ArtifactBytes, view, error)
			&& view.VertexCount == 4 && view.IndexCount == 6
			&& view.DecodeVertex(0, vertex)
			&& std::abs(vertex.Normal[2] - 1.0f) < 0.0001f
			&& view.DecodeIndex(5, lastIndex) && lastIndex == 3,
			"packed OBJ artifact lost geometry, generated normals, or triangulation");
		std::vector<uint8_t> truncated = first.ArtifactBytes;
		truncated.pop_back();
		Require(!TomCat::ParseMeshArtifact(truncated, view, error),
			"truncated mesh artifact was accepted");
		const std::string badFace = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 9\n";
		request.SourceBytes = std::span<const uint8_t>(
			reinterpret_cast<const uint8_t*>(badFace.data()), badFace.size());
		Require(!importer->Import(request).Succeeded(),
			"OBJ with an out-of-range face index was accepted");
		request.SourcePath = "unsupported.fbx";
		Require(!importer->Import(request).Succeeded()
			&& TomCat::AssetTypeFromPath("unsupported.fbx") == TomCat::AssetType::Mesh
			&& TomCat::AssetTypeFromPath("unsupported.gltf") == TomCat::AssetType::Mesh,
			"invalid FBX was accepted or supported model extensions were not recognized");
	}

	void TestOfflineAudioArtifact()
	{
		TomCat::ImporterRegistry registry;
		registry.RegisterBuiltInImporters();
		const auto importer = registry.Find(TomCat::AssetType::Audio);
		Require(importer && importer->GetVersion() >= 2
			&& importer->GetID() == "tomcat.audio.pcm16",
			"canonical audio importer is not registered");
		const std::vector<uint8_t> source = MakeEightBitWave();
		TomCat::AssetImportRequest request;
		request.Type = TomCat::AssetType::Audio;
		request.SourceBytes = source;
		const TomCat::AssetImportResult result = importer->Import(request);
		Require(result.Succeeded()
			&& result.Format == "audio/wav-pcm16-stream-v1"
			&& result.ArtifactBytes != source,
			"audio import did not transcode the source into a derived artifact");
		std::string error;
		const TomCat::Ref<TomCat::AudioClip> clip = TomCat::AudioClip::Decode(
			result.ArtifactBytes, error);
		Require(clip && clip->GetChannels() == 1 && clip->GetSampleRate() == 8000
			&& clip->GetBitsPerSample() == 16 && clip->GetFrameCount() == 4,
			"canonical audio artifact lost format or frame metadata");
	}

	TomCat::AssetHandle RequireHandle(TomCat::AssetRegistry& registry,
		const std::filesystem::path& path, TomCat::AssetType expected)
	{
		const TomCat::AssetHandle handle = registry.ImportAsset(path);
		const TomCat::AssetMetadata* metadata = registry.GetMetadata(handle);
		Require(static_cast<uint64_t>(handle) != 0 && metadata &&
			metadata->Type == expected && !metadata->IsMissing,
			"asset did not receive a live handle of the expected type");
		return handle;
	}

	TomCat::AssetHandle RequireHeroSubAsset(TomCat::AssetRegistry& registry,
		TomCat::AssetHandle hero)
	{
		const TomCat::AssetMetadata* metadata = registry.GetMetadata(hero);
		Require(metadata && metadata->SubAssets.size() == 1 &&
			metadata->SubAssets[0].PersistentID == "sprite:hero" &&
			static_cast<uint64_t>(metadata->SubAssets[0].Handle) != 0,
			"stable imported sub-asset was not committed to tcmeta v2");
		return metadata->SubAssets[0].Handle;
	}

	void TestMalformedFontPreflight()
	{
		TomCat::ImporterRegistry registry;
		registry.RegisterBuiltInImporters();
		const std::shared_ptr<const TomCat::IAssetImporter> importer =
			registry.Find(TomCat::AssetType::Font);
		Require(importer && importer->GetVersion() >= 2,
			"validated SFNT importer is not registered");
		auto rejects = [&](const std::vector<uint8_t>& bytes)
		{
			TomCat::AssetImportRequest request;
			request.Type = TomCat::AssetType::Font;
			request.SourceBytes = bytes;
			const TomCat::AssetImportResult result = importer->Import(request);
			return !result.Succeeded() && result.ArtifactBytes.empty()
				&& !result.Error.empty();
		};

		std::vector<uint8_t> truncatedDirectory(12, 0);
		WriteBigEndian32(truncatedDirectory, 0, 0x00010000u);
		WriteBigEndian16(truncatedDirectory, 4, 1);
		Require(rejects(truncatedDirectory),
			"SFNT with a truncated table directory was accepted");

		std::vector<uint8_t> overflowingTable(28, 0);
		WriteBigEndian32(overflowingTable, 0, 0x00010000u);
		WriteBigEndian16(overflowingTable, 4, 1);
		WriteBigEndian32(overflowingTable, 12, 0x636d6170u);
		WriteBigEndian32(overflowingTable, 20, 0xfffffff0u);
		WriteBigEndian32(overflowingTable, 24, 64);
		Require(rejects(overflowingTable),
			"SFNT with an out-of-range table was accepted");

		std::vector<uint8_t> overflowingCollectionFace(16, 0);
		WriteBigEndian32(overflowingCollectionFace, 0, 0x74746366u);
		WriteBigEndian32(overflowingCollectionFace, 4, 0x00010000u);
		WriteBigEndian32(overflowingCollectionFace, 8, 1);
		WriteBigEndian32(overflowingCollectionFace, 12, 0xfffffff0u);
		Require(rejects(overflowingCollectionFace),
			"TTC with an out-of-range face offset was accepted");

		std::vector<uint8_t> missingRequiredTables(12, 0);
		WriteBigEndian32(missingRequiredTables, 0, 0x00010000u);
		Require(rejects(missingRequiredTables),
			"SFNT without required rendering tables was accepted");
	}

	void TestDeterministicImportPipeline()
	{
		TemporaryProject project;
		const std::filesystem::path heroPath = project.Assets / "hero.png";
		const std::filesystem::path dependencyPath = project.Assets / "common.glsl";
		const std::filesystem::path futurePath = project.Assets / "future.png";
		const std::filesystem::path sharedOwnerPath = project.Assets / "shared-owner.png";
		const std::filesystem::path sharedWaiterPath = project.Assets / "shared-waiter.png";
		WriteBytes(heroPath, "source-one");
		WriteBytes(dependencyPath, MakeDependencyShader("shader-one"));
		WriteBytes(futurePath, "future-source");
		WriteBytes(sharedOwnerPath, "identical-shared-flight");
		WriteBytes(sharedWaiterPath, "identical-shared-flight");

		const std::filesystem::path heroMeta =
			TomCat::AssetRegistry::GetMetadataPath(heroPath);
		WriteBytes(heroMeta,
			"SchemaVersion: 1\n"
			"FutureRoot:\n  Keep: root-value\n"
			"Asset:\n"
			"  Handle: 1001\n"
			"  Type: Texture2D\n"
			"  ImportSettings:\n    quality: high\n"
			"  FutureAsset:\n    Keep: asset-value\n");
		const std::filesystem::path futureMeta =
			TomCat::AssetRegistry::GetMetadataPath(futurePath);
		const std::string futureDocument =
			"SchemaVersion: 99\nAsset:\n  Handle: 9901\n  Type: Texture2D\n"
			"  ImportSettings: {}\nFutureOnly: untouched\n";
		WriteBytes(futureMeta, futureDocument);

		TomCat::AssetRegistry registry;
		Require(registry.Initialize(project.Assets, project.Library),
			"asset registry initialization failed");
		const TomCat::AssetMetadata* migrated =
			registry.GetMetadata(TomCat::AssetHandle(1001));
		Require(migrated && migrated->ImportSettings.at("quality") == "high",
			"v1 tcmeta was not read during migration");
		const std::string migratedDocument = ReadText(heroMeta);
		Require(migratedDocument.find("SchemaVersion: 2") != std::string::npos &&
			migratedDocument.find("SubAssets:") != std::string::npos &&
			migratedDocument.find("FutureRoot") != std::string::npos &&
			migratedDocument.find("FutureAsset") != std::string::npos,
			"v1 migration did not preserve unknown fields or emit v2 SubAssets");
		Require(ReadText(futureMeta) == futureDocument &&
			registry.GetMetadata(futurePath) == nullptr,
			"future tcmeta schema was overwritten or registered ambiguously");

		const TomCat::AssetHandle hero(1001);
		const TomCat::AssetHandle dependency = RequireHandle(registry,
			dependencyPath, TomCat::AssetType::Shader);
		const TomCat::AssetHandle sharedOwner = RequireHandle(registry,
			sharedOwnerPath, TomCat::AssetType::Texture2D);
		const TomCat::AssetHandle sharedWaiter = RequireHandle(registry,
			sharedWaiterPath, TomCat::AssetType::Texture2D);
		TomCat::AssetDatabase database;
		Require(database.Initialize(registry, project.Library),
			"asset database initialization failed");
		const std::vector<TomCat::AssetType> registered =
			database.GetImporters().GetRegisteredTypes();
		for (TomCat::AssetType required : { TomCat::AssetType::Texture2D,
			TomCat::AssetType::Shader, TomCat::AssetType::Material,
			TomCat::AssetType::Font, TomCat::AssetType::Audio,
			TomCat::AssetType::Scene, TomCat::AssetType::Prefab,
			TomCat::AssetType::CSharpScript })
		{
			Require(std::find(registered.begin(), registered.end(), required) !=
				registered.end(), "a required built-in importer was not registered");
		}

		auto counting = std::make_shared<CountingTextureImporter>();
		Require(database.GetImporters().Register(counting, true),
			"custom importer replacement failed");

		// Artifact keys intentionally omit Handle, so these two assets share one
		// flight. Cancelling its owner must not leak Cancelled to the independent,
		// non-cancelled waiter; the waiter retries ownership without recursion.
		counting->DelayMilliseconds = 150;
		auto ownerCancellation = std::make_shared<TomCat::AssetLoadCancellation>();
		TomCat::AssetLoadOptions ownerOptions;
		ownerOptions.Cancellation = ownerCancellation;
		std::future<TomCat::AssetLoadResult> cancelledOwner =
			database.LoadArtifactAsync(sharedOwner, ownerOptions);
		const auto ownerStartedDeadline = std::chrono::steady_clock::now()
			+ std::chrono::seconds(2);
		while (counting->Invocations.load() == 0
			&& std::chrono::steady_clock::now() < ownerStartedDeadline)
			std::this_thread::yield();
		Require(counting->Invocations.load() == 1,
			"shared artifact-key owner did not enter its importer");
		std::future<TomCat::AssetLoadResult> independentWaiter =
			database.LoadArtifactAsync(sharedWaiter);
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
		ownerCancellation->Cancel();
		const TomCat::AssetLoadResult ownerResult = cancelledOwner.get();
		const TomCat::AssetLoadResult waiterResult = independentWaiter.get();
		Require(ownerResult.Status == TomCat::AssetLoadStatus::Cancelled,
			"shared artifact-key owner did not observe its cancellation");
		Require(waiterResult.Succeeded()
			&& waiterResult.Artifact.Handle == sharedWaiter
			&& counting->Invocations.load() == 2,
			"cancelled shared-flight owner incorrectly cancelled its independent waiter");
		counting->Invocations = 0;
		counting->ActiveInvocations = 0;
		counting->MaxConcurrentInvocations = 0;
		counting->DelayMilliseconds = 15;

		TomCat::AssetLoadResult first = database.LoadArtifact(hero);
		Require(first.Succeeded() && !first.Artifact.FromCache &&
			counting->Invocations == 1, "first import did not publish a derived artifact");
		const std::string firstKey = first.Artifact.ArtifactKey;
		const TomCat::AssetHandle stableChild = RequireHeroSubAsset(registry, hero);

		TomCat::AssetLoadResult repeated = database.LoadArtifact(hero);
		Require(repeated.Succeeded() && repeated.Artifact.FromCache &&
			repeated.Artifact.ArtifactKey == firstKey && counting->Invocations == 1 &&
			RequireHeroSubAsset(registry, hero) == stableChild,
			"identical input did not hit cache or changed the sub-asset handle");

		Require(registry.SetImportSettings(hero, { { "quality", "low" } }),
			"could not change import settings");
		TomCat::AssetLoadResult settingsChanged = database.LoadArtifact(hero);
		Require(settingsChanged.Succeeded() && settingsChanged.Artifact.ArtifactKey != firstKey &&
			counting->Invocations == 2 && RequireHeroSubAsset(registry, hero) == stableChild,
			"settings did not invalidate the artifact key or preserve sub-asset identity");

		WriteBytes(heroPath, "source-two");
		TomCat::AssetLoadResult sourceChanged = database.LoadArtifact(hero);
		Require(sourceChanged.Succeeded() &&
			sourceChanged.Artifact.ArtifactKey != settingsChanged.Artifact.ArtifactKey &&
			counting->Invocations == 3 && RequireHeroSubAsset(registry, hero) == stableChild,
			"source content did not invalidate the artifact key");

		Require(database.SetDependencies(hero, { dependency }),
			"could not establish dependency graph edge");
		Require(database.GetDependents(dependency) ==
			std::vector<TomCat::AssetHandle>{ hero },
			"reverse dependency graph is inconsistent");
		TomCat::AssetLoadResult dependencyAdded = database.LoadArtifact(hero);
		Require(dependencyAdded.Succeeded() &&
			dependencyAdded.Artifact.ArtifactKey != sourceChanged.Artifact.ArtifactKey &&
			counting->Invocations == 4,
			"adding a dependency did not invalidate the artifact key");
		WriteBytes(dependencyPath, MakeDependencyShader("shader-two"));
		TomCat::AssetLoadResult dependencyChanged = database.LoadArtifact(hero);
		Require(dependencyChanged.Succeeded() &&
			dependencyChanged.Artifact.ArtifactKey != dependencyAdded.Artifact.ArtifactKey &&
			counting->Invocations == 5,
			"dependency content did not propagate into the artifact key");

		Require(registry.SetImportSettings(hero,
			{ { "batch", "single-flight" }, { "quality", "low" } }),
			"could not prepare single-flight settings");
		const uint32_t beforeConcurrent = counting->Invocations.load();
		std::vector<std::future<TomCat::AssetLoadResult>> futures;
		for (size_t index = 0; index < 12; ++index)
			futures.push_back(database.LoadArtifactAsync(hero));
		std::string concurrentKey;
		for (auto& future : futures)
		{
			TomCat::AssetLoadResult loaded = future.get();
			Require(loaded.Succeeded(), "concurrent import failed");
			if (concurrentKey.empty())
				concurrentKey = loaded.Artifact.ArtifactKey;
			Require(loaded.Artifact.ArtifactKey == concurrentKey,
				"concurrent callers observed different artifact keys");
		}
		Require(counting->Invocations == beforeConcurrent + 1,
			"concurrent identical loads executed the importer more than once");
		Require(RequireHeroSubAsset(registry, hero) == stableChild,
			"concurrent imports changed the stable sub-asset handle");

		const std::filesystem::path cacheEntry =
			database.GetCache().GetEntryPath(concurrentKey);
		WriteBytes(cacheEntry, "damaged-cache-entry");
		const uint32_t beforeRepair = counting->Invocations.load();
		TomCat::AssetLoadResult repaired = database.LoadArtifact(hero);
		Require(repaired.Succeeded() && !repaired.Artifact.FromCache &&
			counting->Invocations == beforeRepair + 1 &&
			database.GetCache().GetStats().CorruptEntries >= 1,
			"damaged cache entry was not detected and rebuilt");
		TomCat::AssetLoadResult repairedHit = database.LoadArtifact(hero);
		Require(repairedHit.Succeeded() && repairedHit.Artifact.FromCache &&
			counting->Invocations == beforeRepair + 1,
			"rebuilt cache entry was not reusable");

		auto cancellation = std::make_shared<TomCat::AssetLoadCancellation>();
		cancellation->Cancel();
		TomCat::AssetLoadOptions cancelledOptions;
		cancelledOptions.Cancellation = cancellation;
		Require(database.LoadArtifactAsync(hero, cancelledOptions).get().Status ==
			TomCat::AssetLoadStatus::Cancelled,
			"pre-cancelled asynchronous load did not stop");

		const std::optional<size_t> decoded = database.Load<size_t>(hero,
			[](const TomCat::ImportedArtifact& artifact) -> std::optional<size_t>
			{
				return artifact.Bytes.size();
			});
		Require(decoded && *decoded == repairedHit.Artifact.Bytes.size(),
			"typed synchronous Load<T> did not decode the imported artifact");
	}


	void TestLoadArtifactRetriesConsistentGraphSnapshot()
	{
		TemporaryProject project;
		const std::filesystem::path rootPath = project.Assets / "snapshot-root.png";
		const std::filesystem::path oldDependencyPath =
			project.Assets / "snapshot-old.png";
		const std::filesystem::path newDependencyPath =
			project.Assets / "snapshot-new.png";
		WriteBytes(rootPath, "snapshot-root-source");
		WriteBytes(oldDependencyPath, "snapshot-old-dependency");
		WriteBytes(newDependencyPath, "snapshot-new-dependency");

		TomCat::AssetRegistry registry;
		Require(registry.Initialize(project.Assets, project.Library),
			"snapshot race registry initialization failed");
		const TomCat::AssetHandle root = RequireHandle(registry, rootPath,
			TomCat::AssetType::Texture2D);
		const TomCat::AssetHandle oldDependency = RequireHandle(registry,
			oldDependencyPath, TomCat::AssetType::Texture2D);
		const TomCat::AssetHandle newDependency = RequireHandle(registry,
			newDependencyPath, TomCat::AssetType::Texture2D);

		TomCat::AssetDatabase database;
		Require(database.Initialize(registry, project.Library),
			"snapshot race database initialization failed");
		auto importer = std::make_shared<BlockingSnapshotTextureImporter>();
		Require(database.GetImporters().Register(importer, true),
			"snapshot race importer registration failed");
		TomCat::AssetLoadOptions options;
		options.Platform = "snapshot-race";
		options.Backend = "test";
		options.DeferMetadataCommit = true;

		TomCat::AssetLoadResult newDependencyLoad =
			database.LoadArtifact(newDependency, options);
		Require(newDependencyLoad.Succeeded(),
			"new dependency warm load failed");
		const std::string newDependencyKey =
			newDependencyLoad.Artifact.ArtifactKey;
		Require(database.SetDependencies(root, { oldDependency }),
			"could not establish the old snapshot dependency");

		TomCat::AssetLoadResult oldRoot = database.LoadArtifact(root, options);
		Require(oldRoot.Succeeded()
			&& oldRoot.Artifact.DependencyKeys.size() == 1,
			"old dependency snapshot warm load failed");
		const std::string oldRootKey = oldRoot.Artifact.ArtifactKey;
		const std::string oldDependencyKey =
			oldRoot.Artifact.DependencyKeys.front();
		const std::filesystem::path oldRootEntry =
			database.GetCache().GetEntryPath(oldRootKey);
		const std::filesystem::path oldDependencyEntry =
			database.GetCache().GetEntryPath(oldDependencyKey);
		Require(std::filesystem::exists(oldRootEntry)
			&& std::filesystem::exists(oldDependencyEntry),
			"snapshot race warm cache entries are missing");
		std::error_code removeError;
		std::filesystem::remove(oldRootEntry, removeError);
		Require(!removeError, "could not remove the old root cache entry");
		removeError.clear();
		std::filesystem::remove(oldDependencyEntry, removeError);
		Require(!removeError, "could not remove the old dependency cache entry");

		importer->Arm(oldDependency);
		std::future<TomCat::AssetLoadResult> raced =
			std::async(std::launch::async, [&database, root, options]()
			{
				return database.LoadArtifact(root, options);
			});
		const bool blocked = importer->WaitUntilBlocked();
		if (!blocked)
			importer->Release();
		Require(blocked, "snapshot race importer did not reach its gate");
		const bool graphChanged = database.SetDependencies(root, { newDependency });
		importer->Release();
		Require(graphChanged, "could not replace the dependency during import");

		TomCat::AssetLoadResult loaded = raced.get();
		Require(loaded.Succeeded(),
			"graph mutation did not retry the complete load tree");
		Require(loaded.Artifact.DependencyKeys ==
			std::vector<std::string>{ newDependencyKey },
			"retried load returned dependency keys from the stale graph");
		Require(loaded.Artifact.ArtifactKey != oldRootKey,
			"retried load returned the old mixed root artifact key");
		Require(!std::filesystem::exists(oldRootEntry),
			"stale dependency keys were published under the old root key");
		Require(database.GetDependencySnapshot(root).Dependencies ==
			std::vector<TomCat::AssetHandle>{ newDependency },
			"final dependency snapshot does not contain the replacement");

		database.Shutdown();
		registry.Shutdown();
	}


	void TestLoadArtifactRetriesChangedSourceSnapshot()
	{
		TemporaryProject project;
		const std::filesystem::path texturePath =
			project.Assets / "snapshot-source.png";
		WriteBytes(texturePath, "source-before-gate");

		TomCat::AssetRegistry registry;
		Require(registry.Initialize(project.Assets, project.Library),
			"source snapshot registry initialization failed");
		const TomCat::AssetHandle texture = RequireHandle(registry, texturePath,
			TomCat::AssetType::Texture2D);
		TomCat::AssetDatabase database;
		Require(database.Initialize(registry, project.Library),
			"source snapshot database initialization failed");
		auto importer = std::make_shared<BlockingSnapshotTextureImporter>();
		Require(database.GetImporters().Register(importer, true),
			"source snapshot importer registration failed");

		TomCat::AssetLoadOptions options;
		options.Platform = "source-snapshot";
		options.Backend = "test";
		options.DeferMetadataCommit = true;
		TomCat::AssetLoadResult before = database.LoadArtifact(texture, options);
		Require(before.Succeeded(), "source snapshot warm load failed");
		const std::string oldArtifactKey = before.Artifact.ArtifactKey;
		const std::string oldSourceHash = before.Artifact.SourceSHA256;
		const std::filesystem::path oldEntry =
			database.GetCache().GetEntryPath(oldArtifactKey);
		std::error_code removeError;
		std::filesystem::remove(oldEntry, removeError);
		Require(!removeError && !std::filesystem::exists(oldEntry),
			"could not remove the old source snapshot cache entry");

		importer->Arm(texture);
		std::future<TomCat::AssetLoadResult> raced =
			std::async(std::launch::async, [&database, texture, options]()
			{
				return database.LoadArtifact(texture, options);
			});
		const bool blocked = importer->WaitUntilBlocked();
		if (!blocked)
			importer->Release();
		Require(blocked, "source snapshot importer did not reach its gate");
		WriteBytes(texturePath, "source-after-gate");
		importer->Release();

		TomCat::AssetLoadResult loaded = raced.get();
		const std::string expectedPrefix = "source-after-gate";
		Require(loaded.Succeeded()
			&& loaded.Artifact.ArtifactKey != oldArtifactKey
			&& loaded.Artifact.SourceSHA256 != oldSourceHash,
			"source mutation did not retry from a fresh content snapshot");
		Require(loaded.Artifact.Bytes.size() >= expectedPrefix.size()
			&& std::equal(expectedPrefix.begin(), expectedPrefix.end(),
				loaded.Artifact.Bytes.begin()),
			"retried import returned bytes from the source before the gate");
		Require(!std::filesystem::exists(oldEntry),
			"stale source bytes were published under the old artifact key");

		database.Shutdown();
		registry.Shutdown();
	}




	void TestDiscoverableSourceCannotUseStaleDependencyKeys()
	{
		TemporaryProject project;
		const std::filesystem::path shaderPath =
			project.Assets / "snapshot-material.glsl";
		const std::filesystem::path oldTexturePath =
			project.Assets / "snapshot-material-old.png";
		const std::filesystem::path newTexturePath =
			project.Assets / "snapshot-material-new.png";
		const std::filesystem::path materialPath =
			project.Assets / "snapshot-material.tcmat";
		WriteBytes(shaderPath, MakeDependencyShader("snapshot-material"));
		WriteBytes(oldTexturePath, "snapshot-material-old-texture");
		WriteBytes(newTexturePath, "snapshot-material-new-texture");

		TomCat::AssetRegistry registry;
		Require(registry.Initialize(project.Assets, project.Library),
			"material snapshot registry initialization failed");
		const TomCat::AssetHandle shader = RequireHandle(registry, shaderPath,
			TomCat::AssetType::Shader);
		const TomCat::AssetHandle oldTexture = RequireHandle(registry,
			oldTexturePath, TomCat::AssetType::Texture2D);
		const TomCat::AssetHandle newTexture = RequireHandle(registry,
			newTexturePath, TomCat::AssetType::Texture2D);
		auto materialSource = [shader](TomCat::AssetHandle texture)
		{
			return std::string("SchemaVersion: 1\nShader: ")
				+ std::to_string(static_cast<uint64_t>(shader))
				+ "\nTextures:\n  Albedo: "
				+ std::to_string(static_cast<uint64_t>(texture))
				+ "\nParameters: {}\n";
		};
		WriteBytes(materialPath, materialSource(oldTexture));
		const TomCat::AssetHandle material = RequireHandle(registry, materialPath,
			TomCat::AssetType::Material);

		TomCat::AssetDatabase database;
		Require(database.Initialize(registry, project.Library),
			"material snapshot database initialization failed");
		auto importer = std::make_shared<BlockingSnapshotTextureImporter>();
		Require(database.GetImporters().Register(importer, true),
			"material snapshot texture importer registration failed");
		TomCat::AssetLoadOptions options;
		options.Platform = "editor";
		options.Backend = "opengl";
		options.DeferMetadataCommit = true;

		TomCat::AssetLoadResult oldTextureLoad =
			database.LoadArtifact(oldTexture, options);
		TomCat::AssetLoadResult newTextureLoad =
			database.LoadArtifact(newTexture, options);
		TomCat::AssetLoadResult oldMaterial =
			database.LoadArtifact(material, options);
		Require(oldTextureLoad.Succeeded() && newTextureLoad.Succeeded()
			&& oldMaterial.Succeeded(),
			"material snapshot warm loads failed");
		const std::string oldMaterialKey = oldMaterial.Artifact.ArtifactKey;
		const std::string oldTextureKey = oldTextureLoad.Artifact.ArtifactKey;
		const std::string newTextureKey = newTextureLoad.Artifact.ArtifactKey;
		const std::filesystem::path oldMaterialEntry =
			database.GetCache().GetEntryPath(oldMaterialKey);
		const std::filesystem::path oldTextureEntry =
			database.GetCache().GetEntryPath(oldTextureKey);
		std::error_code removeError;
		std::filesystem::remove(oldMaterialEntry, removeError);
		Require(!removeError, "could not remove the old material cache entry");
		removeError.clear();
		std::filesystem::remove(oldTextureEntry, removeError);
		Require(!removeError, "could not remove the old material texture cache entry");

		importer->Arm(oldTexture);
		std::future<TomCat::AssetLoadResult> raced =
			std::async(std::launch::async, [&database, material, options]()
			{
				return database.LoadArtifact(material, options);
			});
		const bool blocked = importer->WaitUntilBlocked();
		if (!blocked)
			importer->Release();
		Require(blocked,
			"material dependency importer did not reach its mutation gate");
		const std::string changedSource = materialSource(newTexture);
		const std::vector<uint8_t> changedSourceBytes(changedSource.begin(),
			changedSource.end());
		const std::shared_ptr<const TomCat::IAssetImporter> materialImporter =
			database.GetImporters().Find(TomCat::AssetType::Material);
		Require(materialImporter != nullptr,
			"material snapshot importer was unavailable");
		TomCat::ArtifactKeyInput mixedKeyInput;
		mixedKeyInput.ImporterID = std::string(materialImporter->GetID());
		mixedKeyInput.ImporterVersion = materialImporter->GetVersion();
		mixedKeyInput.Type = TomCat::AssetType::Material;
		mixedKeyInput.SourceSHA256 =
			TomCat::ComputeContentSHA256(changedSourceBytes);
		mixedKeyInput.Settings =
			oldMaterial.Artifact.LoadSnapshots.front().Metadata.ImportSettings;
		mixedKeyInput.Platform = options.Platform;
		mixedKeyInput.Backend = options.Backend;
		mixedKeyInput.DependencyKeys = oldMaterial.Artifact.DependencyKeys;
		const std::filesystem::path mixedEntry = database.GetCache().GetEntryPath(
			TomCat::BuildArtifactKey(std::move(mixedKeyInput)));
		WriteBytes(materialPath, changedSource);
		importer->Release();

		TomCat::AssetLoadResult racedResult = raced.get();
		Require(racedResult.Status == TomCat::AssetLoadStatus::StaleSnapshot,
			"deferred load accepted a source newer than its dependency graph");
		Require(!std::filesystem::exists(oldMaterialEntry)
			&& !std::filesystem::exists(mixedEntry),
			"stale material inputs were published to the DDC");
		Require(database.RefreshRegistry(),
			"material dependency graph did not refresh after the raced source edit");

		TomCat::AssetLoadResult loaded = database.LoadArtifact(material, options);
		Require(loaded.Succeeded() && loaded.Artifact.ArtifactKey != oldMaterialKey,
			"fresh material load did not use the refreshed dependency graph");
		Require(std::find(loaded.Artifact.DependencyKeys.begin(),
			loaded.Artifact.DependencyKeys.end(), newTextureKey)
			!= loaded.Artifact.DependencyKeys.end()
			&& std::find(loaded.Artifact.DependencyKeys.begin(),
				loaded.Artifact.DependencyKeys.end(), oldTextureKey)
				== loaded.Artifact.DependencyKeys.end(),
			"material artifact mixed new source bytes with old dependency keys");
		TomCat::MaterialArtifactView materialView;
		std::string parseError;
		Require(TomCat::ParseMaterialArtifact(loaded.Artifact.Bytes,
			materialView, parseError)
			&& materialView.Textures.size() == 1
			&& materialView.Textures.front().Texture == newTexture,
			"fresh material artifact did not contain the new texture reference");
		const TomCat::AssetDependencySnapshot graph =
			database.GetDependencySnapshot(material);
		Require(graph.Dependencies.size() == 2
			&& std::find(graph.Dependencies.begin(), graph.Dependencies.end(),
				newTexture) != graph.Dependencies.end()
			&& std::find(graph.Dependencies.begin(), graph.Dependencies.end(),
				oldTexture) == graph.Dependencies.end(),
			"material dependency graph was not refreshed before fresh load");

		const std::filesystem::path verifiedEntry =
			database.GetCache().GetEntryPath(loaded.Artifact.ArtifactKey);
		removeError.clear();
		std::filesystem::remove(verifiedEntry, removeError);
		Require(!removeError,
			"could not remove the verified material cache entry");
		WriteBytes(materialPath, "{ malformed-material");
		Require(!database.RefreshRegistry(),
			"malformed material unexpectedly produced a dependency graph");
		const TomCat::AssetDependencySnapshot invalidGraph =
			database.GetDependencySnapshot(material);
		Require(!invalidGraph.SourceSHA256.empty()
			&& invalidGraph.SourceSHA256 == graph.SourceSHA256,
			"failed dependency discovery discarded its last verified provenance");
		const TomCat::AssetLoadResult invalid =
			database.LoadArtifact(material, options);
		Require(invalid.Status == TomCat::AssetLoadStatus::StaleSnapshot
			&& !std::filesystem::exists(verifiedEntry),
			"malformed discoverable source reused old dependency keys or published DDC");

		WriteBytes(materialPath, changedSource);
		Require(database.RefreshRegistry(),
			"material dependency graph did not recover after fixing its source");
		const TomCat::AssetLoadResult recovered =
			database.LoadArtifact(material, options);
		Require(recovered.Succeeded()
			&& std::find(recovered.Artifact.DependencyKeys.begin(),
				recovered.Artifact.DependencyKeys.end(), newTextureKey)
				!= recovered.Artifact.DependencyKeys.end(),
			"material did not load from verified dependencies after recovery");

		database.Shutdown();
		registry.Shutdown();
	}


	void TestCoordinatorRetriesStaleDeferredFinalization()
	{
		TomCat::AssetJobSystem& jobSystem = TomCat::AssetJobSystem::Get();
		const TomCat::AssetJobSystem::Limits previousJobLimits =
			jobSystem.GetLimits();
		TomCat::AssetJobSystem::Limits deterministicJobLimits =
			previousJobLimits;
		deterministicJobLimits.WorkerCount = 1;
		deterministicJobLimits.MaximumQueuedJobs =
			std::max<size_t>(deterministicJobLimits.MaximumQueuedJobs, 8);
		jobSystem.Configure(deterministicJobLimits);

		TemporaryProject project;
		const std::filesystem::path texturePath =
			project.Assets / "deferred-stale.png";
		WriteBytes(texturePath, "deferred-stale-source");

		TomCat::AssetRegistry registry;
		Require(registry.Initialize(project.Assets, project.Library),
			"deferred stale registry initialization failed");
		const TomCat::AssetHandle texture = RequireHandle(registry, texturePath,
			TomCat::AssetType::Texture2D);
		TomCat::AssetDatabase database;
		Require(database.Initialize(registry, project.Library),
			"deferred stale database initialization failed");
		auto importer = std::make_shared<BlockingSnapshotTextureImporter>();
		Require(database.GetImporters().Register(importer, true),
			"deferred stale importer registration failed");

		TomCat::AssetImportCoordinatorOptions coordinatorOptions;
		coordinatorOptions.Platform = "deferred-stale";
		coordinatorOptions.Backend = "test";
		TomCat::AssetImportCoordinator coordinator;
		Require(coordinator.Initialize(registry, database, project.Assets,
			coordinatorOptions),
			"deferred stale coordinator initialization failed");
		std::vector<TomCat::AssetImportEvent> events;
		auto receive = [&events](const TomCat::AssetImportEvent& event)
		{
			events.push_back(event);
		};
		auto pumpUntilInvocation = [&](uint32_t expected, const char* failure)
		{
			const auto deadline = std::chrono::steady_clock::now()
				+ std::chrono::seconds(5);
			while (importer->Invocations.load() < expected
				&& std::chrono::steady_clock::now() < deadline)
			{
				Require(coordinator.PumpMainThread(receive) == 0,
					"stale retry published an event before finalization");
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
			Require(importer->Invocations.load() >= expected, failure);
			Require(importer->WaitUntilBlocked(), failure);
		};
		auto waitUntilWorkerFutureReady = [&](const char* failure)
		{
			// With one FIFO worker, this sentinel cannot complete until the
			// coordinator's already-running LoadArtifactAsync packaged task has
			// returned and made its future ready. Do not Pump while waiting.
			std::future<void> sentinel = jobSystem.Submit(0, []() {});
			Require(sentinel.wait_for(std::chrono::seconds(5))
				== std::future_status::ready, failure);
			sentinel.get();
		};

		importer->Arm(texture);
		Require(coordinator.RequestReimport(texture),
			"could not queue the initial deferred import");
		pumpUntilInvocation(1, "initial deferred worker did not start");
		importer->Release();
		waitUntilWorkerFutureReady(
			"initial deferred worker future did not become ready");

		Require(registry.SetImportSettings(texture, { { "variant", "stale-one" } }),
			"could not create the first deferred publication race");
		importer->Arm(texture);
		pumpUntilInvocation(2,
			"first stale finalization was not rescheduled in place");
		Require(events.empty() && coordinator.GetPendingImportCount() == 1,
			"first stale finalization was published or dropped");
		importer->Release();
		waitUntilWorkerFutureReady(
			"first stale retry worker future did not become ready");

		Require(registry.SetImportSettings(texture, { { "variant", "stale-two" } }),
			"could not create the second deferred publication race");
		importer->Arm(texture);
		pumpUntilInvocation(3,
			"second stale finalization was not rescheduled in place");
		Require(events.empty() && coordinator.GetPendingImportCount() == 1,
			"second stale finalization was published or dropped");
		importer->Release();
		waitUntilWorkerFutureReady(
			"second stale retry worker future did not become ready");

		const auto publishDeadline = std::chrono::steady_clock::now()
			+ std::chrono::seconds(5);
		while (events.empty() && std::chrono::steady_clock::now() < publishDeadline)
		{
			(void)coordinator.PumpMainThread(receive);
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		const TomCat::AssetMetadata* current = registry.GetMetadata(texture);
		Require(events.size() == 1 && events.front().Result.Succeeded()
			&& importer->Invocations.load() == 3
			&& coordinator.GetPendingImportCount() == 0,
			"fresh deferred retry was not published exactly once");
		Require(current && current->ImportSettings.at("variant") == "stale-two"
			&& current->SubAssets.size() == 1
			&& current->SubAssets.front().Name == "stale-two",
			"stale deferred sub-assets overwrote the final fresh import");
		Require(events.front().Result.Artifact.LoadSnapshots.front()
				.Metadata.SubAssets.size() == 1
			&& events.front().Result.Artifact.LoadSnapshots.front()
				.Metadata.SubAssets.front().Handle
				== current->SubAssets.front().Handle,
			"deferred publication identity was not refreshed after commit");

		coordinator.Shutdown();
		database.Shutdown();
		registry.Shutdown();
		jobSystem.Configure(previousJobLimits);
	}

	void TestCoordinatorRefreshesDiscoverableClosureBeforeStaleRetry()
	{
		TomCat::AssetJobSystem& jobSystem = TomCat::AssetJobSystem::Get();
		const TomCat::AssetJobSystem::Limits previousJobLimits =
			jobSystem.GetLimits();
		TomCat::AssetJobSystem::Limits deterministicJobLimits =
			previousJobLimits;
		deterministicJobLimits.WorkerCount = 1;
		deterministicJobLimits.MaximumQueuedJobs =
			std::max<size_t>(deterministicJobLimits.MaximumQueuedJobs, 8);
		jobSystem.Configure(deterministicJobLimits);

		TemporaryProject project;
		const std::filesystem::path shaderPath =
			project.Assets / "coordinator-material.glsl";
		const std::filesystem::path oldTexturePath =
			project.Assets / "coordinator-material-old.png";
		const std::filesystem::path newTexturePath =
			project.Assets / "coordinator-material-new.png";
		const std::filesystem::path materialPath =
			project.Assets / "coordinator-material.tcmat";
		WriteBytes(shaderPath, MakeDependencyShader("coordinator-material"));
		WriteBytes(oldTexturePath, "coordinator-material-old-texture");
		WriteBytes(newTexturePath, "coordinator-material-new-texture");

		TomCat::AssetRegistry registry;
		Require(registry.Initialize(project.Assets, project.Library),
			"coordinator material registry initialization failed");
		const TomCat::AssetHandle shader = RequireHandle(registry, shaderPath,
			TomCat::AssetType::Shader);
		const TomCat::AssetHandle oldTexture = RequireHandle(registry,
			oldTexturePath, TomCat::AssetType::Texture2D);
		const TomCat::AssetHandle newTexture = RequireHandle(registry,
			newTexturePath, TomCat::AssetType::Texture2D);
		auto materialSource = [shader](TomCat::AssetHandle texture)
		{
			return std::string("SchemaVersion: 1\nShader: ")
				+ std::to_string(static_cast<uint64_t>(shader))
				+ "\nTextures:\n  Albedo: "
				+ std::to_string(static_cast<uint64_t>(texture))
				+ "\nParameters: {}\n";
		};
		WriteBytes(materialPath, materialSource(oldTexture));
		const TomCat::AssetHandle material = RequireHandle(registry, materialPath,
			TomCat::AssetType::Material);

		TomCat::AssetDatabase database;
		Require(database.Initialize(registry, project.Library),
			"coordinator material database initialization failed");
		auto importer = std::make_shared<BlockingSnapshotTextureImporter>();
		Require(database.GetImporters().Register(importer, true),
			"coordinator material texture importer registration failed");

		TomCat::AssetLoadOptions options;
		options.Platform = "editor";
		options.Backend = "opengl";
		options.DeferMetadataCommit = true;
		const TomCat::AssetLoadResult oldTextureLoad =
			database.LoadArtifact(oldTexture, options);
		const TomCat::AssetLoadResult newTextureLoad =
			database.LoadArtifact(newTexture, options);
		const TomCat::AssetLoadResult oldMaterialLoad =
			database.LoadArtifact(material, options);
		Require(oldTextureLoad.Succeeded() && newTextureLoad.Succeeded()
			&& oldMaterialLoad.Succeeded(),
			"coordinator material warm loads failed");
		std::error_code removeError;
		std::filesystem::remove(database.GetCache().GetEntryPath(
			oldTextureLoad.Artifact.ArtifactKey), removeError);
		Require(!removeError,
			"could not remove coordinator material's old texture cache entry");

		TomCat::AssetImportCoordinatorOptions coordinatorOptions;
		coordinatorOptions.Platform = options.Platform;
		coordinatorOptions.Backend = options.Backend;
		TomCat::AssetImportCoordinator coordinator;
		Require(coordinator.Initialize(registry, database, project.Assets,
			coordinatorOptions),
			"coordinator material import coordinator initialization failed");
		std::vector<TomCat::AssetImportEvent> events;
		auto receive = [&events](const TomCat::AssetImportEvent& event)
		{
			events.push_back(event);
		};

		const uint32_t expectedInvocation = importer->Invocations.load() + 1;
		importer->Arm(oldTexture);
		Require(coordinator.RequestReimport(material),
			"could not queue the raced coordinator material import");
		const auto startDeadline = std::chrono::steady_clock::now()
			+ std::chrono::seconds(5);
		while (importer->Invocations.load() < expectedInvocation
			&& std::chrono::steady_clock::now() < startDeadline)
		{
			Require(coordinator.PumpMainThread(receive) == 0,
				"raced coordinator material published before its dependency gate");
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		const bool blocked = importer->Invocations.load() >= expectedInvocation
			&& importer->WaitUntilBlocked();
		if (!blocked)
			importer->Release();
		Require(blocked,
			"coordinator material dependency did not reach its mutation gate");

		WriteBytes(materialPath, materialSource(newTexture));
		importer->Release();
		std::future<void> staleSentinel = jobSystem.Submit(0, []() {});
		Require(staleSentinel.wait_for(std::chrono::seconds(5))
			== std::future_status::ready,
			"raced coordinator material future did not become ready");
		staleSentinel.get();

		Require(coordinator.PumpMainThread(receive) == 0
			&& events.empty() && coordinator.GetPendingImportCount() == 1,
			"stale coordinator material was published or dropped instead of retried");
		std::future<void> freshSentinel = jobSystem.Submit(0, []() {});
		Require(freshSentinel.wait_for(std::chrono::seconds(5))
			== std::future_status::ready,
			"refreshed coordinator material future did not become ready");
		freshSentinel.get();

		Require(coordinator.PumpMainThread(receive) == 1
			&& events.size() == 1 && events.front().Result.Succeeded()
			&& coordinator.GetPendingImportCount() == 0,
			"coordinator did not publish the refreshed material exactly once");
		const std::vector<std::string>& dependencyKeys =
			events.front().Result.Artifact.DependencyKeys;
		Require(std::find(dependencyKeys.begin(), dependencyKeys.end(),
				newTextureLoad.Artifact.ArtifactKey) != dependencyKeys.end()
			&& std::find(dependencyKeys.begin(), dependencyKeys.end(),
				oldTextureLoad.Artifact.ArtifactKey) == dependencyKeys.end(),
			"coordinator stale retry did not rebuild the material dependency closure");
		const TomCat::AssetDependencySnapshot graph =
			database.GetDependencySnapshot(material);
		Require(std::find(graph.Dependencies.begin(), graph.Dependencies.end(),
				newTexture) != graph.Dependencies.end()
			&& std::find(graph.Dependencies.begin(), graph.Dependencies.end(),
				oldTexture) == graph.Dependencies.end(),
			"coordinator owner-thread refresh did not publish the new material graph");

		coordinator.Shutdown();
		database.Shutdown();
		registry.Shutdown();
		jobSystem.Configure(previousJobLimits);
	}


	void TestFileMonitorImportCoordinator()
	{
		TemporaryProject project;
		const std::filesystem::path heroPath = project.Assets / "hero.png";
		const std::filesystem::path secondaryPath =
			project.Assets / "secondary.png";
		const std::filesystem::path dependencyPath = project.Assets / "common.glsl";
		WriteBytes(heroPath, "watch-baseline");
		WriteBytes(secondaryPath, "secondary-baseline");
		WriteBytes(dependencyPath, MakeDependencyShader("shader-baseline"));

		TomCat::AssetRegistry registry;
		Require(registry.Initialize(project.Assets, project.Library),
			"monitor registry initialization failed");
		const TomCat::AssetHandle hero = RequireHandle(registry, heroPath,
			TomCat::AssetType::Texture2D);
		const TomCat::AssetHandle dependency = RequireHandle(registry, dependencyPath,
			TomCat::AssetType::Shader);
		const TomCat::AssetHandle secondary = RequireHandle(registry, secondaryPath,
			TomCat::AssetType::Texture2D);
		Require(registry.SetImportSettings(hero, { { "quality", "watch-before" } }),
			"monitor test could not seed import settings");

		TomCat::AssetDatabase database;
		Require(database.Initialize(registry, project.Library),
			"monitor database initialization failed");
		auto counting = std::make_shared<CountingTextureImporter>();
		Require(database.GetImporters().Register(counting, true),
			"monitor counting importer registration failed");
		Require(database.SetDependencies(hero, { dependency }),
			"monitor dependency graph setup failed");
		Require(database.LoadArtifact(hero).Succeeded() && counting->Invocations == 1,
			"monitor baseline import failed");
		const TomCat::AssetHandle heroSprite = RequireHeroSubAsset(registry, hero);

		// Build a real Prefab -> sliced Sprite edge and a real Scene -> Prefab edge.
		// The logical graph keeps the child handle while the artifact graph retains
		// the atlas owner used by file-monitor invalidation and importer keys.
		auto prefabSource = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity prefabRoot = prefabSource->CreateEntity("Watched prefab");
		prefabRoot.AddComponent<TomCat::SpriteRenderer>().SpriteHandle = heroSprite;
		TomCat::PrefabArchive prefabArchive;
		std::string archiveError;
		Require(TomCat::PrefabArchiveCodec::CaptureSubtree(prefabSource, prefabRoot,
			prefabArchive, archiveError), "could not capture dependency Prefab");
		std::string prefabDocument;
		Require(TomCat::PrefabArchiveCodec::Encode(prefabArchive, prefabDocument,
			archiveError), "could not encode dependency Prefab");
		const std::filesystem::path prefabPath = project.Assets / "watched.tcprefab";
		WriteBytes(prefabPath, prefabDocument);
		const TomCat::AssetHandle prefab = RequireHandle(registry, prefabPath,
			TomCat::AssetType::Prefab);

		auto sceneSource = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity sceneEntity = sceneSource->CreateEntity("Watched scene");
		TomCat::CSharpScriptEntry attachment;
		attachment.LastKnownClassName = "DependencyProbe";
		attachment.Fields.emplace_back("11111111111111111111111111111111",
			"Template", TomCat::ScriptFieldType::AssetRef,
			static_cast<uint64_t>(prefab), "TomCat.PrefabAsset");
		sceneEntity.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(
			std::move(attachment));
		std::string sceneDocument;
		Require(TomCat::SceneSerializer(sceneSource).SerializeDocument(sceneDocument,
			archiveError), "could not encode dependency Scene");
		const std::filesystem::path scenePath = project.Assets / "watched.tomcat";
		WriteBytes(scenePath, sceneDocument);
		const TomCat::AssetHandle scene = RequireHandle(registry, scenePath,
			TomCat::AssetType::Scene);
		Require(database.RefreshRegistry(),
			"automatic dependency discovery failed to refresh the registry");
		const TomCat::AssetDependencySnapshot prefabSnapshot =
			database.GetDependencySnapshot(prefab);
		Require(prefabSnapshot.Dependencies ==
				std::vector<TomCat::AssetHandle>{ heroSprite }
			&& prefabSnapshot.ArtifactDependencies ==
				std::vector<TomCat::AssetHandle>{ hero },
			"Prefab dependency graph did not separate the logical Sprite from its atlas artifact owner");
		Require(database.GetDependencies(scene) ==
			std::vector<TomCat::AssetHandle>{ prefab },
			"Scene Prefab dependency was not discovered by AssetReferenceVisitor");
		const std::vector<TomCat::AssetHandle> transitive =
			database.GetDependents(hero, true);
		Require(std::find(transitive.begin(), transitive.end(), prefab) != transitive.end()
			&& std::find(transitive.begin(), transitive.end(), scene) != transitive.end(),
			"transitive reverse dependencies were not built automatically");
		const std::filesystem::path dependencyCache =
			project.Library / "AssetDependencies.yaml";
		Require(std::filesystem::is_regular_file(dependencyCache)
			&& ReadText(dependencyCache).find(std::to_string(
				static_cast<uint64_t>(scene))) != std::string::npos,
			"dependency graph was not persisted as a rebuildable Library cache");

		// Recreate both registry and database. The graph must be immediately usable
		// without the test manually calling SetDependencies for archive references.
		database.Shutdown();
		registry.Shutdown();
		Require(registry.Initialize(project.Assets, project.Library),
			"monitor registry restart failed");
		Require(database.Initialize(registry, project.Library),
			"monitor database restart failed");
		Require(database.GetImporters().Register(counting, true),
			"monitor counting importer restart registration failed");
		const TomCat::AssetDependencySnapshot restartedPrefabSnapshot =
			database.GetDependencySnapshot(prefab);
		Require(RequireHeroSubAsset(registry, hero) == heroSprite
			&& restartedPrefabSnapshot.Dependencies ==
				std::vector<TomCat::AssetHandle>{ heroSprite }
			&& restartedPrefabSnapshot.ArtifactDependencies ==
				std::vector<TomCat::AssetHandle>{ hero }
			&& database.GetDependencies(scene) ==
				std::vector<TomCat::AssetHandle>{ prefab },
			"restart did not restore and rebuild the automatic dependency graph");

		// The Library cache is disposable. A clean database must recover the same
		// graph from source archives and stable .tcmeta child identities alone.
		database.Shutdown();
		registry.Shutdown();
		std::error_code libraryError;
		std::filesystem::remove_all(project.Library, libraryError);
		Require(!libraryError && registry.Initialize(project.Assets, project.Library),
			"registry did not rebuild after deleting Library");
		Require(database.Initialize(registry, project.Library),
			"database did not rebuild after deleting Library");
		Require(database.GetImporters().Register(counting, true),
			"counting importer registration after Library rebuild failed");
		const TomCat::AssetDependencySnapshot rebuiltPrefabSnapshot =
			database.GetDependencySnapshot(prefab);
		Require(rebuiltPrefabSnapshot.Dependencies ==
				std::vector<TomCat::AssetHandle>{ heroSprite }
			&& rebuiltPrefabSnapshot.ArtifactDependencies ==
				std::vector<TomCat::AssetHandle>{ hero }
			&& database.GetDependencies(scene) ==
				std::vector<TomCat::AssetHandle>{ prefab },
			"source archives did not rebuild dependencies after deleting Library");
		Require(database.SetDependencies(hero, { dependency }),
			"monitor dependency graph could not restore its explicit importer edge");

		TomCat::AssetImportCoordinatorOptions options;
		options.PollInterval = std::chrono::milliseconds(12);
		options.Debounce = std::chrono::milliseconds(55);
		TomCat::AssetImportCoordinator coordinator;
		Require(coordinator.Initialize(registry, database, project.Assets, options) &&
			coordinator.Start(), "asset import coordinator did not start");

		const std::thread::id mainThread = std::this_thread::get_id();
		std::vector<TomCat::AssetImportEvent> events;
		auto receive = [&](const TomCat::AssetImportEvent& event)
		{
			Require(std::this_thread::get_id() == mainThread,
				"import completion callback escaped the main thread");
			events.push_back(event);
		};
		auto waitFor = [&](const std::function<bool()>& predicate,
			const char* failure)
		{
			const auto deadline = std::chrono::steady_clock::now() +
				std::chrono::seconds(4);
			while (std::chrono::steady_clock::now() < deadline)
			{
				(void)coordinator.PumpMainThread(receive);
				if (predicate())
					return;
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			throw std::runtime_error(failure);
		};
		auto matchingEvent = [&](size_t first, TomCat::AssetHandle handle,
			bool dependencyEvent, TomCat::AssetFileChangeKind kind,
			bool requireSuccess)
		{
			for (size_t index = first; index < events.size(); ++index)
			{
				const TomCat::AssetImportEvent& event = events[index];
				if (event.Handle == handle && event.IsDependency == dependencyEvent &&
					event.Change == kind &&
					(!requireSuccess || event.Result.Succeeded()))
					return true;
			}
			return false;
		};
		auto waitForIdle = [&]()
		{
			waitFor([&]() { return coordinator.GetPendingImportCount() == 0; },
				"asset import coordinator did not become idle");
		};

		size_t first = events.size();
		uint32_t invocations = counting->Invocations.load();
		WriteBytes(heroPath, "watch-content-one");
		coordinator.RequestScan();
		waitFor([&]()
		{
			return matchingEvent(first, hero, false,
					TomCat::AssetFileChangeKind::Modified, true)
				&& matchingEvent(first, prefab, true,
					TomCat::AssetFileChangeKind::Modified, true)
				&& matchingEvent(first, scene, true,
					TomCat::AssetFileChangeKind::Modified, true);
		}, "content change was not imported by the monitor");
		Require(counting->Invocations == invocations + 1,
			"atlas plus automatic archive dependents imported the atlas more than once");
		waitForIdle();

		// A second file can change while the first file's import is still running.
		// The newer batch must neither block the main-thread pump nor discard the
		// unrelated first completion.
		first = events.size();
		invocations = counting->Invocations.load();
		counting->DelayMilliseconds = 250;
		WriteBytes(heroPath, "overlap-hero");
		coordinator.RequestScan();
		const auto startedDeadline = std::chrono::steady_clock::now()
			+ std::chrono::seconds(4);
		while (counting->Invocations.load() < invocations + 1
			&& std::chrono::steady_clock::now() < startedDeadline)
		{
			(void)coordinator.PumpMainThread(receive);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		Require(counting->Invocations.load() >= invocations + 1,
			"first overlapping import did not start");
		WriteBytes(secondaryPath, "overlap-secondary");
		coordinator.RequestScan();
		waitFor([&]()
		{
			return matchingEvent(first, hero, false,
					TomCat::AssetFileChangeKind::Modified, true)
				&& matchingEvent(first, secondary, false,
					TomCat::AssetFileChangeKind::Modified, true);
		}, "overlapping changes did not publish both independent imports");
		Require(counting->Invocations == invocations + 2,
			"overlapping changes did not execute exactly two imports");
		waitForIdle();

		// Repeated saves of the same file while its importer is blocked retain
		// only the newest replacement. The first worker is cancelled and allowed
		// to unwind before the final worker starts, bounding per-asset concurrency.
		first = events.size();
		invocations = counting->Invocations.load();
		counting->DelayMilliseconds = 350;
		counting->MaxConcurrentInvocations = 0;
		WriteBytes(heroPath, "running-first");
		coordinator.RequestScan();
		const auto runningStartedDeadline = std::chrono::steady_clock::now()
			+ std::chrono::seconds(4);
		while (counting->Invocations.load() < invocations + 1
			&& std::chrono::steady_clock::now() < runningStartedDeadline)
		{
			(void)coordinator.PumpMainThread(receive);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		Require(counting->Invocations.load() == invocations + 1,
			"same-handle import did not start exactly one worker");
		auto pumpFor = [&](std::chrono::milliseconds duration)
		{
			const auto deadline = std::chrono::steady_clock::now() + duration;
			while (std::chrono::steady_clock::now() < deadline)
			{
				(void)coordinator.PumpMainThread(receive);
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		};
		WriteBytes(heroPath, "running-second");
		coordinator.RequestScan();
		pumpFor(std::chrono::milliseconds(80));
		WriteBytes(heroPath, "running-final");
		coordinator.RequestScan();
		pumpFor(std::chrono::milliseconds(80));
		Require(counting->MaxConcurrentInvocations.load() == 1,
			"dependency recursion bypassed the per-handle import concurrency bound");
		waitFor([&]()
		{
			return matchingEvent(first, hero, false,
				TomCat::AssetFileChangeKind::Modified, true);
		}, "latest same-handle replacement import was not published");
		Require(counting->Invocations.load() == invocations + 2,
			"same-handle saves were not reduced to first plus latest import");
		Require(counting->MaxConcurrentInvocations.load() == 1,
			"first and replacement imports overlapped for one asset handle");
		const auto replacementEvent = std::find_if(events.rbegin(), events.rend(),
			[hero](const TomCat::AssetImportEvent& event)
			{
				return event.Handle == hero && event.Result.Succeeded();
			});
		Require(replacementEvent != events.rend()
			&& std::string(replacementEvent->Result.Artifact.Bytes.begin(),
				replacementEvent->Result.Artifact.Bytes.end()).find("running-final") == 0,
			"same-handle replacement did not import the latest source bytes");
		waitForIdle();
		counting->DelayMilliseconds = 15;

		first = events.size();
		invocations = counting->Invocations.load();
		WriteBytes(heroPath, "rapid-one");
		coordinator.RequestScan();
		std::this_thread::sleep_for(std::chrono::milliseconds(22));
		WriteBytes(heroPath, "rapid-two");
		coordinator.RequestScan();
		std::this_thread::sleep_for(std::chrono::milliseconds(22));
		WriteBytes(heroPath, "rapid-final");
		coordinator.RequestScan();
		waitFor([&]()
		{
			return matchingEvent(first, hero, false,
				TomCat::AssetFileChangeKind::Modified, true);
		}, "debounced rapid write was not imported");
		Require(counting->Invocations == invocations + 1,
			"rapid writes were not coalesced into one import");
		const auto rapidEvent = std::find_if(
			events.begin() + static_cast<std::ptrdiff_t>(first), events.end(),
			[hero](const TomCat::AssetImportEvent& event)
			{
				return event.Handle == hero && !event.IsDependency
					&& event.Result.Succeeded();
			});
		Require(rapidEvent != events.end()
			&& std::string(rapidEvent->Result.Artifact.Bytes.begin(),
				rapidEvent->Result.Artifact.Bytes.end()).find("rapid-final") == 0,
			"coalesced import did not use the latest bytes");
		waitForIdle();

		first = events.size();
		invocations = counting->Invocations.load();
		std::error_code timeError;
		const auto oldWriteTime = std::filesystem::last_write_time(heroPath, timeError);
		Require(!timeError, "could not read source mtime");
		std::filesystem::last_write_time(heroPath,
			oldWriteTime + std::chrono::seconds(2), timeError);
		Require(!timeError, "could not touch source mtime");
		coordinator.RequestScan();
		const auto quietDeadline = std::chrono::steady_clock::now() +
			std::chrono::milliseconds(180);
		while (std::chrono::steady_clock::now() < quietDeadline)
		{
			(void)coordinator.PumpMainThread(receive);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		Require(events.size() == first && counting->Invocations == invocations,
			"mtime-only change incorrectly triggered an import");

		first = events.size();
		invocations = counting->Invocations.load();
		const std::filesystem::path heroMeta =
			TomCat::AssetRegistry::GetMetadataPath(heroPath);
		std::string metadata = ReadText(heroMeta);
		const size_t settingOffset = metadata.find("watch-before");
		Require(settingOffset != std::string::npos,
			"could not locate test setting in tcmeta");
		metadata.replace(settingOffset, std::string("watch-before").size(),
			"watch-after");
		WriteBytes(heroMeta, metadata);
		coordinator.RequestScan();
		waitFor([&]()
		{
			return matchingEvent(first, hero, false,
				TomCat::AssetFileChangeKind::Modified, true);
		}, "external tcmeta settings change was not imported");
		Require(counting->Invocations == invocations + 1,
			"tcmeta settings change did not invalidate exactly one artifact");
		waitForIdle();

		first = events.size();
		invocations = counting->Invocations.load();
		WriteBytes(dependencyPath, MakeDependencyShader("shader-watched-change"));
		coordinator.RequestScan();
		waitFor([&]()
		{
			return matchingEvent(first, hero, true,
				TomCat::AssetFileChangeKind::Modified, true);
		}, "dependency change did not reimport its transitive dependent");
		Require(counting->Invocations == invocations + 1,
			"dependency propagation did not execute the dependent importer once");
		waitForIdle();

		first = events.size();
		const std::filesystem::path addedPath = project.Assets / "added.png";
		WriteBytes(addedPath, "new-watched-asset");
		coordinator.RequestScan();
		waitFor([&]()
		{
			return std::any_of(events.begin() + static_cast<std::ptrdiff_t>(first),
				events.end(), [&](const TomCat::AssetImportEvent& event)
				{
					return event.FilePath == std::filesystem::path("added.png") &&
						event.Result.Succeeded() &&
						static_cast<uint64_t>(event.Handle) != 0;
				});
		}, "new file was not registered and imported");
		TomCat::AssetHandle addedHandle(0);
		for (size_t index = first; index < events.size(); ++index)
		{
			if (events[index].FilePath == std::filesystem::path("added.png") &&
				events[index].Result.Succeeded())
				addedHandle = events[index].Handle;
		}
		Require(static_cast<uint64_t>(addedHandle) != 0,
			"new file completion did not expose its stable handle");

		first = events.size();
		std::error_code removeError;
		Require(std::filesystem::remove(addedPath, removeError) && !removeError,
			"could not remove monitored source");
		coordinator.RequestScan();
		waitFor([&]()
		{
			return matchingEvent(first, addedHandle, false,
				TomCat::AssetFileChangeKind::Removed, false);
		}, "deleted source did not publish a tombstone event");
		const TomCat::AssetMetadata* missing = registry.GetMetadata(addedHandle);
		Require(missing && missing->IsMissing,
			"deleted source did not leave its sidecar identity as missing");

		first = events.size();
		const std::filesystem::path renamedPath = project.Assets / "renamed.png";
		const std::filesystem::path renamedMeta =
			TomCat::AssetRegistry::GetMetadataPath(renamedPath);
		std::filesystem::rename(heroPath, renamedPath);
		std::filesystem::rename(heroMeta, renamedMeta);
		coordinator.RequestScan();
		waitFor([&]()
		{
			return matchingEvent(first, hero, false,
				TomCat::AssetFileChangeKind::Added, true);
		}, "source plus sidecar rename was not reimported");
		const TomCat::AssetMetadata* moved = registry.GetMetadata(hero);
		Require(moved && !moved->IsMissing &&
			moved->FilePath == std::filesystem::path("renamed.png"),
			"source plus sidecar rename did not preserve its handle");
		Require(!matchingEvent(first, hero, false,
			TomCat::AssetFileChangeKind::Removed, false),
			"handle-preserving rename emitted a false deletion");

		const size_t stoppedEventCount = events.size();
		coordinator.Stop();
		WriteBytes(renamedPath, "change-while-stopped");
		coordinator.RequestScan();
		std::this_thread::sleep_for(std::chrono::milliseconds(80));
		Require(coordinator.PumpMainThread(receive) == 0 &&
			events.size() == stoppedEventCount,
			"stopped coordinator published a late callback");
		std::atomic_bool raceRequestScan = true;
		std::thread scanObserver([&]()
		{
			while (raceRequestScan.load())
			{
				coordinator.RequestScan();
				std::this_thread::yield();
			}
		});
		coordinator.Shutdown();
		raceRequestScan = false;
		scanObserver.join();
		Require(!coordinator.IsInitialized()
			&& coordinator.GetPendingImportCount() == 0,
			"RequestScan/Shutdown race retained a stale request");
		Require(coordinator.Initialize(registry, database, project.Assets, options)
			&& coordinator.Start(),
			"coordinator did not restart after RequestScan/Shutdown race");
		std::this_thread::sleep_for(std::chrono::milliseconds(80));
		Require(coordinator.PumpMainThread(receive) == 0,
			"stale RequestScan escaped into a later coordinator initialization");
		coordinator.Shutdown();
		database.Shutdown();
		registry.Shutdown();
	}

	void TestAuthoringDependencyValidationRejectsInvalidEdges()
	{
		TemporaryProject project;
		const auto builtIns = TomCat::GetBuiltInSpriteAssets();
		Require(!builtIns.empty(),
			"authoring dependency validation needs one built-in Sprite");

		TomCat::AnimationClipAsset clip;
		clip.Clip.Name = "Dependency Clip";
		clip.Clip.Frames.push_back({ builtIns.front().Handle, 1.0f / 12.0f });
		std::string document;
		std::string error;
		Require(TomCat::AnimationClipAssetCodec::Encode(clip, document, error),
			"authoring dependency clip could not be encoded");
		const std::filesystem::path clipPath = project.Assets / "Dependency.tcanim";
		const std::filesystem::path shaderPath = project.Assets / "WrongType.glsl";
		WriteBytes(clipPath, document);
		WriteBytes(shaderPath, MakeDependencyShader("authoring-wrong-type"));

		TomCat::AssetRegistry registry;
		Require(registry.Initialize(project.Assets, project.Library),
			"authoring dependency registry did not initialize");
		const TomCat::AssetHandle clipHandle = RequireHandle(registry, clipPath,
			TomCat::AssetType::AnimationClip);
		const TomCat::AssetHandle shaderHandle = RequireHandle(registry, shaderPath,
			TomCat::AssetType::Shader);

		TomCat::AnimatorControllerAsset controller;
		controller.InitialState = "State";
		controller.States.push_back({ "State", clipHandle, 1.0f });
		Require(TomCat::AnimatorControllerAssetCodec::Encode(
			controller, document, error),
			"authoring dependency controller could not be encoded");
		const std::filesystem::path controllerPath =
			project.Assets / "Dependency.tccontroller";
		WriteBytes(controllerPath, document);
		Require(registry.Refresh(),
			"authoring dependency controller was not discovered");
		const TomCat::AssetHandle controllerHandle = RequireHandle(registry,
			controllerPath, TomCat::AssetType::AnimatorController);

		TomCat::AssetDatabase database;
		Require(database.Initialize(registry, project.Library),
			"authoring dependency database did not initialize");
		const auto valid = database.GetDependencySnapshot(controllerHandle);
		Require(valid.Dependencies.size() == 1
			&& valid.Dependencies[0] == clipHandle,
			"valid Controller-to-AnimationClip edge was not discovered");

		auto writeControllerReference = [&](TomCat::AssetHandle handle)
		{
			controller.States[0].ClipHandle = handle;
			Require(TomCat::AnimatorControllerAssetCodec::Encode(
				controller, document, error),
				"invalid-edge controller fixture could not be encoded");
			WriteBytes(controllerPath, document);
		};
		writeControllerReference(TomCat::AssetHandle(0x0badf00dULL));
		Require(!database.RefreshRegistry(),
			"authoring dependency discovery accepted a missing handle");
		writeControllerReference(shaderHandle);
		Require(!database.RefreshRegistry(),
			"authoring dependency discovery accepted the wrong asset type");
		writeControllerReference(controllerHandle);
		Require(!database.RefreshRegistry(),
			"authoring dependency discovery accepted a self reference");

		writeControllerReference(clipHandle);
		Require(database.RefreshRegistry(),
			"authoring dependency discovery did not recover after valid source restore");
		const auto restored = database.GetDependencySnapshot(controllerHandle);
		Require(restored.Dependencies.size() == 1
			&& restored.Dependencies[0] == clipHandle,
			"restored authoring dependency graph is incorrect");
		database.Shutdown();
		registry.Shutdown();
	}

}

int main()
{
	TomCat::Log::Init();
	try
	{
		TestScopedPrefabProperties();
		TestMixedPrefixAssetImport();
		TestOfflineTextureArtifacts();
		TestOfflineShaderArtifacts();
		TestCookedShaderRuntimeConsumption();
		TestCanonicalMaterialArtifacts();
		TestTypedMaterialDependencyLoading();
		TestCookTraversesDatabaseDependencyClosure();
		TestExplicitDependencyRefreshesTransferredSpriteOwner();
		TestPartialArtifactOwnerRefreshMergesResolvedAndMissingAliases();
		TestDependencyCacheRetainsMissingSpriteOwnerAlias();
		TestAsyncAssetOwnerLifecycle();
		TestPackedObjMeshArtifacts();
		TestOfflineAudioArtifact();
		TestMalformedFontPreflight();
		TestDeterministicImportPipeline();
		TestLoadArtifactRetriesConsistentGraphSnapshot();
		TestLoadArtifactRetriesChangedSourceSnapshot();
		TestDiscoverableSourceCannotUseStaleDependencyKeys();
		TestCoordinatorRetriesStaleDeferredFinalization();
		TestCoordinatorRefreshesDiscoverableClosureBeforeStaleRetry();
		TestFileMonitorImportCoordinator();
		TestAuthoringDependencyValidationRejectsInvalidEdges();
		TestBoundedAssetJobSystem();
		std::cout << "PASS production artifacts, bounded jobs, DDC, tcmeta v2, and monitored reimport\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL importer regression: " << exception.what() << '\n';
		return 1;
	}
}
