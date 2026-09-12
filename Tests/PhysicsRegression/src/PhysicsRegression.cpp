#include "TomCat/Core/Log.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scripting/DotNetHost.h"
#include "TomCat/Scripting/IScriptRuntime.h"
#include "TomCat/Scripting/ScriptEngine.h"
#include "TomCat/Scripting/ScriptGlue.h"

#include "box2d/b2_body.h"
#include "box2d/b2_fixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
	class ManagedRuntimeProbe final : public TomCat::Scripting::IScriptRuntime
	{
	public:
		enum class PhysicsMutation
		{
			None,
			RemoveRigidbody2D,
			DestroyEntity
		};

		bool IsReady() const override { return Ready; }
		TomCat::Scripting::ScriptStatus CreateSceneRuntime(uint64_t sceneSessionId,
			uint64_t runtimeGeneration) override
		{
			Calls.emplace_back("CreateSceneRuntime");
			LastSceneSession = sceneSessionId;
			LastRuntimeGeneration = runtimeGeneration;
			Active = true;
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus InstantiateAll(
			std::span<const TomCat::Scripting::NativeScriptAttachmentV1> attachments) override
		{
			Calls.emplace_back("InstantiateAll");
			Attachments.assign(attachments.begin(), attachments.end());
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus ApplySerializedFields(
			std::string_view fieldsJson) override
		{
			Calls.emplace_back("ApplySerializedFields");
			LastFields.assign(fieldsJson);
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus InvokeCreateAll() override
		{
			Calls.emplace_back("InvokeCreateAll");
			return InvokeCreateStatus;
		}
		TomCat::Scripting::ScriptStatus SetEnabled(uint64_t, bool) override
		{
			Calls.emplace_back("SetEnabled");
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus UpdateAll(float deltaTime) override
		{
			++UpdateCount;
			LastUpdateDelta = deltaTime;
			Calls.emplace_back("UpdateAll");
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus FixedUpdateAll(float fixedDeltaTime) override
		{
			++FixedUpdateCount;
			LastFixedDelta = fixedDeltaTime;
			Calls.emplace_back("FixedUpdateAll");
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus DispatchPhysicsEvents(
			std::span<const TomCat::Scripting::NativePhysicsEventV1> events) override
		{
			PhysicsEventCount += static_cast<uint32_t>(events.size());
			PhysicsEvents.insert(PhysicsEvents.end(), events.begin(), events.end());
			Calls.emplace_back("DispatchPhysicsEvents");
			if (!MutationAttempted && Mutation != PhysicsMutation::None
				&& !events.empty() && !Attachments.empty())
			{
				MutationAttempted = true;
				if (Mutation == PhysicsMutation::RemoveRigidbody2D)
					MutationQueued = TomCat::Scripting::ScriptEngine::Get().QueueRemoveComponent(
						Attachments.front().Entity,
						TomCat::Scripting::NativeComponentType::Rigidbody2D);
				else
					MutationQueued = TomCat::Scripting::ScriptEngine::Get().QueueDestroyEntity(
						Attachments.front().Entity);
			}
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus DestroyAll() override
		{
			Calls.emplace_back("DestroyAll");
			Active = false;
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus DestroyAttachments(
			std::span<const uint64_t> attachmentIds) override
		{
			DestroyedAttachmentCount += static_cast<uint32_t>(attachmentIds.size());
			Calls.emplace_back("DestroyAttachments");
			return TomCat::Scripting::ScriptStatus::Success;
		}
		bool PollUnload() override { return UnloadSucceeds; }
		void OnUnloadFailed(std::string_view reason) override
		{
			++UnloadFailureCount;
			LastUnloadFailure.assign(reason);
		}

		bool Ready = true;
		bool Active = false;
		bool UnloadSucceeds = true;
		TomCat::Scripting::ScriptStatus InvokeCreateStatus =
			TomCat::Scripting::ScriptStatus::Success;
		uint64_t LastSceneSession = 0;
		uint64_t LastRuntimeGeneration = 0;
		uint32_t UpdateCount = 0;
		uint32_t FixedUpdateCount = 0;
		uint32_t PhysicsEventCount = 0;
		uint32_t DestroyedAttachmentCount = 0;
		uint32_t UnloadFailureCount = 0;
		float LastUpdateDelta = 0.0f;
		float LastFixedDelta = 0.0f;
		PhysicsMutation Mutation = PhysicsMutation::None;
		bool MutationAttempted = false;
		bool MutationQueued = false;
		std::string LastFields;
		std::string LastUnloadFailure;
		std::vector<TomCat::Scripting::NativeScriptAttachmentV1> Attachments;
		std::vector<TomCat::Scripting::NativePhysicsEventV1> PhysicsEvents;
		std::vector<std::string> Calls;
	};

	class ScriptRuntimeOverride final
	{
	public:
		explicit ScriptRuntimeOverride(std::shared_ptr<TomCat::Scripting::IScriptRuntime> runtime)
		{
			TomCat::Scripting::ScriptEngine::Get().SetRuntime(std::move(runtime));
		}

		~ScriptRuntimeOverride()
		{
			TomCat::Scripting::ScriptEngine::Get().SetRuntime({});
		}

		ScriptRuntimeOverride(const ScriptRuntimeOverride&) = delete;
		ScriptRuntimeOverride& operator=(const ScriptRuntimeOverride&) = delete;
	};

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	std::string ReadTextFile(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		Require(static_cast<bool>(input), "could not open text fixture");
		return std::string(std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>());
	}

	void WriteTextFile(const std::filesystem::path& path, const std::string& contents)
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		Require(static_cast<bool>(output), "could not create text fixture");
		output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		Require(output.good(), "could not write text fixture");
	}

	std::vector<uint8_t> ReadBinaryFile(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		Require(static_cast<bool>(input), "could not open binary fixture");
		return std::vector<uint8_t>(std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>());
	}

	void WriteBinaryFile(const std::filesystem::path& path,
		const std::vector<uint8_t>& contents)
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		Require(static_cast<bool>(output), "could not create binary fixture");
		if (!contents.empty())
			output.write(reinterpret_cast<const char*>(contents.data()),
				static_cast<std::streamsize>(contents.size()));
		Require(output.good(), "could not write binary fixture");
	}

	uint16_t ReadLittleEndian16(const std::vector<uint8_t>& bytes, std::size_t offset)
	{
		Require(offset + 2 <= bytes.size(), "binary fixture has no uint16 at requested offset");
		return static_cast<uint16_t>(bytes[offset])
			| static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
	}

	uint32_t ReadLittleEndian32(const std::vector<uint8_t>& bytes, std::size_t offset)
	{
		Require(offset + 4 <= bytes.size(), "binary fixture has no uint32 at requested offset");
		uint32_t value = 0;
		for (std::size_t index = 0; index < 4; ++index)
			value |= static_cast<uint32_t>(bytes[offset + index]) << (index * 8);
		return value;
	}

	uint64_t ReadLittleEndian64(const std::vector<uint8_t>& bytes, std::size_t offset)
	{
		Require(offset + 8 <= bytes.size(), "binary fixture has no uint64 at requested offset");
		uint64_t value = 0;
		for (std::size_t index = 0; index < 8; ++index)
			value |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8);
		return value;
	}

	void WriteLittleEndian16(std::vector<uint8_t>& bytes, std::size_t offset, uint16_t value)
	{
		Require(offset + 2 <= bytes.size(), "binary fixture has no uint16 at requested offset");
		bytes[offset] = static_cast<uint8_t>(value & 0xff);
		bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
	}

	void WriteLittleEndian32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value)
	{
		Require(offset + 4 <= bytes.size(), "binary fixture has no uint32 at requested offset");
		for (std::size_t index = 0; index < 4; ++index)
			bytes[offset + index] = static_cast<uint8_t>((value >> (index * 8)) & 0xff);
	}

	void WriteLittleEndian64(std::vector<uint8_t>& bytes, std::size_t offset, uint64_t value)
	{
		Require(offset + 8 <= bytes.size(), "binary fixture has no uint64 at requested offset");
		for (std::size_t index = 0; index < 8; ++index)
			bytes[offset + index] = static_cast<uint8_t>((value >> (index * 8)) & 0xff);
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

	std::size_t FindManagedPackageIndexEntry(const std::vector<uint8_t>& package)
	{
		Require(package.size() >= 64, "package fixture has no fixed header");
		const uint64_t entryCount = ReadLittleEndian64(package, 16);
		Require(entryCount <= (package.size() - 64) / 32,
			"package fixture has an out-of-bounds index");
		for (uint64_t index = 0; index < entryCount; ++index)
		{
			const std::size_t offset = 64 + static_cast<std::size_t>(index * 32);
			if (ReadLittleEndian64(package, offset)
				== (std::numeric_limits<uint64_t>::max)())
				return offset;
		}
		throw std::runtime_error("package fixture has no managed payload index entry");
	}

	bool Near(float actual, float expected, float tolerance = 1.0e-4f)
	{
		return std::abs(actual - expected) <= tolerance;
	}

	bool Near(const glm::vec2& actual, const glm::vec2& expected,
		float tolerance = 1.0e-4f)
	{
		return Near(actual.x, expected.x, tolerance)
			&& Near(actual.y, expected.y, tolerance);
	}

	bool Near(const glm::vec3& actual, const glm::vec3& expected,
		float tolerance = 1.0e-4f)
	{
		return Near(actual.x, expected.x, tolerance)
			&& Near(actual.y, expected.y, tolerance)
			&& Near(actual.z, expected.z, tolerance);
	}

	bool Near(const glm::mat4& actual, const glm::mat4& expected,
		float tolerance = 1.0e-4f)
	{
		for (glm::length_t column = 0; column < 4; ++column)
			for (glm::length_t row = 0; row < 4; ++row)
				if (!Near(actual[column][row], expected[column][row], tolerance))
					return false;
		return true;
	}

	bool Contains(const TomCat::CollisionEnter2D& event, TomCat::UUID entityID)
	{
		return event.EntityA == entityID || event.EntityB == entityID;
	}

	bool Contains(const TomCat::CollisionExit2D& event, TomCat::UUID entityID)
	{
		return event.EntityA == entityID || event.EntityB == entityID;
	}

	bool Contains(const TomCat::TriggerEnter2D& event, TomCat::UUID entityID)
	{
		return event.EntityA == entityID || event.EntityB == entityID;
	}

	bool Contains(const TomCat::TriggerExit2D& event, TomCat::UUID entityID)
	{
		return event.EntityA == entityID || event.EntityB == entityID;
	}

	class TemporaryCookedProject final
	{
	public:
		TemporaryCookedProject()
		{
			TomCat::AssetManager::Get().Shutdown();
			Root = std::filesystem::temp_directory_path()
				/ ("tomcat_physics_cooked_"
					+ std::to_string(static_cast<uint64_t>(TomCat::UUID())));
		}

		~TemporaryCookedProject()
		{
			// A mounted package owns an open file handle on Windows, so always close
			// the process-global asset manager before removing this unique test tree.
			TomCat::AssetManager::Get().Shutdown();
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}

		std::filesystem::path Root;
	};

	void TestExplicitDotNetRootIsExclusive()
	{
		TemporaryCookedProject environment;
		std::filesystem::create_directories(environment.Root / "Managed");
		const std::filesystem::path runtimeConfig =
			environment.Root / "Managed" / "TomCat.ScriptHost.runtimeconfig.json";
		const std::filesystem::path scriptHost =
			environment.Root / "Managed" / "TomCat.ScriptHost.dll";
		WriteTextFile(runtimeConfig, "{}");
		WriteTextFile(scriptHost, "not loaded because the private runtime is missing");

		TomCat::DotNetHost host;
		TomCat::DotNetHost::Configuration configuration;
		configuration.RuntimeConfigPath = runtimeConfig;
		configuration.ScriptHostAssemblyPath = scriptHost;
		configuration.DotNetRoot = environment.Root / "MissingPrivateDotNet";
		Require(!host.Initialize(configuration)
			&& host.GetLastError().find("explicitly selected private dotnet root")
				!= std::string::npos,
			"an explicit Player dotnet root fell back to a global .NET installation");
	}

	void TestProjectSettingsPersistenceAndValidation()
	{
		TemporaryCookedProject environment;
		TomCat::ProjectConfig config;
		config.Name = "Project Settings Regression";
		config.Template = "2D";
		config.AssetDirectory = "Assets";
		config.StartScene = "Main.tomcat";
		const std::filesystem::path projectPath = environment.Root / "Project.tcproj";
		auto project = TomCat::Project::CreateNew(projectPath, config);
		Require(project != nullptr, "CreateNew rejected a valid project settings fixture");

		const TomCat::ProjectSettings defaults;
		Require(project->GetSettings() == defaults,
			"CreateNew did not initialize default project settings");
		Require(project->GetSettingsPath()
			== environment.Root / "ProjectSettings" / "ProjectSettings.json",
			"project settings path is not the project-root ProjectSettings document");
		Require(std::filesystem::is_regular_file(project->GetSettingsPath()),
			"CreateNew did not persist default project settings");
		for (std::size_t layer = 0; layer < TomCat::Physics2DLayerCount; ++layer)
			Require(defaults.Physics2D.CollisionMasks[layer] == 0xffff,
				"default collision matrix is not all-on");

		const std::string projectDocument = ReadTextFile(projectPath);
		Require(projectDocument.find("SchemaVersion: 3") != std::string::npos,
			"Project.tcproj did not remain at schema v3");
		Require(projectDocument.find("TagsAndLayers") == std::string::npos
			&& projectDocument.find("Physics2D") == std::string::npos,
			"project settings leaked into the strict Project.tcproj document");

		TomCat::EditorProjectState editorState;
		editorState.ContentBrowserCurrentDirectory = "@assets/Scripts";
		editorState.ContentBrowserExpandedNodes = { "@assets", "@assets/Scripts" };
		editorState.ExternalScriptEditor = std::filesystem::absolute(
			environment.Root / "Tools" / "Code Editor.EXE").lexically_normal();
		Require(project->SaveEditorState(editorState),
			"SaveEditorState rejected a valid absolute external C# editor path");
		TomCat::EditorProjectState loadedEditorState;
		Require(project->LoadEditorState(loadedEditorState)
			== TomCat::EditorProjectStateLoadResult::Loaded,
			"LoadEditorState could not reload project-local editor settings");
		Require(loadedEditorState.ExternalScriptEditor == editorState.ExternalScriptEditor
			&& loadedEditorState.ContentBrowserCurrentDirectory == "@assets/Scripts"
			&& loadedEditorState.ContentBrowserExpandedNodes == editorState.ContentBrowserExpandedNodes,
			"project-local editor settings did not preserve the configured C# editor and browser state");
		const std::filesystem::path editorStatePath = project->GetUserSettingsPath()
			/ "editor.json";
		const std::string validEditorStateDocument = ReadTextFile(editorStatePath);
		Require(validEditorStateDocument.find("\"externalScriptEditor\":")
			!= std::string::npos
			&& ReadTextFile(projectPath) == projectDocument,
			"external C# editor persistence was not isolated to UserSettings/editor.json");
		TomCat::EditorProjectState invalidEditorState = editorState;
		invalidEditorState.ExternalScriptEditor = "relative-editor.exe";
		Require(!project->SaveEditorState(invalidEditorState)
			&& ReadTextFile(editorStatePath) == validEditorStateDocument,
			"SaveEditorState accepted a relative C# editor path or modified the last valid state");

		TomCat::ProjectSettings customized;
		customized.TagsAndLayers.Tags = { "Untagged", "Player", "Enemy" };
		customized.TagsAndLayers.LayerNames[1] = "Player";
		customized.TagsAndLayers.LayerNames[2] = "Enemy";
		customized.Physics2D.SetLayersCollide(1, 2, false);
		Require(project->SetSettings(customized),
			"SetSettings rejected valid tags, layers, and symmetric matrix data");
		Require(project->GetSettings() == customized,
			"SetSettings did not update the in-memory project settings");
		Require(project->SaveSettings(), "SaveSettings rejected valid settings");

		auto loaded = TomCat::Project::Load(projectPath);
		Require(loaded != nullptr && loaded->GetSettings() == customized,
			"Project::Load did not roundtrip separately persisted settings");
		const std::string validSettingsDocument = ReadTextFile(project->GetSettingsPath());
		Require(validSettingsDocument.find("\"schemaVersion\": 2") != std::string::npos
			&& validSettingsDocument.find("\"tagsAndLayers\": {") != std::string::npos
			&& validSettingsDocument.find("\"physics2D\": {") != std::string::npos
			&& validSettingsDocument.find("SchemaVersion:") == std::string::npos,
			"settings writer did not emit its strict lowerCamel JSON schema");

		auto requireRejected = [&](const TomCat::ProjectSettings& invalid, const char* message)
		{
			const TomCat::ProjectSettings before = project->GetSettings();
			const std::string fileBefore = ReadTextFile(project->GetSettingsPath());
			Require(!project->SetSettings(invalid), message);
			Require(project->GetSettings() == before,
				"rejected settings changed the in-memory project state");
			Require(ReadTextFile(project->GetSettingsPath()) == fileBefore,
				"rejected settings changed the persisted project state");
		};

		TomCat::ProjectSettings invalid = customized;
		invalid.TagsAndLayers.Tags[0] = "NotUntagged";
		requireRejected(invalid, "SetSettings accepted a missing reserved Untagged tag");
		invalid = customized;
		invalid.TagsAndLayers.Tags.push_back("");
		requireRejected(invalid, "SetSettings accepted an empty tag");
		invalid = customized;
		invalid.TagsAndLayers.Tags.push_back("Player");
		requireRejected(invalid, "SetSettings accepted a duplicate tag");
		invalid = customized;
		invalid.TagsAndLayers.LayerNames[0] = "NotDefault";
		requireRejected(invalid, "SetSettings accepted a renamed reserved Default layer");
		invalid = customized;
		invalid.TagsAndLayers.LayerNames[3] = "Player";
		requireRejected(invalid, "SetSettings accepted a duplicate nonempty layer name");
		invalid = customized;
		invalid.Physics2D.CollisionMasks[1] |= uint16_t(1) << 2;
		requireRejected(invalid, "SetSettings accepted an asymmetric collision matrix");

		std::string unknownField = validSettingsDocument;
		const std::size_t rootClose = unknownField.rfind("\n}");
		Require(rootClose != std::string::npos,
			"could not locate the project settings JSON root terminator");
		unknownField.replace(rootClose, 2, ",\n  \"unexpected\": true\n}");
		WriteTextFile(project->GetSettingsPath(), unknownField);
		Require(TomCat::Project::Load(projectPath) == nullptr,
			"strict settings loader accepted an unknown top-level field");
		WriteTextFile(project->GetSettingsPath(), validSettingsDocument + "trailing-data");
		Require(TomCat::Project::Load(projectPath) == nullptr,
			"settings loader accepted invalid JSON syntax");
		WriteTextFile(project->GetSettingsPath(), validSettingsDocument);
		std::string wrongSchema = validSettingsDocument;
		const std::size_t schemaPosition = wrongSchema.find("\"schemaVersion\": 2");
		Require(schemaPosition != std::string::npos,
			"could not locate project settings schema version");
		wrongSchema.replace(schemaPosition, std::string("\"schemaVersion\": 2").size(),
			"\"schemaVersion\": 3");
		WriteTextFile(project->GetSettingsPath(), wrongSchema);
		Require(TomCat::Project::Load(projectPath) == nullptr,
			"settings loader accepted an unsupported schema version");
		WriteTextFile(project->GetSettingsPath(), validSettingsDocument);

		const std::filesystem::path legacySettingsPath = environment.Root
			/ "ProjectSettings" / "ProjectSettings.tcsettings";
		std::ostringstream legacySettings;
		legacySettings << "SchemaVersion: 1\n"
			<< "TagsAndLayers:\n"
			<< "  Tags: [Untagged, Player, Enemy]\n"
			<< "  LayerNames: [";
		for (std::size_t index = 0; index < customized.TagsAndLayers.LayerNames.size(); ++index)
		{
			if (index != 0)
				legacySettings << ", ";
			legacySettings << '"' << customized.TagsAndLayers.LayerNames[index] << '"';
		}
		legacySettings << "]\n"
			<< "Physics2D:\n"
			<< "  CollisionMasks: [";
		for (std::size_t index = 0; index < customized.Physics2D.CollisionMasks.size(); ++index)
		{
			if (index != 0)
				legacySettings << ", ";
			legacySettings << customized.Physics2D.CollisionMasks[index];
		}
		legacySettings << "]\n";
		WriteTextFile(legacySettingsPath, legacySettings.str());

		WriteTextFile(project->GetSettingsPath(), "{ invalid JSON");
		Require(TomCat::Project::Load(projectPath) == nullptr,
			"a valid legacy settings file hid a damaged authoritative JSON file");

		std::error_code removeError;
		Require(std::filesystem::remove(project->GetSettingsPath(), removeError) && !removeError,
			"could not remove JSON settings fixture for legacy compatibility test");
		auto legacySettingsProject = TomCat::Project::Load(projectPath);
		Require(legacySettingsProject != nullptr
			&& legacySettingsProject->GetSettings() == customized,
			"missing JSON settings did not fall back to valid legacy settings");
		Require(!std::filesystem::exists(project->GetSettingsPath()),
			"loading legacy settings unexpectedly rewrote the project");
		Require(legacySettingsProject->SaveSettings(),
			"SaveSettings could not migrate loaded legacy data to JSON");
		Require(std::filesystem::is_regular_file(project->GetSettingsPath()),
			"SaveSettings did not create the authoritative JSON settings file");
		auto migratedSettingsProject = TomCat::Project::Load(projectPath);
		Require(migratedSettingsProject != nullptr
			&& migratedSettingsProject->GetSettings() == customized,
			"project settings changed while legacy data was saved as JSON");

		removeError.clear();
		Require(std::filesystem::remove(project->GetSettingsPath(), removeError) && !removeError,
			"could not remove JSON settings fixture for missing-file test");
		removeError.clear();
		Require(std::filesystem::remove(legacySettingsPath, removeError) && !removeError,
			"could not remove legacy settings fixture for missing-file test");
		auto missingSettings = TomCat::Project::Load(projectPath);
		Require(missingSettings != nullptr && missingSettings->GetSettings() == defaults,
			"missing project settings did not load backward-compatible defaults");
		Require(project->SaveSettings(), "could not restore settings after missing-file test");
	}

	TomCat::Entity AddCircleBody(TomCat::Scene& scene, const char* name,
		TomCat::Rigidbody2D::BodyType bodyType, const glm::vec2& position,
		float radius = 0.5f)
	{
		TomCat::Entity entity = scene.CreateEntity(name);
		auto& transform = entity.GetComponent<TomCat::Transform>();
		transform._Translation = { position.x, position.y, 0.0f };
		transform._LocalTranslation = transform._Translation;
		auto& body = entity.AddComponent<TomCat::Rigidbody2D>();
		body.Type = bodyType;
		entity.AddComponent<TomCat::CircleCollider2D>().Radius = radius;
		return entity;
	}

	void AttachManagedProbe(TomCat::Entity entity, uint64_t scriptAsset)
	{
		TomCat::CSharpScriptEntry script;
		script.AttachmentID = TomCat::UUID();
		script.ScriptAsset = TomCat::AssetHandle(scriptAsset);
		script.LastKnownClassName = "Game.PhysicsRegressionProbe";
		entity.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(std::move(script));
	}

	void TestManagedTransformSettersRespectHierarchy()
	{
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity parent = scene.CreateEntity("Managed transform parent");
		TomCat::Entity child = scene.CreateEntity("Managed transform child");

		const glm::mat4 parentWorld = TomCat::Math::ComposeTransform(
			{ 10.0f, -4.0f, 2.0f }, { 0.0f, 0.0f, 0.35f },
			{ 2.0f, 2.0f, 2.0f });
		const glm::mat4 childWorld = TomCat::Math::ComposeTransform(
			{ 3.0f, 5.0f, -1.0f }, { 0.0f, 0.0f, -0.2f },
			{ 0.75f, 0.75f, 0.75f });
		Require(scene.SetWorldTransform(parent, parentWorld)
			&& scene.SetWorldTransform(child, childWorld)
			&& scene.SetParent(child, parent),
			"could not create the managed Transform hierarchy fixture");
		AttachManagedProbe(child, 8110);
		Require(scene.OnRuntimeStart(),
			"managed Transform hierarchy fixture did not start");

		const TomCat::Scripting::NativeApiV1 api =
			TomCat::Scripting::BuildNativeApiV1();
		const TomCat::Scripting::EntityHandleV1 handle{
			runtime->LastSceneSession,
			static_cast<uint64_t>(child.GetUUID()),
			runtime->LastRuntimeGeneration
		};
		auto requireSynchronized = [&]()
		{
			const auto& parentTransform = parent.GetComponent<TomCat::Transform>();
			const auto& childTransform = child.GetComponent<TomCat::Transform>();
			Require(Near(parentTransform.GetTransform() * childTransform.GetLocalTransform(),
				childTransform.GetTransform(), 3.0e-4f),
				"managed world Transform setter left child local/world values inconsistent");
		};
		requireSynchronized();

		const glm::vec3 requestedPosition{ 20.0f, 6.0f, -2.5f };
		Require(api.TransformSetPosition(handle,
			{ requestedPosition.x, requestedPosition.y, requestedPosition.z }) == 0,
			"managed Transform.position setter rejected a live child Entity");
		Require(Near(child.GetComponent<TomCat::Transform>()._Translation,
			requestedPosition),
			"managed Transform.position setter did not update world position");
		requireSynchronized();

		const glm::vec3 requestedRotation{ 0.0f, 0.0f, -0.65f };
		Require(api.TransformSetRotationEuler(handle,
			{ requestedRotation.x, requestedRotation.y, requestedRotation.z }) == 0,
			"managed Transform.rotation setter rejected a live child Entity");
		Require(Near(child.GetComponent<TomCat::Transform>()._Rotation,
			requestedRotation, 3.0e-4f),
			"managed Transform.rotation setter did not update world rotation");
		requireSynchronized();

		const glm::vec3 requestedScale{ 1.25f, 1.25f, 1.25f };
		Require(api.TransformSetScale(handle,
			{ requestedScale.x, requestedScale.y, requestedScale.z }) == 0,
			"managed Transform.scale setter rejected a live child Entity");
		Require(Near(child.GetComponent<TomCat::Transform>()._Scale,
			requestedScale, 3.0e-4f),
			"managed Transform.scale setter did not update world scale");
		requireSynchronized();

		TomCat::Scripting::NativeVector3 readback{};
		Require(api.TransformGetPosition(handle, &readback) == 0
			&& Near(glm::vec3(readback.X, readback.Y, readback.Z), requestedPosition),
			"managed Transform getter did not return the synchronized world position");
		scene.OnRuntimeStop();
	}

	void MoveRuntimeBody(TomCat::Entity entity, const glm::vec2& position)
	{
		auto* body = static_cast<b2Body*>(entity.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(body != nullptr, "entity has no runtime body");
		body->SetTransform({ position.x, position.y }, body->GetAngle());
	}

	void TestFixedAccumulatorAndStep()
	{
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity entity = scene.CreateEntity("Fixed step probe");
		AttachManagedProbe(entity, 8101);
		Require(scene.OnRuntimeStart(), "managed fixed-step probe did not start");

		scene.OnUpdateRuntime(TomCat::Timestep(1.0f / 120.0f));
		Require(runtime->FixedUpdateCount == 0, "half step advanced simulation early");
		scene.OnUpdateRuntime(TomCat::Timestep(1.0f / 120.0f));
		Require(runtime->FixedUpdateCount == 1,
			"two half steps did not produce one managed fixed update");
		Require(Near(runtime->LastFixedDelta, TomCat::Scene::FixedRuntimeTimestep),
			"managed script did not receive the fixed timestep");

		scene.OnUpdateRuntime(TomCat::Timestep(1.0f));
		Require(runtime->FixedUpdateCount == 1 + TomCat::Scene::MaximumRuntimeSubsteps,
			"hitch was not capped at the maximum substep count");
		scene.OnUpdateRuntime(TomCat::Timestep(0.0f));
		Require(runtime->FixedUpdateCount == 1 + TomCat::Scene::MaximumRuntimeSubsteps,
			"overdue whole steps were not discarded after substep cap");

		scene.OnRuntimeStep();
		Require(runtime->FixedUpdateCount == 2 + TomCat::Scene::MaximumRuntimeSubsteps,
			"single-step did not advance exactly one managed fixed update");
		scene.OnRuntimeStop();
		Require(!runtime->Calls.empty() && runtime->Calls.back() == "DestroyAll",
			"managed fixed-step runtime was not destroyed on stop");
	}

	struct FallingBodyResult
	{
		float PositionY = 0.0f;
		float VelocityY = 0.0f;
	};

	FallingBodyResult SimulateFallingBody(int displayHz, int seconds)
	{
		TomCat::Scene scene;
		TomCat::Entity entity = AddCircleBody(scene, "Falling body",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f });
		scene.OnRuntimeStart();
		for (int frame = 0; frame < displayHz * seconds; ++frame)
			scene.OnUpdateRuntime(TomCat::Timestep(1.0f / static_cast<float>(displayHz)));

		auto* body = static_cast<b2Body*>(entity.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(body != nullptr, "falling body did not receive a runtime body");
		const FallingBodyResult result{ body->GetPosition().y, body->GetLinearVelocity().y };
		scene.OnRuntimeStop();
		return result;
	}

	void CompareFrameRatePhysics(int simulationSeconds)
	{
		const FallingBodyResult at30Hz = SimulateFallingBody(30, simulationSeconds);
		const FallingBodyResult at60Hz = SimulateFallingBody(60, simulationSeconds);
		const FallingBodyResult at144Hz = SimulateFallingBody(144, simulationSeconds);
		Require(Near(at30Hz.PositionY, at60Hz.PositionY) && Near(at30Hz.VelocityY, at60Hz.VelocityY),
			"30Hz and 60Hz produced different physics results");
		Require(Near(at30Hz.PositionY, at144Hz.PositionY) && Near(at30Hz.VelocityY, at144Hz.VelocityY),
			"30Hz and 144Hz produced different physics results");
	}

	void TestFrameRateIndependentPhysics()
	{
		CompareFrameRatePhysics(1);
		CompareFrameRatePhysics(10);
	}

	void TestPauseRenderPathAndSingleStep()
	{
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity entity = AddCircleBody(scene, "Paused body",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f });
		AttachManagedProbe(entity, 8102);
		Require(scene.OnRuntimeStart(), "managed pause probe did not start");
		scene.OnRuntimeStep();
		auto* body = static_cast<b2Body*>(entity.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(body != nullptr, "paused test body did not receive a runtime body");
		const float pausedPosition = body->GetPosition().y;
		const uint32_t pausedUpdates = runtime->FixedUpdateCount;

		for (int frame = 0; frame < 20; ++frame)
			scene.OnRenderRuntime();
		Require(runtime->FixedUpdateCount == pausedUpdates && runtime->UpdateCount == 0,
			"render-only pause path advanced managed scripts");
		Require(Near(body->GetPosition().y, pausedPosition),
			"render-only pause path advanced physics");

		scene.OnRuntimeStep();
		Require(runtime->FixedUpdateCount == pausedUpdates + 1,
			"paused single-step did not advance managed scripts exactly once");
		Require(body->GetPosition().y < pausedPosition,
			"paused single-step did not advance physics exactly one step");
		scene.OnRuntimeStop();
	}

	void TestColliderOnlyStaticBody()
	{
		TomCat::Scene scene;
		TomCat::Entity ground = scene.CreateEntity("Collider-only ground");
		auto& groundTransform = ground.GetComponent<TomCat::Transform>();
		groundTransform._Translation.y = -1.0f;
		groundTransform._LocalTranslation = groundTransform._Translation;
		auto& box = ground.AddComponent<TomCat::BoxCollider2D>();
		box.Size = { 5.0f, 0.5f };
		TomCat::Entity ball = AddCircleBody(scene, "Ball",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 2.0f });

		int enters = 0;
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D&) { ++enters; });
		scene.OnRuntimeStart();
		Require(box.RuntimeFixture != nullptr,
			"collider without Rigidbody2D did not create a static fixture");
		for (int step = 0; step < 120; ++step)
			scene.OnRuntimeStep();
		Require(enters == 1, "dynamic body did not collide exactly once with collider-only static body");
		auto* body = static_cast<b2Body*>(ball.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(body && body->GetPosition().y > -0.1f,
			"dynamic body passed through collider-only static body");
		scene.OnRuntimeStop();
	}

	void TestCircleNonUniformScaleFixture()
	{
		TomCat::Scene scene;
		TomCat::Entity entity = scene.CreateEntity("Scaled circle");
		auto& transform = entity.GetComponent<TomCat::Transform>();
		transform._Translation = { 4.0f, 5.0f, 2.0f };
		transform._Rotation = { 0.0f, 0.0f, 1.57079632679f };
		transform._Scale = { 2.0f, 3.0f, 1.0f };
		entity.AddComponent<TomCat::Rigidbody2D>();
		auto& circle = entity.AddComponent<TomCat::CircleCollider2D>();
		circle.Offset = { 1.0f, -2.0f };
		circle.Radius = 0.5f;

		scene.OnRuntimeStart();
		Require(circle.RuntimeFixture != nullptr, "CircleCollider2D did not create a fixture");
		const auto shapes = scene.GetColliderDebugShapes(true);
		Require(shapes.size() == 1, "runtime did not expose exactly one circle fixture");
		Require(shapes[0].Type == TomCat::ColliderDebugShapeType::Circle,
			"runtime fixture was not reported as a circle");
		Require(Near(shapes[0].Radius, 1.5f),
			"circle radius did not use max(abs(scale.x), abs(scale.y))");
		Require(Near(shapes[0].Center.x, 10.0f) && Near(shapes[0].Center.y, 7.0f),
			"scaled/rotated circle offset did not match the Box2D fixture");
		scene.OnRuntimeStop();
	}

	void TestAuthoringAndRuntimeOutlinesMatch()
	{
		TomCat::Scene scene;
		TomCat::Entity entity = scene.CreateEntity("Outline parity");
		auto& transform = entity.GetComponent<TomCat::Transform>();
		transform._Translation = { 3.0f, -4.0f, 2.0f };
		transform._LocalTranslation = transform._Translation;
		transform._Rotation.z = 0.7f;
		transform._LocalRotation = transform._Rotation;
		transform._Scale = { -2.0f, 3.0f, 1.0f };
		transform._LocalScale = transform._Scale;
		entity.AddComponent<TomCat::Rigidbody2D>();
		auto& box = entity.AddComponent<TomCat::BoxCollider2D>();
		box.Offset = { 1.0f, -0.5f };
		box.Size = { 1.25f, 0.75f };
		box.IsTrigger = true;
		box.CollisionLayer = 0x0004;
		auto& circle = entity.AddComponent<TomCat::CircleCollider2D>();
		circle.Offset = { -0.25f, 1.5f };
		circle.Radius = 0.4f;
		circle.CollisionLayer = 0x0008;

		const auto authoringShapes = scene.GetColliderDebugShapes(false);
		Require(authoringShapes.size() == 2,
			"authoring visualization did not expose both colliders");
		scene.OnRuntimeStart();
		const auto runtimeShapes = scene.GetColliderDebugShapes(true);
		Require(runtimeShapes.size() == authoringShapes.size(),
			"runtime fixture visualization count differs from authoring visualization");
		for (const auto& authoring : authoringShapes)
		{
			const auto runtimeIt = std::find_if(runtimeShapes.begin(), runtimeShapes.end(),
				[&](const auto& runtime)
				{
					return runtime.EntityID == authoring.EntityID && runtime.Type == authoring.Type;
				});
			Require(runtimeIt != runtimeShapes.end(),
				"runtime fixture visualization omitted an authoring collider");
			Require(runtimeIt->Enabled == authoring.Enabled
				&& runtimeIt->IsTrigger == authoring.IsTrigger
				&& runtimeIt->CollisionLayer == authoring.CollisionLayer
				&& Near(runtimeIt->Center, authoring.Center)
				&& Near(runtimeIt->HalfSize, authoring.HalfSize)
				&& Near(runtimeIt->Radius, authoring.Radius)
				&& Near(runtimeIt->Rotation, authoring.Rotation)
				&& Near(runtimeIt->Transform, authoring.Transform),
				"authoring collider outline does not exactly match its Box2D fixture outline");
		}
		scene.OnRuntimeStop();
	}

	void TestTriggerAndManagedCallbacks()
	{
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity trigger = scene.CreateEntity("Trigger");
		auto& triggerCollider = trigger.AddComponent<TomCat::CircleCollider2D>();
		triggerCollider.Radius = 2.0f;
		triggerCollider.IsTrigger = true;
		AttachManagedProbe(trigger, 8103);
		TomCat::Entity body = AddCircleBody(scene, "Trigger visitor",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 0.5f);
		AttachManagedProbe(body, 8104);
		const TomCat::UUID triggerUUID = trigger.GetUUID();
		const TomCat::UUID bodyUUID = body.GetUUID();

		int collisionEnters = 0;
		int triggerEnters = 0;
		int triggerExits = 0;
		bool invalidListenerPair = false;
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D&) { ++collisionEnters; });
		scene.AddTriggerEnter2DListener([&](const TomCat::TriggerEnter2D& event)
		{
			++triggerEnters;
			invalidListenerPair |= !Contains(event, triggerUUID) || !Contains(event, bodyUUID);
		});
		scene.AddTriggerExit2DListener([&](const TomCat::TriggerExit2D& event)
		{
			++triggerExits;
			invalidListenerPair |= !Contains(event, triggerUUID) || !Contains(event, bodyUUID);
		});
		Require(scene.OnRuntimeStart(), "managed trigger probe did not start");
		scene.OnRuntimeStep();
		Require(triggerEnters == 1 && collisionEnters == 0,
			"sensor contact was not routed exclusively as TriggerEnter2D");
		Require(runtime->PhysicsEvents.size() == 1
			&& runtime->PhysicsEvents.front().Kind == static_cast<uint32_t>(
				TomCat::Scripting::NativePhysicsEventKind::TriggerEnter),
			"managed runtime did not receive one TriggerEnter2D event");
		const auto& managedEnter = runtime->PhysicsEvents.front();
		const bool managedPairMatches =
			(managedEnter.EntityA.EntityId == static_cast<uint64_t>(triggerUUID)
				&& managedEnter.EntityB.EntityId == static_cast<uint64_t>(bodyUUID))
			|| (managedEnter.EntityA.EntityId == static_cast<uint64_t>(bodyUUID)
				&& managedEnter.EntityB.EntityId == static_cast<uint64_t>(triggerUUID));
		Require(managedPairMatches && !invalidListenerPair,
			"trigger callbacks received an unrelated entity pair");

		MoveRuntimeBody(body, { 10.0f, 0.0f });
		scene.OnRuntimeStep();
		Require(triggerExits == 1 && runtime->PhysicsEvents.size() == 2
			&& runtime->PhysicsEvents.back().Kind == static_cast<uint32_t>(
				TomCat::Scripting::NativePhysicsEventKind::TriggerExit),
			"TriggerExit2D was not delivered once to listeners and the managed runtime");
		scene.OnRuntimeStep();
		Require(triggerEnters == 1 && triggerExits == 1
			&& runtime->PhysicsEvents.size() == 2,
			"persistent separation emitted duplicate trigger events");
		scene.OnRuntimeStop();
	}

	void TestCollisionFilteringAndRuntimeRebuild()
	{
		TomCat::Scene scene;
		TomCat::Entity staticEntity = scene.CreateEntity("Filtered static");
		auto& staticCollider = staticEntity.AddComponent<TomCat::CircleCollider2D>();
		staticCollider.Radius = 2.0f;
		staticCollider.CollisionLayer = 0x0001;
		staticCollider.CollisionMask = 0x0002;
		TomCat::Entity dynamicEntity = AddCircleBody(scene, "Filtered dynamic",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 1.0f);
		auto& dynamicCollider = dynamicEntity.GetComponent<TomCat::CircleCollider2D>();
		dynamicCollider.CollisionLayer = 0x0002;
		dynamicCollider.CollisionMask = 0x0000;

		int enters = 0;
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D&) { ++enters; });
		scene.OnRuntimeStart();
		scene.OnRuntimeStep();
		Require(enters == 0, "collision layer/mask rejected pair still collided");

		dynamicCollider.CollisionMask = 0x0001;
		scene.OnRuntimeStep();
		Require(enters == 1,
			"runtime filter edit did not rebuild fixtures and enable contact at a safe step boundary");

		dynamicCollider.Radius = 3.0f;
		scene.OnRuntimeStep();
		const auto resized = scene.GetColliderDebugShapes(true);
		const auto resizedIt = std::find_if(resized.begin(), resized.end(), [&](const auto& shape)
		{
			return shape.EntityID == dynamicEntity.GetUUID()
				&& shape.Type == TomCat::ColliderDebugShapeType::Circle;
		});
		Require(resizedIt != resized.end() && Near(resizedIt->Radius, 3.0f),
			"runtime collider data edit did not rebuild the Box2D fixture");

		dynamicEntity.RemoveComponent<TomCat::CircleCollider2D>();
		scene.OnRuntimeStep();
		const auto removed = scene.GetColliderDebugShapes(true);
		Require(std::none_of(removed.begin(), removed.end(), [&](const auto& shape)
		{
			return shape.EntityID == dynamicEntity.GetUUID();
		}), "runtime collider removal left a stale Box2D fixture");

		auto& box = dynamicEntity.AddComponent<TomCat::BoxCollider2D>();
		box.Size = { 0.75f, 1.25f };
		scene.OnRuntimeStep();
		Require(box.RuntimeFixture != nullptr,
			"runtime collider addition did not create a Box2D fixture");
		box.Friction = 1.25f;
		scene.OnRuntimeStep();
		Require(box.RuntimeFixture != nullptr
			&& Near(static_cast<b2Fixture*>(box.RuntimeFixture)->GetFriction(), 1.25f),
			"runtime collider material edit did not rebuild the Box2D fixture");
		box.Enabled = false;
		scene.OnRuntimeStep();
		Require(box.RuntimeFixture == nullptr,
			"disabling a collider at runtime left its Box2D fixture enabled");
		box.Enabled = true;
		scene.OnRuntimeStep();
		Require(box.RuntimeFixture != nullptr,
			"re-enabling a collider at runtime did not recreate its Box2D fixture");

		auto& runtimeRigidbody = dynamicEntity.GetComponent<TomCat::Rigidbody2D>();
		runtimeRigidbody.Type = TomCat::Rigidbody2D::BodyType::Kinematic;
		runtimeRigidbody.FixedRotation = true;
		scene.OnRuntimeStep();
		Require(runtimeRigidbody.RuntimeBody != nullptr
			&& static_cast<b2Body*>(runtimeRigidbody.RuntimeBody)->GetType() == b2_kinematicBody
			&& static_cast<b2Body*>(runtimeRigidbody.RuntimeBody)->IsFixedRotation(),
			"runtime Rigidbody2D type/fixed-rotation edit was not rebuilt");
		runtimeRigidbody.Enabled = false;
		scene.OnRuntimeStep();
		Require(runtimeRigidbody.RuntimeBody == nullptr && box.RuntimeFixture == nullptr,
			"disabling Rigidbody2D did not remove its body and fixtures");
		runtimeRigidbody.Enabled = true;
		runtimeRigidbody.Type = TomCat::Rigidbody2D::BodyType::Dynamic;
		scene.OnRuntimeStep();
		Require(runtimeRigidbody.RuntimeBody != nullptr && box.RuntimeFixture != nullptr,
			"re-enabling Rigidbody2D did not recreate its body and fixtures");

		dynamicEntity.RemoveComponent<TomCat::Rigidbody2D>();
		scene.OnRuntimeStep();
		Require(box.RuntimeFixture != nullptr,
			"removing Rigidbody2D did not retain collider as a static runtime body");
		Require(!scene.ApplyLinearImpulse2D(dynamicEntity.GetUUID(), { 1.0f, 0.0f }),
			"force API accepted a collider-only static body");
		auto& addedRigidbody = dynamicEntity.AddComponent<TomCat::Rigidbody2D>();
		addedRigidbody.Type = TomCat::Rigidbody2D::BodyType::Dynamic;
		scene.OnRuntimeStep();
		Require(addedRigidbody.RuntimeBody != nullptr
			&& static_cast<b2Body*>(addedRigidbody.RuntimeBody)->GetType() == b2_dynamicBody,
			"runtime Rigidbody2D addition did not replace the implicit static body");
		scene.OnRuntimeStop();
	}

	struct FilterGateResult
	{
		int CollisionEnters = 0;
		int TriggerEnters = 0;
	};

	FilterGateResult RunProjectAndFixtureFilterCase(bool projectAllows,
		bool fixtureAllows, bool trigger)
	{
		TomCat::Scene scene;
		TomCat::Physics2DSettings settings;
		settings.SetLayersCollide(1, 2, projectAllows);
		scene.SetPhysics2DSettings(settings);

		TomCat::Entity staticEntity = scene.CreateEntity("Independent filter static");
		staticEntity.GetComponent<TomCat::EntityMetadata>().Layer = 1;
		auto& staticCollider = staticEntity.AddComponent<TomCat::CircleCollider2D>();
		staticCollider.Radius = 2.0f;
		staticCollider.IsTrigger = trigger;
		// Fixture bits deliberately do not correspond to entity-layer indices.
		staticCollider.CollisionLayer = 0x0040;
		staticCollider.CollisionMask = 0x0200;

		TomCat::Entity dynamicEntity = AddCircleBody(scene, "Independent filter dynamic",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 1.0f);
		dynamicEntity.GetComponent<TomCat::EntityMetadata>().Layer = 2;
		auto& dynamicCollider = dynamicEntity.GetComponent<TomCat::CircleCollider2D>();
		dynamicCollider.CollisionLayer = 0x0200;
		dynamicCollider.CollisionMask = fixtureAllows ? 0x0040 : 0x0000;

		FilterGateResult result;
		scene.AddCollisionEnter2DListener(
			[&](const TomCat::CollisionEnter2D&) { ++result.CollisionEnters; });
		scene.AddTriggerEnter2DListener(
			[&](const TomCat::TriggerEnter2D&) { ++result.TriggerEnters; });
		scene.OnRuntimeStart();
		auto* staticFixture = static_cast<b2Fixture*>(staticCollider.RuntimeFixture);
		auto* dynamicFixture = static_cast<b2Fixture*>(dynamicCollider.RuntimeFixture);
		Require(staticFixture && dynamicFixture,
			"independent filter fixture pair was not created");
		const b2Filter staticFilter = staticFixture->GetFilterData();
		const b2Filter dynamicFilter = dynamicFixture->GetFilterData();
		Require(staticFilter.categoryBits == 0x0040 && staticFilter.maskBits == 0x0200
			&& dynamicFilter.categoryBits == 0x0200
			&& dynamicFilter.maskBits == (fixtureAllows ? 0x0040 : 0x0000),
			"entity project layers overwrote per-fixture Box2D filter bits");
		scene.OnRuntimeStep();
		scene.OnRuntimeStop();
		return result;
	}

	int RunAsymmetricProjectFilterCase(bool allowOneToTwo, bool reverseCreation)
	{
		TomCat::Scene scene;
		TomCat::Physics2DSettings settings;
		const uint16_t layerOneBit = uint16_t(1) << 1;
		const uint16_t layerTwoBit = uint16_t(1) << 2;
		if (allowOneToTwo)
		{
			settings.CollisionMasks[1] |= layerTwoBit;
			settings.CollisionMasks[2] &= static_cast<uint16_t>(~layerOneBit);
		}
		else
		{
			settings.CollisionMasks[1] &= static_cast<uint16_t>(~layerTwoBit);
			settings.CollisionMasks[2] |= layerOneBit;
		}
		scene.SetPhysics2DSettings(settings);

		TomCat::Entity staticEntity;
		TomCat::Entity dynamicEntity;
		auto createStatic = [&]()
		{
			staticEntity = scene.CreateEntity("Asymmetric filter static");
			staticEntity.GetComponent<TomCat::EntityMetadata>().Layer = 1;
			auto& collider = staticEntity.AddComponent<TomCat::CircleCollider2D>();
			collider.Radius = 2.0f;
		};
		auto createDynamic = [&]()
		{
			dynamicEntity = AddCircleBody(scene, "Asymmetric filter dynamic",
				TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 1.0f);
			dynamicEntity.GetComponent<TomCat::EntityMetadata>().Layer = 2;
		};
		if (reverseCreation)
		{
			createDynamic();
			createStatic();
		}
		else
		{
			createStatic();
			createDynamic();
		}

		int collisionEnters = 0;
		scene.AddCollisionEnter2DListener(
			[&](const TomCat::CollisionEnter2D&) { ++collisionEnters; });
		scene.OnRuntimeStart();
		Require(staticEntity.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture
			&& dynamicEntity.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture,
			"asymmetric project-filter fixtures were not created");
		scene.OnRuntimeStep();
		scene.OnRuntimeStop();
		return collisionEnters;
	}

	void TestProjectMatrixAndFixtureFilters()
	{
		FilterGateResult result = RunProjectAndFixtureFilterCase(false, true, false);
		Require(result.CollisionEnters == 0 && result.TriggerEnters == 0,
			"project collision matrix did not independently reject a solid contact");
		result = RunProjectAndFixtureFilterCase(true, false, false);
		Require(result.CollisionEnters == 0 && result.TriggerEnters == 0,
			"fixture category/mask did not independently reject a solid contact");
		result = RunProjectAndFixtureFilterCase(true, true, false);
		Require(result.CollisionEnters == 1 && result.TriggerEnters == 0,
			"two enabled independent gates did not admit one solid contact");

		result = RunProjectAndFixtureFilterCase(false, true, true);
		Require(result.CollisionEnters == 0 && result.TriggerEnters == 0,
			"project collision matrix did not reject a trigger contact");
		result = RunProjectAndFixtureFilterCase(true, false, true);
		Require(result.CollisionEnters == 0 && result.TriggerEnters == 0,
			"fixture category/mask did not reject a trigger contact");
		result = RunProjectAndFixtureFilterCase(true, true, true);
		Require(result.CollisionEnters == 0 && result.TriggerEnters == 1,
			"two enabled independent gates did not admit one trigger contact");

		for (bool allowOneToTwo : { false, true })
		{
			for (bool reverseCreation : { false, true })
			{
				Require(RunAsymmetricProjectFilterCase(allowOneToTwo, reverseCreation) == 0,
					"an asymmetric direct Scene collision matrix admitted a contact");
			}
		}
	}

	void TestQueriesAndMotionAPI()
	{
		TomCat::Scene scene;
		TomCat::Entity solid = scene.CreateEntity("Query solid");
		solid.GetComponent<TomCat::EntityMetadata>().Layer = 2;
		auto& solidTransform = solid.GetComponent<TomCat::Transform>();
		solidTransform._Translation.x = 3.0f;
		solidTransform._LocalTranslation = solidTransform._Translation;
		auto& solidBox = solid.AddComponent<TomCat::BoxCollider2D>();
		solidBox.Size = { 0.5f, 0.5f };
		// Raw fixture filters are deliberately unrelated to the entity layer.
		solidBox.CollisionLayer = 0x0040;

		TomCat::Entity trigger = scene.CreateEntity("Query trigger");
		trigger.GetComponent<TomCat::EntityMetadata>().Layer = 3;
		auto& triggerTransform = trigger.GetComponent<TomCat::Transform>();
		triggerTransform._Translation.x = 6.0f;
		triggerTransform._LocalTranslation = triggerTransform._Translation;
		auto& triggerCircle = trigger.AddComponent<TomCat::CircleCollider2D>();
		triggerCircle.Radius = 1.0f;
		triggerCircle.IsTrigger = true;
		triggerCircle.CollisionLayer = 0x0200;

		TomCat::Entity missingMetadata = scene.CreateEntity("Query missing metadata");
		auto& missingTransform = missingMetadata.GetComponent<TomCat::Transform>();
		missingTransform._Translation.x = 9.0f;
		missingTransform._LocalTranslation = missingTransform._Translation;
		missingMetadata.AddComponent<TomCat::BoxCollider2D>().Size = { 0.5f, 0.5f };
		missingMetadata.RemoveComponent<TomCat::EntityMetadata>();

		TomCat::Entity invalidMetadata = scene.CreateEntity("Query invalid metadata");
		invalidMetadata.GetComponent<TomCat::EntityMetadata>().Layer =
			static_cast<uint8_t>(TomCat::Physics2DLayerCount);
		auto& invalidTransform = invalidMetadata.GetComponent<TomCat::Transform>();
		invalidTransform._Translation.x = 11.0f;
		invalidTransform._LocalTranslation = invalidTransform._Translation;
		invalidMetadata.AddComponent<TomCat::CircleCollider2D>().Radius = 0.5f;

		TomCat::Entity dynamicEntity = AddCircleBody(scene, "Motion API body",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 20.0f, 0.0f }, 1.0f);
		scene.OnRuntimeStart();

		const auto rayHit = scene.Raycast2D({ 0.0f, 0.0f }, { 10.0f, 0.0f }, 0x000C, true);
		Require(rayHit && rayHit->EntityID == solid.GetUUID() && !rayHit->IsTrigger
			&& rayHit->CollisionLayer == 0x0004,
			"raycast did not return the nearest matching entity layer");
		const auto triggerRay = scene.Raycast2D({ 4.0f, 0.0f }, { 10.0f, 0.0f }, 0x0008, true);
		Require(triggerRay && triggerRay->EntityID == trigger.GetUUID() && triggerRay->IsTrigger
			&& triggerRay->CollisionLayer == 0x0008,
			"raycast did not include a trigger on the matching entity layer");
		Require(!scene.Raycast2D({ 4.0f, 0.0f }, { 10.0f, 0.0f }, 0x0008, false),
			"raycast includeTriggers=false still returned a sensor");
		Require(!scene.Raycast2D({ 0.0f, 0.0f }, { 12.0f, 0.0f }, 0x0240, true),
			"raycast filtered by raw fixture category bits instead of entity layers");

		const auto allHits = scene.QueryAABB2D({ 1.0f, -2.0f }, { 12.0f, 2.0f }, 0x000C, true);
		Require(allHits.size() == 2, "AABB query did not return both matching entities");
		const auto solidHit = std::find_if(allHits.begin(), allHits.end(), [&](const auto& hit)
		{
			return hit.EntityID == solid.GetUUID();
		});
		const auto triggerHit = std::find_if(allHits.begin(), allHits.end(), [&](const auto& hit)
		{
			return hit.EntityID == trigger.GetUUID();
		});
		Require(solidHit != allHits.end() && solidHit->CollisionLayer == 0x0004
			&& triggerHit != allHits.end() && triggerHit->CollisionLayer == 0x0008,
			"AABB query results did not report entity-layer bits");
		const auto solidHits = scene.QueryAABB2D({ 1.0f, -2.0f }, { 12.0f, 2.0f }, 0x000C, false);
		Require(solidHits.size() == 1 && solidHits[0].EntityID == solid.GetUUID()
			&& solidHits[0].CollisionLayer == 0x0004,
			"AABB query trigger exclusion failed");
		Require(scene.QueryAABB2D({ 8.0f, -2.0f }, { 12.0f, 2.0f }, 0xFFFF, true).empty(),
			"AABB query returned an entity with missing or invalid layer metadata");

		Require(scene.SetLinearVelocity2D(dynamicEntity.GetUUID(), { 3.0f, 4.0f }),
			"SetLinearVelocity2D rejected a dynamic body");
		const auto initialVelocity = scene.GetLinearVelocity2D(dynamicEntity.GetUUID());
		Require(initialVelocity && Near(initialVelocity->x, 3.0f) && Near(initialVelocity->y, 4.0f),
			"GetLinearVelocity2D did not return the assigned velocity");
		Require(scene.ApplyLinearImpulse2D(dynamicEntity.GetUUID(), { 2.0f, 0.0f }),
			"ApplyLinearImpulse2D rejected a dynamic body");
		const auto impulseVelocity = scene.GetLinearVelocity2D(dynamicEntity.GetUUID());
		Require(impulseVelocity && impulseVelocity->x > initialVelocity->x,
			"linear impulse did not change velocity immediately");
		Require(scene.ApplyForce2D(dynamicEntity.GetUUID(), { 60.0f, 0.0f }),
			"ApplyForce2D rejected a dynamic body");
		Require(scene.ApplyForceAtPoint2D(dynamicEntity.GetUUID(), { 0.0f, 20.0f },
			{ 21.0f, 0.0f }), "ApplyForceAtPoint2D rejected a dynamic body");
		Require(scene.ApplyLinearImpulseAtPoint2D(dynamicEntity.GetUUID(), { 0.0f, 2.0f },
			{ 21.0f, 0.0f }), "ApplyLinearImpulseAtPoint2D rejected a dynamic body");
		auto* motionBody = static_cast<b2Body*>(
			dynamicEntity.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(motionBody && std::abs(motionBody->GetAngularVelocity()) > 0.0f,
			"at-point impulse did not produce angular velocity");
		const float beforeForceStep = impulseVelocity->x;
		scene.OnRuntimeStep();
		const auto forceVelocity = scene.GetLinearVelocity2D(dynamicEntity.GetUUID());
		Require(forceVelocity && forceVelocity->x > beforeForceStep,
			"force did not change velocity on the next fixed step");
		Require(!scene.SetLinearVelocity2D(solid.GetUUID(), { 1.0f, 0.0f }),
			"velocity API accepted a collider-only static body");
		scene.OnRuntimeStop();
	}

	void TestDistanceJoint()
	{
		TomCat::Scene scene;
		TomCat::Entity bodyA = AddCircleBody(scene, "Joint A",
			TomCat::Rigidbody2D::BodyType::Dynamic, { -2.0f, 5.0f });
		TomCat::Entity bodyB = AddCircleBody(scene, "Joint B",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 2.0f, 5.0f });
		auto& joint = bodyA.AddComponent<TomCat::DistanceJoint2D>();
		joint.ConnectedEntity = bodyB.GetUUID();
		joint.Distance = 2.0f;
		joint.Frequency = 0.0f;
		joint.Damping = 0.0f;

		scene.OnRuntimeStart();
		Require(joint.RuntimeJoint != nullptr, "DistanceJoint2D did not create a Box2D joint");
		for (int step = 0; step < 120; ++step)
			scene.OnRuntimeStep();
		auto* runtimeA = static_cast<b2Body*>(bodyA.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		auto* runtimeB = static_cast<b2Body*>(bodyB.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(runtimeA && runtimeB, "joint bodies were not created");
		const float distance = (runtimeB->GetPosition() - runtimeA->GetPosition()).Length();
		Require(Near(distance, 2.0f, 2.0e-2f), "DistanceJoint2D did not maintain its configured length");

		joint.Distance = 1.0f;
		scene.OnRuntimeStep();
		Require(joint.RuntimeJoint != nullptr, "runtime joint edit did not recreate the Box2D joint");
		for (int step = 0; step < 120; ++step)
			scene.OnRuntimeStep();
		runtimeA = static_cast<b2Body*>(bodyA.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		runtimeB = static_cast<b2Body*>(bodyB.GetComponent<TomCat::Rigidbody2D>().RuntimeBody);
		Require(Near((runtimeB->GetPosition() - runtimeA->GetPosition()).Length(), 1.0f, 2.0e-2f),
			"runtime Distance edit was not reflected in the Box2D joint");

		bodyA.RemoveComponent<TomCat::DistanceJoint2D>();
		scene.OnRuntimeStep();
		auto& replacementJoint = bodyA.AddComponent<TomCat::DistanceJoint2D>();
		replacementJoint.ConnectedEntity = bodyB.GetUUID();
		replacementJoint.Distance = 1.0f;
		scene.OnRuntimeStep();
		Require(replacementJoint.RuntimeJoint != nullptr,
			"runtime DistanceJoint2D removal/addition did not rebuild safely");
		scene.DestroyEntity(bodyB);
		scene.OnRuntimeStep();
		Require(replacementJoint.RuntimeJoint == nullptr
			&& static_cast<uint64_t>(replacementJoint.ConnectedEntity) == 0,
			"deleting a connected entity left a dangling joint reference or runtime joint");
		scene.OnRuntimeStop();
	}

	void RequireSameCircleFields(const TomCat::CircleCollider2D& actual,
		const TomCat::CircleCollider2D& expected, const char* context)
	{
		Require(actual.Enabled == expected.Enabled, context);
		Require(actual.IsTrigger == expected.IsTrigger, context);
		Require(actual.CollisionLayer == expected.CollisionLayer, context);
		Require(actual.CollisionMask == expected.CollisionMask, context);
		Require(Near(actual.Offset.x, expected.Offset.x) && Near(actual.Offset.y, expected.Offset.y), context);
		Require(Near(actual.Radius, expected.Radius), context);
		Require(Near(actual.Density, expected.Density), context);
		Require(Near(actual.Friction, expected.Friction), context);
		Require(Near(actual.Restitution, expected.Restitution), context);
	}

	void RequireSameBoxFields(const TomCat::BoxCollider2D& actual,
		const TomCat::BoxCollider2D& expected, const char* context)
	{
		Require(actual.Enabled == expected.Enabled, context);
		Require(actual.IsTrigger == expected.IsTrigger, context);
		Require(actual.CollisionLayer == expected.CollisionLayer, context);
		Require(actual.CollisionMask == expected.CollisionMask, context);
		Require(Near(actual.Offset.x, expected.Offset.x) && Near(actual.Offset.y, expected.Offset.y), context);
		Require(Near(actual.Size.x, expected.Size.x) && Near(actual.Size.y, expected.Size.y), context);
		Require(Near(actual.Density, expected.Density), context);
		Require(Near(actual.Friction, expected.Friction), context);
		Require(Near(actual.Restitution, expected.Restitution), context);
		Require(Near(actual.RestitutionThreshold, expected.RestitutionThreshold), context);
	}

	void RequireSameJointFields(const TomCat::DistanceJoint2D& actual,
		const TomCat::DistanceJoint2D& expected, const char* context)
	{
		Require(actual.Enabled == expected.Enabled, context);
		Require(actual.ConnectedEntity == expected.ConnectedEntity, context);
		Require(Near(actual.Anchor.x, expected.Anchor.x) && Near(actual.Anchor.y, expected.Anchor.y), context);
		Require(Near(actual.ConnectedAnchor.x, expected.ConnectedAnchor.x)
			&& Near(actual.ConnectedAnchor.y, expected.ConnectedAnchor.y), context);
		Require(Near(actual.Distance, expected.Distance), context);
		Require(Near(actual.Frequency, expected.Frequency), context);
		Require(Near(actual.Damping, expected.Damping), context);
		Require(actual.CollideConnected == expected.CollideConnected, context);
	}

	void RequireSameScriptField(const TomCat::ScriptField& actual,
		const TomCat::ScriptField& expected, const char* context)
	{
		Require(actual.FieldID == expected.FieldID && actual.Name == expected.Name
			&& actual.Type == expected.Type, context);
		Require(TomCat::IsScriptFieldValueCompatible(actual.Type, actual.Value)
			&& TomCat::IsScriptFieldValueCompatible(expected.Type, expected.Value),
			context);
		switch (actual.Type)
		{
			case TomCat::ScriptFieldType::Bool:
				Require(std::get<bool>(actual.Value) == std::get<bool>(expected.Value),
					context);
				break;
			case TomCat::ScriptFieldType::Int32:
				Require(std::get<int32_t>(actual.Value)
					== std::get<int32_t>(expected.Value), context);
				break;
			case TomCat::ScriptFieldType::Int64:
			case TomCat::ScriptFieldType::Enum:
				Require(std::get<int64_t>(actual.Value)
					== std::get<int64_t>(expected.Value), context);
				break;
			case TomCat::ScriptFieldType::Float:
				Require(Near(std::get<float>(actual.Value),
					std::get<float>(expected.Value)), context);
				break;
			case TomCat::ScriptFieldType::Double:
				Require(std::abs(std::get<double>(actual.Value)
					- std::get<double>(expected.Value)) <= 1.0e-10, context);
				break;
			case TomCat::ScriptFieldType::String:
				Require(std::get<std::string>(actual.Value)
					== std::get<std::string>(expected.Value), context);
				break;
			case TomCat::ScriptFieldType::Vector2:
				Require(Near(std::get<glm::vec2>(actual.Value),
					std::get<glm::vec2>(expected.Value)), context);
				break;
			case TomCat::ScriptFieldType::Vector3:
			{
				const glm::vec3& left = std::get<glm::vec3>(actual.Value);
				const glm::vec3& right = std::get<glm::vec3>(expected.Value);
				Require(Near(left.x, right.x) && Near(left.y, right.y)
					&& Near(left.z, right.z), context);
				break;
			}
			case TomCat::ScriptFieldType::Vector4:
			case TomCat::ScriptFieldType::Color:
			{
				const glm::vec4& left = std::get<glm::vec4>(actual.Value);
				const glm::vec4& right = std::get<glm::vec4>(expected.Value);
				Require(Near(left.x, right.x) && Near(left.y, right.y)
					&& Near(left.z, right.z) && Near(left.w, right.w), context);
				break;
			}
			case TomCat::ScriptFieldType::Entity:
			case TomCat::ScriptFieldType::AssetRef:
				Require(std::get<uint64_t>(actual.Value)
					== std::get<uint64_t>(expected.Value), context);
				break;
		}
	}

	void RequireSameCSharpScripts(const TomCat::CSharpScripts& actual,
		const TomCat::CSharpScripts& expected, bool sameAttachmentIDs,
		const char* context)
	{
		Require(actual.Scripts.size() == expected.Scripts.size(), context);
		for (size_t scriptIndex = 0; scriptIndex < actual.Scripts.size();
			++scriptIndex)
		{
			const auto& left = actual.Scripts[scriptIndex];
			const auto& right = expected.Scripts[scriptIndex];
			Require((left.AttachmentID == right.AttachmentID)
				== sameAttachmentIDs, context);
			Require(left.Enabled == right.Enabled
				&& left.ScriptAsset == right.ScriptAsset
				&& left.LastKnownClassName == right.LastKnownClassName
				&& left.Fields.size() == right.Fields.size(), context);
			for (size_t fieldIndex = 0; fieldIndex < left.Fields.size();
				++fieldIndex)
				RequireSameScriptField(left.Fields[fieldIndex],
					right.Fields[fieldIndex], context);
		}
	}

	void TestSchemaV10PersistenceAndCopies()
	{
		TemporaryCookedProject environment;
		std::filesystem::create_directories(environment.Root);
		const std::filesystem::path scenePath = environment.Root / "physics_v10_roundtrip.tomcat";
		auto source = TomCat::CreateRef<TomCat::Scene>();
		source->SetSceneName("Physics v10 roundtrip");
		TomCat::Entity entity = source->CreateEntity("Circle source");
		Require(entity.HasComponent<TomCat::EntityMetadata>(),
			"CreateEntity omitted required EntityMetadata");
		Require(entity.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Untagged"
			&& entity.GetComponent<TomCat::EntityMetadata>().Layer == 0
			&& entity.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Entity,
			"CreateEntity did not initialize default EntityMetadata");
		entity.GetComponent<TomCat::EntityMetadata>().GameplayTag = "Player";
		entity.GetComponent<TomCat::EntityMetadata>().Layer = 3;
		entity.GetComponent<TomCat::EntityMetadata>().HierarchyIcon = TomCat::EntityIconMode::Sprite;
		const TomCat::UUID sourceUUID = entity.GetUUID();
		auto& circle = entity.AddComponent<TomCat::CircleCollider2D>();
		circle.Enabled = false;
		circle.IsTrigger = true;
		circle.CollisionLayer = 0x0040;
		circle.CollisionMask = 0x0081;
		circle.Offset = { 1.25f, -2.5f };
		circle.Radius = 3.75f;
		circle.Density = 2.25f;
		circle.Friction = 1.25f;
		circle.Restitution = 0.65f;
		circle.RuntimeFixture = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1234));
		const TomCat::CircleCollider2D expectedCircle = circle;

		TomCat::Entity target = source->CreateEntity("Joint target");
		target.GetComponent<TomCat::EntityMetadata>().GameplayTag = "Ground";
		target.GetComponent<TomCat::EntityMetadata>().Layer = 7;
		target.GetComponent<TomCat::EntityMetadata>().HierarchyIcon = TomCat::EntityIconMode::Collider2D;
		const TomCat::UUID targetUUID = target.GetUUID();
		auto& box = target.AddComponent<TomCat::BoxCollider2D>();
		box.Enabled = false;
		box.IsTrigger = true;
		box.CollisionLayer = 0x0200;
		box.CollisionMask = 0x0104;
		box.Offset = { -1.5f, 2.75f };
		box.Size = { 4.0f, 5.0f };
		box.Density = 3.0f;
		box.Friction = 0.25f;
		box.Restitution = 0.8f;
		box.RestitutionThreshold = 1.75f;
		box.RuntimeFixture = reinterpret_cast<void*>(static_cast<uintptr_t>(0x3456));
		const TomCat::BoxCollider2D expectedBox = box;
		constexpr std::array<TomCat::EntityIconMode, 6> iconModes = {
			TomCat::EntityIconMode::Automatic,
			TomCat::EntityIconMode::Entity,
			TomCat::EntityIconMode::Camera,
			TomCat::EntityIconMode::Sprite,
			TomCat::EntityIconMode::Rigidbody2D,
			TomCat::EntityIconMode::Collider2D
		};
		std::array<TomCat::UUID, iconModes.size()> iconModeEntityUUIDs{};
		for (std::size_t index = 0; index < iconModes.size(); ++index)
		{
			TomCat::Entity iconEntity = source->CreateEntity(
				"Icon mode " + std::to_string(index));
			iconEntity.GetComponent<TomCat::EntityMetadata>().HierarchyIcon = iconModes[index];
			iconModeEntityUUIDs[index] = iconEntity.GetUUID();
		}
		auto& joint = entity.AddComponent<TomCat::DistanceJoint2D>();
		joint.Enabled = false;
		joint.ConnectedEntity = targetUUID;
		joint.Anchor = { 1.0f, 2.0f };
		joint.ConnectedAnchor = { -3.0f, 4.0f };
		joint.Distance = 5.0f;
		joint.Frequency = 2.0f;
		joint.Damping = 0.75f;
		joint.CollideConnected = true;
		joint.RuntimeJoint = reinterpret_cast<void*>(static_cast<uintptr_t>(0x5678));
		const TomCat::DistanceJoint2D expectedJoint = joint;

		auto& csharpScripts = entity.AddComponent<TomCat::CSharpScripts>();
		TomCat::CSharpScriptEntry controller;
		controller.Enabled = true;
		controller.ScriptAsset = TomCat::AssetHandle(7001);
		controller.LastKnownClassName = "Game.PlayerController";
		controller.Fields.emplace_back("00000000000000000000000000000001",
			"_enabled", TomCat::ScriptFieldType::Bool, true);
		controller.Fields.emplace_back("00000000000000000000000000000002",
			"_lives", TomCat::ScriptFieldType::Int32, int32_t{ -42 });
		controller.Fields.emplace_back("00000000000000000000000000000003",
			"_score", TomCat::ScriptFieldType::Int64, int64_t{ -5000000000LL });
		controller.Fields.emplace_back("00000000000000000000000000000004",
			"_speed", TomCat::ScriptFieldType::Float, 12.25f);
		controller.Fields.emplace_back("00000000000000000000000000000005",
			"_precision", TomCat::ScriptFieldType::Double, -0.125);
		controller.Fields.emplace_back("00000000000000000000000000000006",
			"_label", TomCat::ScriptFieldType::String,
			std::string("saved even when metadata disappears"));
		controller.Fields.emplace_back("00000000000000000000000000000007",
			"_v2", TomCat::ScriptFieldType::Vector2,
			glm::vec2{ 1.5f, -2.5f });
		controller.Fields.emplace_back("00000000000000000000000000000008",
			"_v3", TomCat::ScriptFieldType::Vector3,
			glm::vec3{ 1.0f, 2.0f, 3.0f });
		controller.Fields.emplace_back("00000000000000000000000000000009",
			"_v4", TomCat::ScriptFieldType::Vector4,
			glm::vec4{ 4.0f, 5.0f, 6.0f, 7.0f });
		controller.Fields.emplace_back("0000000000000000000000000000000a",
			"_color", TomCat::ScriptFieldType::Color,
			glm::vec4{ 0.1f, 0.2f, 0.3f, 0.4f });
		controller.Fields.emplace_back("0000000000000000000000000000000b",
			"_state", TomCat::ScriptFieldType::Enum, int64_t{ -3 });
		controller.Fields.emplace_back("0000000000000000000000000000000c",
			"_target", TomCat::ScriptFieldType::Entity,
			static_cast<uint64_t>(targetUUID));
		controller.Fields.emplace_back("0000000000000000000000000000000d",
			"_prefab", TomCat::ScriptFieldType::AssetRef, uint64_t{ 9001 });
		csharpScripts.Scripts.emplace_back(std::move(controller));

		// This attachment and field intentionally have no current compiler metadata.
		// They must remain as raw authoring data rather than being deleted.
		TomCat::CSharpScriptEntry missing;
		missing.Enabled = false;
		missing.ScriptAsset = TomCat::AssetHandle(7002);
		missing.LastKnownClassName = "Game.RemovedBehaviour";
		missing.Fields.emplace_back("ffffffffffffffffffffffffffffffff",
			"_removedField", TomCat::ScriptFieldType::String,
			std::string("orphan-value"));
		csharpScripts.Scripts.emplace_back(std::move(missing));
		const TomCat::CSharpScripts expectedScripts = csharpScripts;

		auto copiedScene = TomCat::Scene::Copy(source);
		Require(copiedScene != nullptr, "Scene::Copy failed for schema-v10 components");
		TomCat::Entity copiedEntity = copiedScene->FindEntityByUUID(sourceUUID);
		Require(copiedEntity && copiedEntity.HasComponent<TomCat::CircleCollider2D>()
			&& copiedEntity.HasComponent<TomCat::DistanceJoint2D>()
			&& copiedEntity.HasComponent<TomCat::EntityMetadata>()
			&& copiedEntity.HasComponent<TomCat::CSharpScripts>(),
			"Scene::Copy omitted a schema-v10 component");
		Require(copiedEntity.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Player"
			&& copiedEntity.GetComponent<TomCat::EntityMetadata>().Layer == 3
			&& copiedEntity.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Sprite,
			"Scene::Copy changed EntityMetadata");
		RequireSameCircleFields(copiedEntity.GetComponent<TomCat::CircleCollider2D>(), expectedCircle,
			"Scene::Copy changed CircleCollider2D fields");
		RequireSameJointFields(copiedEntity.GetComponent<TomCat::DistanceJoint2D>(), expectedJoint,
			"Scene::Copy changed DistanceJoint2D fields");
		RequireSameCSharpScripts(copiedEntity.GetComponent<TomCat::CSharpScripts>(),
			expectedScripts, true,
			"Scene::Copy changed C# fields or stable attachment identities");
		Require(copiedEntity.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture == nullptr
			&& copiedEntity.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint == nullptr,
			"Scene::Copy retained runtime physics pointers");
		TomCat::Entity copiedTarget = copiedScene->FindEntityByUUID(targetUUID);
		Require(copiedTarget && copiedTarget.HasComponent<TomCat::BoxCollider2D>(),
			"Scene::Copy omitted BoxCollider2D");
		Require(copiedTarget.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Ground"
			&& copiedTarget.GetComponent<TomCat::EntityMetadata>().Layer == 7
			&& copiedTarget.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Collider2D,
			"Scene::Copy changed target EntityMetadata");
		RequireSameBoxFields(copiedTarget.GetComponent<TomCat::BoxCollider2D>(), expectedBox,
			"Scene::Copy changed BoxCollider2D fields");
		Require(copiedTarget.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr,
			"Scene::Copy retained BoxCollider2D RuntimeFixture");

		TomCat::Entity duplicate = source->DuplicateEntity(entity);
		Require(duplicate && duplicate.HasComponent<TomCat::CircleCollider2D>()
			&& duplicate.HasComponent<TomCat::DistanceJoint2D>()
			&& duplicate.HasComponent<TomCat::EntityMetadata>()
			&& duplicate.HasComponent<TomCat::CSharpScripts>(),
			"DuplicateEntity omitted a schema-v10 component");
		Require(duplicate.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Player"
			&& duplicate.GetComponent<TomCat::EntityMetadata>().Layer == 3
			&& duplicate.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Sprite,
			"DuplicateEntity changed EntityMetadata");
		RequireSameCircleFields(duplicate.GetComponent<TomCat::CircleCollider2D>(), expectedCircle,
			"DuplicateEntity changed CircleCollider2D fields");
		RequireSameJointFields(duplicate.GetComponent<TomCat::DistanceJoint2D>(), expectedJoint,
			"DuplicateEntity changed DistanceJoint2D fields");
		RequireSameCSharpScripts(duplicate.GetComponent<TomCat::CSharpScripts>(),
			expectedScripts, false,
			"DuplicateEntity failed to preserve fields with fresh attachment identities");
		Require(duplicate.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture == nullptr
			&& duplicate.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint == nullptr,
			"DuplicateEntity retained runtime physics pointers");
		TomCat::Entity duplicateTarget = source->DuplicateEntity(target);
		Require(duplicateTarget && duplicateTarget.HasComponent<TomCat::BoxCollider2D>(),
			"DuplicateEntity omitted BoxCollider2D");
		Require(duplicateTarget.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Ground"
			&& duplicateTarget.GetComponent<TomCat::EntityMetadata>().Layer == 7
			&& duplicateTarget.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Collider2D,
			"DuplicateEntity changed target EntityMetadata");
		RequireSameBoxFields(duplicateTarget.GetComponent<TomCat::BoxCollider2D>(), expectedBox,
			"DuplicateEntity changed BoxCollider2D fields");
		Require(duplicateTarget.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr,
			"DuplicateEntity retained BoxCollider2D RuntimeFixture");

		TomCat::SceneSerializer writer(source);
		Require(writer.Serialize(scenePath), "schema-v10 scene serialization failed");
		Require(TomCat::SceneSerializer::ValidateCurrentFormat(scenePath),
			"serialized schema-v10 scene failed strict validation");
		const std::string serialized = ReadTextFile(scenePath);
		Require(serialized.find("SchemaVersion: 10") != std::string::npos,
			"serialized scene did not declare schema v10");
		Require(serialized.find("IsTrigger:") != std::string::npos
			&& serialized.find("CollisionLayer:") != std::string::npos
			&& serialized.find("CollisionMask:") != std::string::npos
			&& serialized.find("DistanceJoint2D:") != std::string::npos
			&& serialized.find("EntityMetadata:") != std::string::npos
			&& serialized.find("GameplayTag: Player") != std::string::npos
			&& serialized.find("Layer: 3") != std::string::npos
			&& serialized.find("HierarchyIcon: Sprite") != std::string::npos
			&& serialized.find("CSharpScripts:") != std::string::npos
			&& serialized.find("Type: AssetRef") != std::string::npos
			&& serialized.find("orphan-value") != std::string::npos,
			"serialized scene omitted schema-v10 script, metadata, or physics fields");

		auto loaded = TomCat::CreateRef<TomCat::Scene>();
		TomCat::SceneSerializer reader(loaded);
		Require(reader.Deserialize(scenePath), "schema-v10 scene deserialization failed");
		TomCat::Entity loadedEntity = loaded->FindEntityByUUID(sourceUUID);
		Require(loadedEntity && loadedEntity.HasComponent<TomCat::CircleCollider2D>()
			&& loadedEntity.HasComponent<TomCat::DistanceJoint2D>()
			&& loadedEntity.HasComponent<TomCat::EntityMetadata>()
			&& loadedEntity.HasComponent<TomCat::CSharpScripts>(),
			"loaded scene omitted a schema-v10 component");
		Require(loadedEntity.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Player"
			&& loadedEntity.GetComponent<TomCat::EntityMetadata>().Layer == 3
			&& loadedEntity.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Sprite,
			"save/load changed EntityMetadata");
		RequireSameCircleFields(loadedEntity.GetComponent<TomCat::CircleCollider2D>(), expectedCircle,
			"save/load changed CircleCollider2D fields");
		RequireSameJointFields(loadedEntity.GetComponent<TomCat::DistanceJoint2D>(), expectedJoint,
			"save/load changed DistanceJoint2D fields");
		RequireSameCSharpScripts(loadedEntity.GetComponent<TomCat::CSharpScripts>(),
			expectedScripts, true,
			"save/load changed C# fields, orphans, or attachment identities");
		Require(loadedEntity.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture == nullptr
			&& loadedEntity.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint == nullptr,
			"save/load restored runtime physics pointers");
		TomCat::Entity loadedTarget = loaded->FindEntityByUUID(targetUUID);
		Require(loadedTarget && loadedTarget.HasComponent<TomCat::BoxCollider2D>(),
			"loaded scene omitted BoxCollider2D");
		Require(loadedTarget.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Ground"
			&& loadedTarget.GetComponent<TomCat::EntityMetadata>().Layer == 7
			&& loadedTarget.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Collider2D,
			"save/load changed target EntityMetadata");
		RequireSameBoxFields(loadedTarget.GetComponent<TomCat::BoxCollider2D>(), expectedBox,
			"save/load changed BoxCollider2D fields");
		Require(loadedTarget.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr,
			"save/load restored BoxCollider2D RuntimeFixture");
		for (std::size_t index = 0; index < iconModes.size(); ++index)
		{
			TomCat::Entity loadedIconEntity = loaded->FindEntityByUUID(iconModeEntityUUIDs[index]);
			Require(loadedIconEntity
				&& loadedIconEntity.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
					== iconModes[index],
				"save/load changed one of the supported hierarchy icon tokens");
		}

		std::string obsolete = serialized;
		const size_t version = obsolete.find("SchemaVersion: 10");
		Require(version != std::string::npos, "could not locate serialized schema version");
		obsolete.replace(version, std::string("SchemaVersion: 10").size(), "SchemaVersion: 8");
		const std::filesystem::path obsoletePath = environment.Root / "schema_v8_rejected.tomcat";
		WriteTextFile(obsoletePath, obsolete);
		Require(!TomCat::SceneSerializer::ValidateCurrentFormat(obsoletePath),
			"strict current-format validation accepted schema v8");
		auto obsoleteTarget = TomCat::CreateRef<TomCat::Scene>();
		TomCat::SceneSerializer obsoleteReader(obsoleteTarget);
		Require(!obsoleteReader.Deserialize(obsoletePath),
			"scene reader accepted schema v8 instead of requiring schema 9 or 10");

		auto legacySource = TomCat::CreateRef<TomCat::Scene>();
		legacySource->SetSceneName("Schema 9 migration");
		legacySource->CreateEntity("Legacy entity");
		const std::filesystem::path legacyV10Path =
			environment.Root / "legacy_source_v10.tomcat";
		TomCat::SceneSerializer legacyWriter(legacySource);
		Require(legacyWriter.Serialize(legacyV10Path),
			"could not create a script-free schema-v10 migration fixture");
		std::string legacyV9 = ReadTextFile(legacyV10Path);
		const size_t legacyVersion = legacyV9.find("SchemaVersion: 10");
		Require(legacyVersion != std::string::npos,
			"could not locate schema-v10 migration fixture version");
		legacyV9.replace(legacyVersion, std::string("SchemaVersion: 10").size(),
			"SchemaVersion: 9");
		const std::filesystem::path legacyV9Path =
			environment.Root / "legacy_v9_accepted.tomcat";
		WriteTextFile(legacyV9Path, legacyV9);
		Require(TomCat::SceneSerializer::ValidateCurrentFormat(legacyV9Path),
			"schema-v9 migration input was rejected");
		auto legacyLoaded = TomCat::CreateRef<TomCat::Scene>();
		TomCat::SceneSerializer legacyReader(legacyLoaded);
		Require(legacyReader.Deserialize(legacyV9Path),
			"schema-v9 migration input did not deserialize");
		Require(!legacyLoaded->GetRootEntityUUIDs().empty(),
			"schema-v9 migration input lost its entity");
		const std::filesystem::path migratedV10Path =
			environment.Root / "legacy_resaved_as_v10.tomcat";
		TomCat::SceneSerializer migratedWriter(legacyLoaded);
		Require(migratedWriter.Serialize(migratedV10Path)
			&& ReadTextFile(migratedV10Path).find("SchemaVersion: 10")
				!= std::string::npos,
			"saving schema-v9 migration input did not upgrade it to schema 10");

		std::string illegalV9Scripts = serialized;
		const size_t currentVersion = illegalV9Scripts.find("SchemaVersion: 10");
		Require(currentVersion != std::string::npos,
			"could not locate schema-v10 strict-migration fixture version");
		illegalV9Scripts.replace(currentVersion,
			std::string("SchemaVersion: 10").size(), "SchemaVersion: 9");
		const std::filesystem::path illegalV9ScriptsPath =
			environment.Root / "schema_v9_with_v10_scripts.tomcat";
		WriteTextFile(illegalV9ScriptsPath, illegalV9Scripts);
		Require(!TomCat::SceneSerializer::ValidateCurrentFormat(
			illegalV9ScriptsPath),
			"strict schema-v9 migration accepted a schema-v10 CSharpScripts field");

		auto duplicateAttachmentScene = TomCat::CreateRef<TomCat::Scene>();
		TomCat::CSharpScriptEntry duplicateAttachment;
		duplicateAttachment.AttachmentID = TomCat::UUID(424242);
		duplicateAttachment.ScriptAsset = TomCat::AssetHandle(7001);
		duplicateAttachment.LastKnownClassName = "Game.DuplicateAttachment";
		duplicateAttachmentScene->CreateEntity("First attachment owner")
			.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(
				duplicateAttachment);
		duplicateAttachmentScene->CreateEntity("Second attachment owner")
			.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(
				duplicateAttachment);
		TomCat::SceneSerializer duplicateAttachmentWriter(duplicateAttachmentScene);
		Require(!duplicateAttachmentWriter.Serialize(
			environment.Root / "duplicate_attachment_ids.tomcat"),
			"scene writer accepted one C# AttachmentID on two different entities");

		auto invalidScene = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity invalidOwner = invalidScene->CreateEntity("Invalid joint owner");
		invalidOwner.AddComponent<TomCat::DistanceJoint2D>().ConnectedEntity = TomCat::UUID(9999999);
		const std::filesystem::path invalidPath = environment.Root / "invalid_joint.tomcat";
		TomCat::SceneSerializer invalidWriter(invalidScene);
		Require(!invalidWriter.Serialize(invalidPath),
			"scene writer emitted an unresolved DistanceJoint2D reference");

		auto invalidMetadataScene = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity invalidMetadata = invalidMetadataScene->CreateEntity("Invalid metadata");
		invalidMetadata.GetComponent<TomCat::EntityMetadata>().Layer =
			static_cast<uint8_t>(TomCat::Physics2DLayerCount);
		TomCat::SceneSerializer invalidMetadataWriter(invalidMetadataScene);
		Require(!invalidMetadataWriter.Serialize(environment.Root / "invalid_metadata_layer.tomcat"),
			"scene writer accepted an out-of-range EntityMetadata layer");
		invalidMetadata.GetComponent<TomCat::EntityMetadata>().Layer = 0;
		invalidMetadata.GetComponent<TomCat::EntityMetadata>().GameplayTag.clear();
		Require(!invalidMetadataWriter.Serialize(environment.Root / "invalid_metadata_tag.tomcat"),
			"scene writer accepted an empty EntityMetadata gameplay tag");
		invalidMetadata.GetComponent<TomCat::EntityMetadata>().GameplayTag = "Untagged";
		invalidMetadata.GetComponent<TomCat::EntityMetadata>().HierarchyIcon =
			static_cast<TomCat::EntityIconMode>(255);
		Require(!invalidMetadataWriter.Serialize(environment.Root / "invalid_metadata_icon.tomcat"),
			"scene writer accepted an invalid EntityMetadata hierarchy icon");

		std::string unknownIcon = serialized;
		const size_t iconToken = unknownIcon.find("HierarchyIcon: Sprite");
		Require(iconToken != std::string::npos, "could not locate serialized hierarchy icon token");
		unknownIcon.replace(iconToken, std::string("HierarchyIcon: Sprite").size(),
			"HierarchyIcon: Unknown");
		const std::filesystem::path unknownIconPath = environment.Root / "unknown_icon.tomcat";
		WriteTextFile(unknownIconPath, unknownIcon);
		Require(!TomCat::SceneSerializer::ValidateCurrentFormat(unknownIconPath),
			"scene validator accepted an unknown hierarchy icon token");

		std::string missingIcon = serialized;
		const size_t missingIconToken = missingIcon.find("HierarchyIcon: Sprite");
		Require(missingIconToken != std::string::npos,
			"could not locate hierarchy icon field for missing-field validation");
		const size_t missingIconLineStart = missingIcon.rfind('\n', missingIconToken);
		const size_t missingIconLineEnd = missingIcon.find('\n', missingIconToken);
		Require(missingIconLineEnd != std::string::npos,
			"serialized hierarchy icon field did not end with a newline");
		missingIcon.erase(missingIconLineStart == std::string::npos ? 0 : missingIconLineStart + 1,
			missingIconLineEnd - (missingIconLineStart == std::string::npos ? 0 : missingIconLineStart + 1) + 1);
		const std::filesystem::path missingIconPath = environment.Root / "missing_icon.tomcat";
		WriteTextFile(missingIconPath, missingIcon);
		Require(!TomCat::SceneSerializer::ValidateCurrentFormat(missingIconPath),
			"scene validator accepted EntityMetadata without HierarchyIcon");
	}

	void TestCookedPlayerPhysicsRoundtrip()
	{
		Require(TomCat::AssetTypeFromPath("Player.cs")
				== TomCat::AssetType::CSharpScript
			&& TomCat::AssetTypeFromPath("Player.cpp")
				== TomCat::AssetType::Other
			&& TomCat::AssetTypeFromPath("Player.h")
				== TomCat::AssetType::Other
			&& TomCat::AssetTypeFromPath("Player.lua")
				== TomCat::AssetType::Other,
			"asset classification still mixes C#, C++, headers, or Lua");

		TemporaryCookedProject environment;
		TomCat::ProjectConfig config;
		config.Name = "Physics Cook Regression";
		config.Template = "2D";
		config.AssetDirectory = "Assets";
		config.StartScene = "Main.tomcat";
		config.StartSceneHandle = TomCat::AssetHandle(0);
		auto project = TomCat::Project::CreateNew(
			environment.Root / "Project.tcproj", config);
		Require(project != nullptr, "could not create temporary project for cooked physics test");
		TomCat::ProjectSettings cookedSettings;
		cookedSettings.TagsAndLayers.Tags = { "Untagged", "Ground", "Player" };
		cookedSettings.TagsAndLayers.LayerNames[1] = "Ground";
		cookedSettings.TagsAndLayers.LayerNames[2] = "Player";
		cookedSettings.Physics2D.SetLayersCollide(1, 2, false);
		Require(project->SetSettings(cookedSettings),
			"could not persist the cooked package's Physics2D matrix");

		const std::filesystem::path scriptPath =
			project->GetAssetPath() / "CookedPlayerProbe.cs";
		const std::string sourceMarker =
			"TOMCAT_CSHARP_SOURCE_MUST_NOT_BE_COOKED_6b3a177b";
		const std::string csprojMarker =
			"TOMCAT_CSPROJ_MUST_NOT_BE_COOKED_9f71d0aa";
		const std::string objectMarker =
			"TOMCAT_OBJ_MUST_NOT_BE_COOKED_294cdc32";
		const std::string nestedLibraryMarker =
			"TOMCAT_LIBRARY_MUST_NOT_BE_COOKED_e98e59f1";
		WriteTextFile(scriptPath,
			"using TomCat; // " + sourceMarker
			+ "\npublic sealed class CookedPlayerProbe : TomCatBehaviour {}\n");
		std::filesystem::create_directories(project->GetAssetPath() / "obj");
		std::filesystem::create_directories(project->GetAssetPath() / "Library");
		WriteTextFile(project->GetAssetPath() / "Assembly-CSharp.csproj", csprojMarker);
		WriteTextFile(project->GetAssetPath() / "obj" / "authoring.bin", objectMarker);
		WriteTextFile(project->GetAssetPath() / "Library" / "authoring.bin",
			nestedLibraryMarker);

		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.SetProject(project),
			"could not initialize AssetManager for cooked physics test");
		const TomCat::AssetMetadata* scriptMetadata =
			assets.Registry().GetMetadata(scriptPath);
		Require(scriptMetadata && !scriptMetadata->IsMissing
			&& scriptMetadata->Type == TomCat::AssetType::CSharpScript
			&& static_cast<uint64_t>(scriptMetadata->Handle) != 0,
			".cs source did not receive a stable CSharpScript AssetHandle");
		const TomCat::AssetHandle scriptHandle = scriptMetadata->Handle;

		auto source = TomCat::CreateRef<TomCat::Scene>();
		source->SetSceneName("Cooked physics v10");
		TomCat::Entity ground = source->CreateEntity("Cooked box");
		ground.GetComponent<TomCat::EntityMetadata>().GameplayTag = "Ground";
		ground.GetComponent<TomCat::EntityMetadata>().Layer = 1;
		ground.GetComponent<TomCat::EntityMetadata>().HierarchyIcon = TomCat::EntityIconMode::Collider2D;
		const TomCat::UUID groundUUID = ground.GetUUID();
		auto& scripts = ground.AddComponent<TomCat::CSharpScripts>();
		TomCat::CSharpScriptEntry script;
		script.ScriptAsset = scriptHandle;
		script.LastKnownClassName = "CookedPlayerProbe";
		script.Fields.emplace_back("11111111111111111111111111111111",
			"_orphanedBuildValue", TomCat::ScriptFieldType::Float, 6.5f);
		scripts.Scripts.emplace_back(std::move(script));
		const TomCat::CSharpScripts expectedScripts = scripts;
		ground.AddComponent<TomCat::Rigidbody2D>().Type =
			TomCat::Rigidbody2D::BodyType::Static;
		auto& box = ground.AddComponent<TomCat::BoxCollider2D>();
		box.IsTrigger = true;
		box.CollisionLayer = 0x0010;
		box.CollisionMask = 0x0020;
		box.Offset = { 0.5f, -0.25f };
		box.Size = { 2.0f, 0.75f };
		box.Density = 1.5f;
		box.Friction = 0.35f;
		box.Restitution = 0.45f;
		box.RestitutionThreshold = 1.25f;
		const TomCat::BoxCollider2D expectedBox = box;

		TomCat::Entity ball = source->CreateEntity("Cooked circle");
		ball.GetComponent<TomCat::EntityMetadata>().GameplayTag = "Player";
		ball.GetComponent<TomCat::EntityMetadata>().Layer = 2;
		ball.GetComponent<TomCat::EntityMetadata>().HierarchyIcon = TomCat::EntityIconMode::Sprite;
		const TomCat::UUID ballUUID = ball.GetUUID();
		ball.GetComponent<TomCat::Transform>()._Translation = { 1.25f, -0.5f, 0.0f };
		ball.GetComponent<TomCat::Transform>()._LocalTranslation =
			ball.GetComponent<TomCat::Transform>()._Translation;
		ball.AddComponent<TomCat::Rigidbody2D>().Type =
			TomCat::Rigidbody2D::BodyType::Dynamic;
		auto& circle = ball.AddComponent<TomCat::CircleCollider2D>();
		circle.CollisionLayer = 0x0020;
		circle.CollisionMask = 0x0010;
		circle.Offset = { -0.75f, 0.25f };
		circle.Radius = 0.625f;
		circle.Density = 2.0f;
		circle.Friction = 0.2f;
		circle.Restitution = 0.6f;
		const TomCat::CircleCollider2D expectedCircle = circle;

		auto& joint = ground.AddComponent<TomCat::DistanceJoint2D>();
		joint.ConnectedEntity = ballUUID;
		joint.Anchor = { 0.25f, 0.5f };
		joint.ConnectedAnchor = { -0.5f, -0.25f };
		joint.Distance = 3.0f;
		joint.Frequency = 2.5f;
		joint.Damping = 0.4f;
		joint.CollideConnected = true;
		const TomCat::DistanceJoint2D expectedJoint = joint;

		const std::filesystem::path sourceScenePath = project->GetAssetPath() / "Main.tomcat";
		TomCat::SceneSerializer writer(source);
		Require(writer.Serialize(sourceScenePath),
			"could not serialize/import schema-v10 physics scene for cooking");
		const TomCat::AssetMetadata* sceneMetadata = assets.Registry().GetMetadata(sourceScenePath);
		Require(sceneMetadata && sceneMetadata->Type == TomCat::AssetType::Scene
			&& static_cast<uint64_t>(sceneMetadata->Handle) != 0,
			"serialized physics scene was not registered as a Scene asset");
		const TomCat::AssetHandle sceneHandle = sceneMetadata->Handle;
		Require(project->SetStartScene("Main.tomcat"),
			"could not set temporary project's start-scene path");
		project->SetStartSceneHandle(sceneHandle);
		Require(project->Save(), "could not save temporary project's start-scene handle");

		const std::filesystem::path missingPayloadPath =
			environment.Root / "Build" / "MissingManagedPayload.tcpak";
		assets.ClearManagedCookPayload();
		Require(!assets.CookToPackage(missingPayloadPath),
			"cook accepted a scene with CSharpScripts but no last-good managed payload");

		std::ostringstream manifestBuilder;
		manifestBuilder
			<< "{\"version\":1,\"scripts\":[{\"assetHandle\":"
			<< static_cast<uint64_t>(scriptHandle)
			<< ",\"typeName\":\"CookedPlayerProbe\",\"executionOrder\":0,"
				"\"disallowMultiple\":false,\"lifecycle\":0,\"fields\":[]}]}";
		const std::string managedManifest = manifestBuilder.str();
		const std::vector<uint8_t> managedAssembly =
			MakeManagedAssemblyFixture(managedManifest);
		const std::vector<uint8_t> managedPdb = { 'B', 'S', 'J', 'B', 1, 0, 0, 0 };
		const std::string buildID = "regression-build-1";
		const std::filesystem::path assemblies =
			project->GetLibraryPath() / "ScriptAssemblies";
		const std::filesystem::path buildDirectory =
			assemblies / "Build" / buildID;
		std::error_code managedDirectoryError;
		std::filesystem::create_directories(buildDirectory, managedDirectoryError);
		std::filesystem::create_directories(
			project->GetLibraryPath() / "ScriptProject", managedDirectoryError);
		Require(!managedDirectoryError,
			"could not create authoring managed-payload fixture directories");
		WriteBinaryFile(buildDirectory / "Assembly-CSharp.dll", managedAssembly);
		WriteBinaryFile(buildDirectory / "Assembly-CSharp.pdb", managedPdb);
		WriteTextFile(assemblies / "last-good.json",
			"{\n"
			"  \"version\": 1,\n"
			"  \"sourceHash\": \"0123456789abcdef\",\n"
			"  \"buildId\": \"" + buildID + "\",\n"
			"  \"assembly\": \"Build/" + buildID + "/Assembly-CSharp.dll\",\n"
			"  \"pdb\": \"Build/" + buildID + "/Assembly-CSharp.pdb\"\n"
			"}\n");
		WriteTextFile(project->GetLibraryPath() / "ScriptProject" / "ScriptAssets.json",
			"{\n  \"version\": 1,\n  \"assets\": {\n"
			"    \"CookedPlayerProbe.cs\": "
			+ std::to_string(static_cast<uint64_t>(scriptHandle))
			+ "\n  }\n}\n");

		const std::filesystem::path packagePath = environment.Root / "Build" / "Game.tcpak";
		Require(assets.CookToPackage(packagePath),
			"schema-v10 physics scene and last-good assembly did not cook into a Player package");
		assets.Shutdown();

		Require(assets.MountCookedPackage(packagePath),
			"Player path could not mount the cooked physics package");
		Require(assets.GetCookedStartSceneHandle() == sceneHandle,
			"cooked package did not preserve its start-scene handle");
		Require(assets.GetPhysics2DSettings() == cookedSettings.Physics2D,
			"tcpak v4 did not roundtrip the project Physics2D collision matrix");
		const TomCat::ManagedPackagePayload* mountedPayload =
			assets.GetCookedManagedPayload();
		Require(mountedPayload
			&& mountedPayload->NativeApiVersion == 1
			&& mountedPayload->ManagedApiVersion == 1
			&& mountedPayload->ScriptManifestVersion == 1
			&& mountedPayload->TargetFramework == "net10.0"
			&& mountedPayload->RuntimeIdentifier == "win-x64"
			&& mountedPayload->BuildID == buildID
			&& mountedPayload->AssemblySHA256.size() == 64
			&& mountedPayload->ScriptManifestJson == managedManifest
			&& mountedPayload->Assembly == managedAssembly
			&& mountedPayload->Pdb == managedPdb,
			"mounted tcpak did not expose the complete validated managed payload");
		std::vector<uint8_t> forbiddenSourceBytes;
		Require(!assets.ReadAssetBytes(scriptHandle, forbiddenSourceBytes),
			"tcpak v4 exposed a C# source asset entry");
		std::vector<uint8_t> cookedBytes;
		TomCat::AssetType cookedType = TomCat::AssetType::None;
		Require(assets.ReadAssetBytes(sceneHandle, cookedBytes, &cookedType)
			&& cookedType == TomCat::AssetType::Scene
			&& TomCat::SceneSerializer::ValidateCurrentFormat(cookedBytes, "CookedPhysicsRegression"),
			"cooked scene payload was not a complete schema-v10 Scene");

		auto loaded = TomCat::CreateRef<TomCat::Scene>();
		TomCat::SceneSerializer reader(loaded);
		Require(reader.Deserialize(sceneHandle),
			"Cooked Player scene path could not deserialize physics components");
		Require(loaded->GetPhysics2DSettings() == cookedSettings.Physics2D,
			"Cooked Player scene did not receive the mounted package collision matrix");
		TomCat::Entity loadedGround = loaded->FindEntityByUUID(groundUUID);
		TomCat::Entity loadedBall = loaded->FindEntityByUUID(ballUUID);
		Require(loadedGround && loadedBall
			&& loadedGround.HasComponent<TomCat::BoxCollider2D>()
			&& loadedGround.HasComponent<TomCat::DistanceJoint2D>()
			&& loadedGround.HasComponent<TomCat::CSharpScripts>()
			&& loadedBall.HasComponent<TomCat::CircleCollider2D>(),
			"Cooked Player scene omitted scripts or physics components");
		RequireSameCSharpScripts(
			loadedGround.GetComponent<TomCat::CSharpScripts>(),
			expectedScripts, true,
			"Cooked Player scene changed C# attachment data");
		Require(loadedGround.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Ground"
			&& loadedGround.GetComponent<TomCat::EntityMetadata>().Layer == 1
			&& loadedGround.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Collider2D
			&& loadedBall.GetComponent<TomCat::EntityMetadata>().GameplayTag == "Player"
			&& loadedBall.GetComponent<TomCat::EntityMetadata>().Layer == 2
			&& loadedBall.GetComponent<TomCat::EntityMetadata>().HierarchyIcon
				== TomCat::EntityIconMode::Sprite,
			"Cooked Player scene changed EntityMetadata");
		RequireSameBoxFields(loadedGround.GetComponent<TomCat::BoxCollider2D>(), expectedBox,
			"Cooked Player changed BoxCollider2D fields");
		RequireSameCircleFields(loadedBall.GetComponent<TomCat::CircleCollider2D>(), expectedCircle,
			"Cooked Player changed CircleCollider2D fields");
		RequireSameJointFields(loadedGround.GetComponent<TomCat::DistanceJoint2D>(), expectedJoint,
			"Cooked Player changed DistanceJoint2D fields");
		Require(loadedGround.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr
			&& loadedBall.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture == nullptr
			&& loadedGround.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint == nullptr,
			"cooked deserialization restored runtime physics pointers");

		int triggerEnters = 0;
		loaded->AddTriggerEnter2DListener(
			[&](const TomCat::TriggerEnter2D&) { ++triggerEnters; });
		auto managedRuntime = std::make_shared<ManagedRuntimeProbe>();
		TomCat::Scripting::ScriptEngine::Get().SetRuntime(managedRuntime);
		Require(loaded->OnRuntimeStart(),
			"Cooked Player scene could not start its managed runtime");
		Require(loadedGround.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture != nullptr
			&& loadedBall.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture != nullptr
			&& loadedGround.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint != nullptr,
			"Cooked Player runtime did not create Box/Circle fixtures and DistanceJoint");
		loaded->OnRuntimeStep();
		Require(triggerEnters == 0,
			"Cooked Player ignored the tcpak v4 project collision matrix");
		loaded->OnRuntimeStop();
		Require(loadedGround.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr
			&& loadedBall.GetComponent<TomCat::CircleCollider2D>().RuntimeFixture == nullptr
			&& loadedGround.GetComponent<TomCat::DistanceJoint2D>().RuntimeJoint == nullptr,
			"Cooked Player Stop did not clear runtime physics pointers");

		TomCat::Physics2DSettings allowedSettings = cookedSettings.Physics2D;
		allowedSettings.SetLayersCollide(1, 2, true);
		loaded->SetPhysics2DSettings(allowedSettings);
		Require(loaded->OnRuntimeStart(),
			"Cooked Player scene could not restart its managed runtime");
		loaded->OnRuntimeStep();
		Require(triggerEnters == 1,
			"enabling the project matrix did not admit the cooked trigger pair whose fixture masks match");
		loaded->OnRuntimeStop();
		TomCat::Scripting::ScriptEngine::Get().SetRuntime({});

		assets.Shutdown();
		const std::vector<uint8_t> validPackage = ReadBinaryFile(packagePath);
		const std::string packageText(validPackage.begin(), validPackage.end());
		Require(packageText.find(sourceMarker) == std::string::npos,
			"tcpak v4 contains C# source bytes");
		Require(packageText.find(csprojMarker) == std::string::npos
			&& packageText.find(objectMarker) == std::string::npos
			&& packageText.find(nestedLibraryMarker) == std::string::npos
			&& packageText.find("Assembly-CSharp.csproj") == std::string::npos
			&& packageText.find("ScriptAssets.json") == std::string::npos
			&& packageText.find("last-good.json") == std::string::npos
			&& packageText.find("Library/Script") == std::string::npos
			&& packageText.find(environment.Root.generic_string()) == std::string::npos,
			"tcpak v4 leaked authoring files or absolute project paths");
		Require(validPackage.size() >= 64,
			"cooked package is smaller than the tcpak v4 fixed header");
		Require(ReadLittleEndian32(validPackage, 8) == 4
			&& ReadLittleEndian32(validPackage, 12) == 64,
			"cooked package did not declare tcpak version 4 with its 64-byte header");

		const std::size_t managedIndex = FindManagedPackageIndexEntry(validPackage);
		const uint64_t managedEnvelopeOffset64 =
			ReadLittleEndian64(validPackage, managedIndex + 16);
		const uint64_t managedEnvelopeSize64 =
			ReadLittleEndian64(validPackage, managedIndex + 24);
		Require(managedEnvelopeOffset64 <= validPackage.size()
			&& managedEnvelopeSize64 <= validPackage.size() - managedEnvelopeOffset64,
			"managed envelope index range is outside its package");
		const std::size_t managedEnvelopeOffset =
			static_cast<std::size_t>(managedEnvelopeOffset64);

		std::vector<uint8_t> incompatibleApi = validPackage;
		WriteLittleEndian32(incompatibleApi, managedEnvelopeOffset + 12, 2);
		const std::filesystem::path incompatibleApiPath =
			environment.Root / "Build" / "IncompatibleManagedApi.tcpak";
		WriteBinaryFile(incompatibleApiPath, incompatibleApi);
		Require(!assets.MountCookedPackage(incompatibleApiPath),
			"tcpak loader accepted an incompatible managed NativeApi version");

		std::vector<uint8_t> missingManagedEntry = validPackage;
		WriteLittleEndian64(missingManagedEntry, managedIndex,
			(std::numeric_limits<uint64_t>::max)() - 1);
		WriteLittleEndian16(missingManagedEntry, managedIndex + 8,
			static_cast<uint16_t>(TomCat::AssetType::Other));
		WriteLittleEndian16(missingManagedEntry, managedIndex + 10, 0);
		WriteLittleEndian32(missingManagedEntry, managedIndex + 12, 0);
		const std::filesystem::path missingManagedEntryPath =
			environment.Root / "Build" / "MissingManagedEntry.tcpak";
		WriteBinaryFile(missingManagedEntryPath, missingManagedEntry);
		Require(!assets.MountCookedPackage(missingManagedEntryPath),
			"tcpak loader accepted CSharpScripts without a managed envelope entry");

		const uint32_t frameworkLength =
			ReadLittleEndian32(validPackage, managedEnvelopeOffset + 24);
		const uint32_t runtimeLength =
			ReadLittleEndian32(validPackage, managedEnvelopeOffset + 28);
		const uint32_t buildLength =
			ReadLittleEndian32(validPackage, managedEnvelopeOffset + 32);
		const uint32_t hashLength =
			ReadLittleEndian32(validPackage, managedEnvelopeOffset + 36);
		const uint64_t manifestLength =
			ReadLittleEndian64(validPackage, managedEnvelopeOffset + 40);
		const uint64_t assemblyLength =
			ReadLittleEndian64(validPackage, managedEnvelopeOffset + 48);
		const uint64_t assemblyOffset64 = managedEnvelopeOffset64 + 64
			+ frameworkLength + runtimeLength + buildLength + hashLength
			+ manifestLength;
		Require(assemblyLength > 2 && assemblyOffset64 <= validPackage.size()
			&& assemblyLength <= validPackage.size() - assemblyOffset64,
			"managed assembly range is outside its envelope");
		std::vector<uint8_t> badAssemblyHash = validPackage;
		badAssemblyHash[static_cast<std::size_t>(assemblyOffset64 + 2)] ^= 0x01;
		const std::filesystem::path badAssemblyHashPath =
			environment.Root / "Build" / "BadManagedHash.tcpak";
		WriteBinaryFile(badAssemblyHashPath, badAssemblyHash);
		Require(!assets.MountCookedPackage(badAssemblyHashPath),
			"tcpak loader accepted a managed assembly whose SHA-256 no longer matched");

		std::vector<uint8_t> legacyVersion = validPackage;
		WriteLittleEndian32(legacyVersion, 8, 3);
		const std::filesystem::path legacyPath = environment.Root / "Build" / "LegacyV3.tcpak";
		WriteBinaryFile(legacyPath, legacyVersion);
		Require(!assets.MountCookedPackage(legacyPath),
			"tcpak loader accepted obsolete package version 3");

		std::vector<uint8_t> asymmetric = validPackage;
		constexpr std::size_t matrixOffset = 32;
		const std::size_t rowOneOffset = matrixOffset + sizeof(uint16_t);
		const std::size_t rowTwoOffset = matrixOffset + 2 * sizeof(uint16_t);
		uint16_t rowOne = ReadLittleEndian16(asymmetric, rowOneOffset);
		const uint16_t rowTwo = ReadLittleEndian16(asymmetric, rowTwoOffset);
		Require((rowOne & (uint16_t(1) << 2)) == 0
			&& (rowTwo & (uint16_t(1) << 1)) == 0,
			"cooked matrix fixture did not preserve its disabled symmetric pair");
		rowOne |= uint16_t(1) << 2;
		WriteLittleEndian16(asymmetric, rowOneOffset, rowOne);
		const std::filesystem::path asymmetricPath =
			environment.Root / "Build" / "Asymmetric.tcpak";
		WriteBinaryFile(asymmetricPath, asymmetric);
		Require(!assets.MountCookedPackage(asymmetricPath),
			"tcpak loader accepted an asymmetric Physics2D collision matrix");

		std::vector<uint8_t> truncated = validPackage;
		truncated.resize(63);
		const std::filesystem::path truncatedPath =
			environment.Root / "Build" / "Truncated.tcpak";
		WriteBinaryFile(truncatedPath, truncated);
		Require(!assets.MountCookedPackage(truncatedPath),
			"tcpak loader accepted a truncated v4 fixed header");
	}

	void TestScriptFreeCookWithoutManagedPayload()
	{
		TemporaryCookedProject environment;
		TomCat::ProjectConfig config;
		config.Name = "Script Free Cook Regression";
		config.Template = "2D";
		config.AssetDirectory = "Assets";
		config.StartScene = "Main.tomcat";
		auto project = TomCat::Project::CreateNew(
			environment.Root / "Project.tcproj", config);
		Require(project != nullptr,
			"could not create a temporary script-free project");

		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.SetProject(project),
			"could not initialize AssetManager for script-free cook");
		auto scene = TomCat::CreateRef<TomCat::Scene>();
		scene->CreateEntity("No managed runtime required");
		const std::filesystem::path scenePath =
			project->GetAssetPath() / "Main.tomcat";
		TomCat::SceneSerializer writer(scene);
		Require(writer.Serialize(scenePath),
			"could not serialize the script-free start scene");
		const TomCat::AssetMetadata* sceneMetadata =
			assets.Registry().GetMetadata(scenePath);
		Require(sceneMetadata && sceneMetadata->Type == TomCat::AssetType::Scene,
			"script-free start scene was not imported");
		project->SetStartSceneHandle(sceneMetadata->Handle);
		Require(project->Save(), "could not save the script-free start-scene handle");

		assets.ClearManagedCookPayload();
		const std::filesystem::path packagePath =
			environment.Root / "Build" / "ScriptFree.tcpak";
		Require(assets.CookToPackage(packagePath),
			"script-free scene incorrectly required a managed payload");
		assets.Shutdown();
		Require(assets.MountCookedPackage(packagePath)
			&& assets.GetCookedManagedPayload() == nullptr,
			"script-free tcpak unexpectedly exposed a managed payload");
	}

	TomCat::Entity MakeMultiFixtureBody(TomCat::Scene& scene, const char* name,
		TomCat::Rigidbody2D::BodyType type)
	{
		TomCat::Entity entity = scene.CreateEntity(name);
		auto& body = entity.AddComponent<TomCat::Rigidbody2D>();
		body.Type = type;
		auto& box = entity.AddComponent<TomCat::BoxCollider2D>();
		box.Size = { 5.0f, 5.0f };
		entity.AddComponent<TomCat::CircleCollider2D>().Radius = 4.0f;
		return entity;
	}

	void TestCollisionPairDeduplication()
	{
		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		TomCat::Scene scene;
		TomCat::Entity entityA = MakeMultiFixtureBody(scene, "Static",
			TomCat::Rigidbody2D::BodyType::Static);
		TomCat::Entity entityB = MakeMultiFixtureBody(scene, "Dynamic",
			TomCat::Rigidbody2D::BodyType::Dynamic);
		AttachManagedProbe(entityA, 8105);
		AttachManagedProbe(entityB, 8106);
		int enters = 0;
		int exits = 0;
		uint64_t lastA = 0;
		uint64_t lastB = 0;
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D& event)
		{
			++enters;
			lastA = static_cast<uint64_t>(event.EntityA);
			lastB = static_cast<uint64_t>(event.EntityB);
		});
		scene.AddCollisionExit2DListener([&](const TomCat::CollisionExit2D&) { ++exits; });

		Require(scene.OnRuntimeStart(), "managed collision de-duplication probe did not start");
		scene.OnRuntimeStep();
		Require(enters == 1, "multiple fixtures emitted more than one entity-pair Enter");
		Require(runtime->PhysicsEvents.size() == 1
			&& runtime->PhysicsEvents.front().Kind == static_cast<uint32_t>(
				TomCat::Scripting::NativePhysicsEventKind::CollisionEnter),
			"managed runtime did not receive one de-duplicated CollisionEnter2D event");
		Require(lastA < lastB, "collision UUID pair was not canonicalized");
		Require(runtime->PhysicsEvents.front().EntityA.EntityId == lastA
			&& runtime->PhysicsEvents.front().EntityB.EntityId == lastB,
			"managed and native listeners received different canonical collision pairs");
		scene.OnRuntimeStep();
		Require(enters == 1 && exits == 0 && runtime->PhysicsEvents.size() == 1,
			"persistent contact emitted duplicate events");

		MoveRuntimeBody(entityB, { 100.0f, 0.0f });
		scene.OnRuntimeStep();
		Require(enters == 1 && exits == 1, "entity-pair Exit was not emitted exactly once");
		Require(runtime->PhysicsEvents.size() == 2
			&& runtime->PhysicsEvents.back().Kind == static_cast<uint32_t>(
				TomCat::Scripting::NativePhysicsEventKind::CollisionExit),
			"managed runtime did not receive one de-duplicated CollisionExit2D event");
		scene.OnRuntimeStop();
	}

	void TestDeletionDuringCollisionDispatch()
	{
		TomCat::Scene scene;
		TomCat::Entity entityA = AddCircleBody(scene, "Delete A",
			TomCat::Rigidbody2D::BodyType::Static, { 0.0f, 0.0f }, 2.0f);
		TomCat::Entity entityB = AddCircleBody(scene, "Delete B",
			TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 2.0f);

		int destructiveListenerCalls = 0;
		int laterListenerCalls = 0;
		int exitCalls = 0;
		TomCat::UUID deletedUUID(0);
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D& event)
		{
			++destructiveListenerCalls;
			deletedUUID = event.EntityB;
			TomCat::Entity victim = scene.FindEntityByUUID(event.EntityB);
			if (victim)
				scene.DestroyEntity(victim);
		});
		scene.AddCollisionEnter2DListener([&](const TomCat::CollisionEnter2D&)
		{
			++laterListenerCalls;
		});
		scene.AddCollisionExit2DListener([&](const TomCat::CollisionExit2D&) { ++exitCalls; });

		scene.OnRuntimeStart();
		scene.OnRuntimeStep();
		Require(destructiveListenerCalls == 1, "destructive collision listener did not run once");
		Require(laterListenerCalls == 0, "listener received an event after an entity was deleted");
		Require(static_cast<uint64_t>(deletedUUID) != 0 && !scene.FindEntityByUUID(deletedUUID),
			"collision callback did not delete its target");
		scene.OnRuntimeStep();
		Require(exitCalls == 0, "entity deletion leaked a stale Exit event");
		scene.OnRuntimeStop();
	}

	void TestManagedMutationDuringCollisionDispatch()
	{
		auto runMutation = [](ManagedRuntimeProbe::PhysicsMutation action)
		{
			auto runtime = std::make_shared<ManagedRuntimeProbe>();
			runtime->Mutation = action;
			ScriptRuntimeOverride runtimeOverride(runtime);
			TomCat::Scene scene;
			TomCat::Entity scripted = AddCircleBody(scene, "Mutating script",
				TomCat::Rigidbody2D::BodyType::Static, { 0.0f, 0.0f }, 2.0f);
			const TomCat::UUID scriptedUUID = scripted.GetUUID();
			AttachManagedProbe(scripted, 8107);
			AddCircleBody(scene, "Mutation contact",
				TomCat::Rigidbody2D::BodyType::Dynamic, { 0.0f, 0.0f }, 2.0f);

			int laterListenerCalls = 0;
			scene.AddCollisionEnter2DListener(
				[&](const TomCat::CollisionEnter2D&) { ++laterListenerCalls; });
			Require(scene.OnRuntimeStart(),
				"managed collision-mutation probe did not start");
			scene.OnRuntimeStep();

			Require(runtime->PhysicsEventCount == 1
				&& runtime->MutationAttempted && runtime->MutationQueued,
				"managed physics callback did not queue its deferred mutation");

			TomCat::Entity survivingScripted = scene.FindEntityByUUID(scriptedUUID);
			if (action == ManagedRuntimeProbe::PhysicsMutation::DestroyEntity)
			{
				Require(!survivingScripted,
					"DestroyEntity requested by a managed collision callback was not flushed");
				Require(laterListenerCalls == 0,
					"listener dispatch continued after managed code deleted a participant");
				Require(runtime->DestroyedAttachmentCount == 1,
					"managed attachment was not destroyed with its Entity");
			}
			else
			{
				Require(survivingScripted,
					"managed component mutation unexpectedly deleted its Entity");
				Require(laterListenerCalls == 1,
					"component-only managed mutation incorrectly cancelled the Scene event");
				Require(!survivingScripted.HasComponent<TomCat::Rigidbody2D>(),
					"managed Rigidbody2D removal was not committed at callback return");
			}

			scene.OnRuntimeStop();
		};

		runMutation(ManagedRuntimeProbe::PhysicsMutation::RemoveRigidbody2D);
		runMutation(ManagedRuntimeProbe::PhysicsMutation::DestroyEntity);
	}

	void TestManagedScriptLifecycleBackend()
	{
		for (const int frameRate : { 30, 60, 144 })
		{
			auto runtime = std::make_shared<ManagedRuntimeProbe>();
			TomCat::Scripting::ScriptEngine::Get().SetRuntime(runtime);
			TomCat::Scene scene;
			TomCat::Entity scripted = scene.CreateEntity("Managed lifecycle probe");
			TomCat::CSharpScriptEntry entry;
			entry.AttachmentID = TomCat::UUID();
			entry.ScriptAsset = TomCat::AssetHandle(9001);
			entry.LastKnownClassName = "Game.LifecycleProbe";
			scripted.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(entry);

			Require(scene.OnRuntimeStart(),
				"managed fake backend could not start a scripted scene");
			Require(runtime->Calls.size() >= 4
				&& runtime->Calls[0] == "CreateSceneRuntime"
				&& runtime->Calls[1] == "InstantiateAll"
				&& runtime->Calls[2] == "ApplySerializedFields"
				&& runtime->Calls[3] == "InvokeCreateAll",
				"managed scene startup callbacks were not ordered transactionally");
			Require(runtime->Attachments.size() == 1
				&& runtime->Attachments[0].AttachmentId
					== static_cast<uint64_t>(entry.AttachmentID)
				&& runtime->Attachments[0].Entity.SceneSessionId
					== runtime->LastSceneSession,
				"managed attachment identity was not bound to the active scene session");
			Require(!TomCat::Scripting::ScriptEngine::Get().QueueAddComponent(
				runtime->Attachments[0].Entity,
				static_cast<TomCat::Scripting::NativeComponentType>(999))
				&& !TomCat::Scripting::ScriptEngine::Get().QueueRemoveComponent(
					runtime->Attachments[0].Entity,
					static_cast<TomCat::Scripting::NativeComponentType>(999)),
				"invalid managed component types entered the deferred command queue");

			for (int frame = 0; frame < frameRate; ++frame)
				scene.OnUpdateRuntime(TomCat::Timestep(1.0f
					/ static_cast<float>(frameRate)));
			Require(runtime->UpdateCount == static_cast<uint32_t>(frameRate),
				"managed OnUpdate was not called once per display frame");
			Require(runtime->FixedUpdateCount == 60,
				"managed OnFixedUpdate was not fixed at 60 Hz");
			const uint32_t updatesBeforeStep = runtime->UpdateCount;
			scene.OnRuntimeStep();
			Require(runtime->FixedUpdateCount == 61
				&& runtime->UpdateCount == updatesBeforeStep,
				"managed Step did not perform exactly one fixed update and zero display updates");
			scene.OnRuntimeStop();
			Require(!runtime->Calls.empty() && runtime->Calls.back() == "DestroyAll",
				"managed Stop did not destroy the scene runtime");
			TomCat::Scripting::ScriptEngine::Get().SetRuntime({});
		}

		{
			auto runtime = std::make_shared<ManagedRuntimeProbe>();
			runtime->InvokeCreateStatus = TomCat::Scripting::ScriptStatus::ManagedException;
			TomCat::Scripting::ScriptEngine::Get().SetRuntime(runtime);
			TomCat::Scene scene;
			TomCat::Entity entity = scene.CreateEntity("Managed start failure");
			entity.AddComponent<TomCat::BoxCollider2D>();
			TomCat::CSharpScriptEntry entry;
			entry.AttachmentID = TomCat::UUID();
			entry.ScriptAsset = TomCat::AssetHandle(9002);
			entity.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(entry);
			Require(!scene.OnRuntimeStart(),
				"managed startup failure incorrectly entered Play");
			Require(entity.GetComponent<TomCat::BoxCollider2D>().RuntimeFixture == nullptr
				&& !runtime->Active,
				"managed startup failure did not roll back scripts and physics");
			TomCat::Scripting::ScriptEngine::Get().SetRuntime({});
		}

		{
			auto runtime = std::make_shared<ManagedRuntimeProbe>();
			runtime->UnloadSucceeds = false;
			TomCat::Scripting::ScriptEngine::Get().SetRuntime(runtime);
			TomCat::Scene scene;
			TomCat::CSharpScriptEntry entry;
			entry.AttachmentID = TomCat::UUID();
			entry.ScriptAsset = TomCat::AssetHandle(9003);
			scene.CreateEntity("Managed unload failure")
				.AddComponent<TomCat::CSharpScripts>().Scripts.push_back(entry);
			Require(scene.OnRuntimeStart(),
				"managed unload-failure probe could not start");
			scene.OnRuntimeStop();
			Require(runtime->UnloadFailureCount == 1
				&& !runtime->LastUnloadFailure.empty(),
				"ScriptEngine did not issue the terminal unload-failure notification");
			TomCat::Scripting::ScriptEngine::Get().SetRuntime({});
		}
	}

}

