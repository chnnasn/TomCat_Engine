#include "TomCat/Asset/AssetDatabase.h"
#include "TomCat/Asset/AssetImportCoordinator.h"
#include "TomCat/Asset/AssetJobSystem.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/AssetRegistry.h"
#include "TomCat/Asset/MaterialArtifact.h"
#include "TomCat/Asset/MeshArtifact.h"
#include "TomCat/Asset/ShaderArtifact.h"
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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
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

	TomCat::AssetHandle RequireHandle(TomCat::AssetRegistry& registry,
		const std::filesystem::path& path, TomCat::AssetType expected);

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
			&& TomCat::AssetTypeFromPath("unsupported.fbx") == TomCat::AssetType::Other
			&& TomCat::AssetTypeFromPath("unsupported.gltf") == TomCat::AssetType::Other,
			"unsupported mesh containers are still advertised as production assets");
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
		// The dependency scanner must normalize the child Sprite handle to its atlas
		// parent because file monitor events are emitted for the parent source file.
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
		Require(database.GetDependencies(prefab) ==
			std::vector<TomCat::AssetHandle>{ hero },
			"Prefab Sprite child dependency was not normalized to the atlas parent");
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
		Require(RequireHeroSubAsset(registry, hero) == heroSprite
			&& database.GetDependencies(prefab) ==
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
		Require(database.GetDependencies(prefab) ==
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
		Require(counting->Invocations.load() == invocations + 1,
			"same-handle saves started concurrent obsolete import workers");
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

}

int main()
{
	TomCat::Log::Init();
	try
	{
		TestOfflineTextureArtifacts();
		TestOfflineShaderArtifacts();
		TestCookedShaderRuntimeConsumption();
		TestCanonicalMaterialArtifacts();
		TestTypedMaterialDependencyLoading();
		TestAsyncAssetOwnerLifecycle();
		TestPackedObjMeshArtifacts();
		TestOfflineAudioArtifact();
		TestMalformedFontPreflight();
		TestDeterministicImportPipeline();
		TestFileMonitorImportCoordinator();
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