int main()
{
	TomCat::Log::Init();
	int failures = 0;
	auto run = [&](const char* name, auto&& test)
	{
		try
		{
			test();
			std::cout << "PASS " << name << '\n';
		}
		catch (const std::exception& exception)
		{
			++failures;
			std::cerr << "FAIL " << name << ": " << exception.what() << '\n';
		}
	};

	run("project settings persistence and validation", TestProjectSettingsPersistenceAndValidation);
	run("explicit Player dotnet root is exclusive", TestExplicitDotNetRootIsExclusive);
	run("fixed accumulator and exact Step", TestFixedAccumulatorAndStep);
	run("managed Transform world setters preserve hierarchy",
		TestManagedTransformSettersRespectHierarchy);
	run("30/60/144Hz one- and ten-second consistency", TestFrameRateIndependentPhysics);
	run("Pause render path and exact single-step", TestPauseRenderPathAndSingleStep);
	run("collider-only static runtime body", TestColliderOnlyStaticBody);
	run("CircleCollider2D non-uniform fixture", TestCircleNonUniformScaleFixture);
	run("authoring/runtime collider outline parity", TestAuthoringAndRuntimeOutlinesMatch);
	run("Trigger events and managed callbacks", TestTriggerAndManagedCallbacks);
	run("layer/mask filtering and runtime rebuild", TestCollisionFilteringAndRuntimeRebuild);
	run("project matrix and fixture filters are independent", TestProjectMatrixAndFixtureFilters);
	run("entity-layer raycast/AABB query and motion APIs", TestQueriesAndMotionAPI);
	run("DistanceJoint2D runtime creation and rebuild", TestDistanceJoint);
	run("schema v10 scripts/metadata save/load/copy/duplicate and v9 migration",
		TestSchemaV10PersistenceAndCopies);
	run("Cooked Player v4 physics roundtrip and validation", TestCookedPlayerPhysicsRoundtrip);
	run("script-free tcpak v4 needs no managed payload", TestScriptFreeCookWithoutManagedPayload);
	run("collision Enter/Exit entity-pair de-duplication", TestCollisionPairDeduplication);
	run("collision callback deletion safety", TestDeletionDuringCollisionDispatch);
	run("managed collision mutation safety", TestManagedMutationDuringCollisionDispatch);
	run("managed lifecycle backend, timing, rollback, and unload failure",
		TestManagedScriptLifecycleBackend);
	return failures == 0 ? 0 : 1;
}
