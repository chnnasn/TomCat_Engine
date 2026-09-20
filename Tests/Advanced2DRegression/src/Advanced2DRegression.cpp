#include "TomCat/Core/Log.h"
#include "../../../Editor/TomCatInut/src/EditorPropertyTransaction.h"
#include "TomCat/Scene/Advanced2D.h"
#include "TomCat/Scene/ComponentRegistry.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/Serialization/AssetReferenceVisitor.h"
#include "TomCat/Scene/Serialization/SceneArchiveCodec.h"

#include <yaml-cpp/yaml.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	bool Near(float left, float right, float epsilon = 1.0e-5f)
	{
		return std::abs(left - right) <= epsilon;
	}

	bool Near(const glm::vec2& left, const glm::vec2& right,
		float epsilon = 1.0e-5f)
	{
		return Near(left.x, right.x, epsilon)
			&& Near(left.y, right.y, epsilon);
	}

	bool Near(const glm::vec4& left, const glm::vec4& right,
		float epsilon = 1.0e-5f)
	{
		return Near(left.x, right.x, epsilon)
			&& Near(left.y, right.y, epsilon)
			&& Near(left.z, right.z, epsilon)
			&& Near(left.w, right.w, epsilon);
	}

    void TestEkitSceneLifetime()
    {
        auto scene = TomCat::CreateRef<TomCat::Scene>();
        auto first = scene->CreateEntity("First");
        const int pick = static_cast<int>(first);
        const auto oldHandle = static_cast<ekit::Entity>(first);
        auto* tag = &first.GetComponent<TomCat::Tag>();
        auto* transform = &first.GetComponent<TomCat::Transform>();
        for (int i = 0; i < 1024; ++i)
            scene->CreateEntity("Growth");
        Require(&first.GetComponent<TomCat::Tag>() == tag,
            "Sparse storage growth invalidated an owning component reference");
        Require(&first.GetComponent<TomCat::Transform>() == transform,
            "Sparse storage growth invalidated a transform reference");
        Require(scene->FindEntityByPickingID(pick) == first,
            "Picking did not preserve the complete entity handle");
        scene->DestroyEntity(first);
        auto replacement = scene->CreateEntity("Replacement");
        Require(static_cast<ekit::Entity>(replacement).GetIndex() == oldHandle.GetIndex(),
            "Test did not exercise entity slot reuse");
        Require(!first && !scene->FindEntityByPickingID(pick),
            "Destroyed handle or stale picking ID aliased a recycled entity");
        Require(static_cast<int>(replacement) != pick,
            "Picking IDs must not be recycled within a scene");
        Require(!scene->FindEntityByPickingID(-1) && !scene->FindEntityByPickingID(INT_MAX),
            "Invalid picking ID resolved to an entity");

        TomCat::SceneWorld world;
        world.RegisterSparseComponent<TomCat::Tag>();
        world.RegisterSparseComponent<TomCat::Transform>();
        auto a = world.Create(), b = world.Create();
        world.Add<TomCat::Tag>(a, "A");
        world.Add<TomCat::Tag>(b, "B");
        world.Add<TomCat::Transform>(b);
        size_t count = 0;
        const auto& readOnly = world;
        for (auto entity : readOnly.View<TomCat::Tag, TomCat::Transform>()) {
            Require(entity == b, "Multi-component range included a nonmatching entity");
            ++count;
        }
        Require(count == 1, "Sparse intersection range skipped a matching entity");
        TomCat::SceneWorld adopted = std::move(world);
        Require(adopted.IsAlive(b) && adopted.Get<TomCat::Tag>(b)._Tag == "B",
            "Adopting a validated world lost components");
    }

    void TestMultiSelectionPropertyTransaction()
    {
        auto scene=TomCat::CreateRef<TomCat::Scene>();
        auto first=scene->CreateEntity("First"), second=scene->CreateEntity("Second");
        first.AddComponent<TomCat::HealthComponent>().Current=20;
        second.AddComponent<TomCat::HealthComponent>().Current=90;
        const auto* descriptor=TomCat::ComponentRegistry::Get().Find(TomCat::UUID(TomCat::ComponentIds::Health));
        Require(descriptor!=nullptr,"Missing Health descriptor");
        const TomCat::PropertyDescriptor* maximum=nullptr;
        for(const auto& property:descriptor->Properties) if(property.StableName=="Maximum") maximum=&property;
        Require(maximum!=nullptr,"Missing Maximum property");
        std::string error;
        Require(!TomCat::EditorProperties::SetAll(*maximum,{first,second},int32_t(50),error),"Batch must reject invalid second target");
        Require(first.GetComponent<TomCat::HealthComponent>().Maximum==100 && second.GetComponent<TomCat::HealthComponent>().Maximum==100,"Rejected batch left a partial edit");
        Require(!error.empty(),"Rejected edit must retain validation reason");
        Require(TomCat::EditorProperties::SetAll(*maximum,{first,second},int32_t(150),error),"Valid batch was rejected");
        Require(first.GetComponent<TomCat::HealthComponent>().Maximum==150 && second.GetComponent<TomCat::HealthComponent>().Maximum==150,"Valid batch did not edit both targets");
        Require(error.empty(),"Successful batch retained stale error");
    }

	void TestTilemapEditingAndTransform()
	{
		TomCat::Tilemap2D tilemap;
		TomCat::TilemapCell overwritten;
		overwritten.Coordinate = { 2, 1 };
		overwritten.SpriteHandle = TomCat::AssetHandle(1001);
		TomCat::TilemapCell first;
		first.Coordinate = { -1, 0 };
		first.SpriteHandle = TomCat::AssetHandle(1002);
		TomCat::TilemapCell winner = overwritten;
		winner.SpriteHandle = TomCat::AssetHandle(1003);
		winner.RotationQuarterTurns = -1;
		tilemap.Cells = { overwritten, first, winner };

		TomCat::Tilemap2DRuntime::Normalize(tilemap);
		Require(tilemap.Cells.size() == 2,
			"Tilemap normalization did not remove duplicate coordinates");
		Require(tilemap.Cells[0].Coordinate == glm::ivec2(-1, 0)
			&& tilemap.Cells[1].Coordinate == glm::ivec2(2, 1),
			"Tilemap normalization did not produce row-major order");
		Require(tilemap.Cells[1].SpriteHandle == TomCat::AssetHandle(1003)
			&& tilemap.Cells[1].RotationQuarterTurns == 3,
			"Tilemap normalization did not preserve the last painted cell");
		Require(TomCat::Tilemap2DRuntime::FindCell(tilemap, { 2, 1 })
			== &tilemap.Cells[1], "Tilemap lookup missed a normalized cell");

		TomCat::TilemapCell replacement = tilemap.Cells[1];
		replacement.FlipY = true;
		replacement.RotationQuarterTurns = 5;
		Require(TomCat::Tilemap2DRuntime::SetCell(tilemap, replacement),
			"Tilemap replacement did not report a mutation");
		Require(!TomCat::Tilemap2DRuntime::SetCell(tilemap, replacement),
			"Tilemap accepted an identical replacement as a mutation");
		Require(TomCat::Tilemap2DRuntime::FindCell(tilemap, { 2, 1 })
			->RotationQuarterTurns == 1,
			"Tilemap replacement did not canonicalize rotation");

		TomCat::TilemapCell inserted;
		inserted.Coordinate = { 0, -2 };
		inserted.SpriteHandle = TomCat::AssetHandle(1004);
		Require(TomCat::Tilemap2DRuntime::SetCell(tilemap, inserted)
			&& tilemap.Cells.front().Coordinate == glm::ivec2(0, -2),
			"Tilemap insertion did not preserve row-major order");
		Require(TomCat::Tilemap2DRuntime::EraseCell(tilemap, { -1, 0 })
			&& !TomCat::Tilemap2DRuntime::EraseCell(tilemap, { -1, 0 }),
			"Tilemap erase did not distinguish present and missing cells");

		TomCat::Tilemap2D transformTilemap;
		transformTilemap.CellSize = { 2.0f, 3.0f };
		transformTilemap.CellGap = { 0.5f, 0.25f };
		TomCat::TilemapCell transformed;
		transformed.Coordinate = { 2, -1 };
		transformed.FlipX = true;
		transformed.RotationQuarterTurns = 1;
		const glm::mat4 matrix = TomCat::Tilemap2DRuntime::GetCellTransform(
			transformTilemap, transformed);
		const glm::vec4 origin = matrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		const glm::vec4 xAxis = matrix * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
		Require(Near(origin, glm::vec4(5.0f, -3.25f, 0.0f, 1.0f))
			&& Near(xAxis, glm::vec4(5.0f, -5.25f, 0.0f, 1.0f)),
			"Tilemap cell transform did not apply stride, rotation and flip");

		TomCat::Grid2D grid;
		grid.CellSize = { 4.0f, 5.0f };
		grid.CellGap = { 1.0f, 2.0f };
		const glm::mat4 gridMatrix = TomCat::Tilemap2DRuntime::GetCellTransform(
			transformTilemap, transformed, &grid);
		const glm::vec4 gridOrigin = gridMatrix
			* glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		Require(Near(gridOrigin, glm::vec4(10.0f, -7.0f, 0.0f, 1.0f)),
			"Parent Grid2D did not override legacy Tilemap2D cell layout");
		grid.Layout = TomCat::GridCellLayout2D::Isometric;
		const glm::vec4 isometricOrigin =
			TomCat::Tilemap2DRuntime::GetCellTransform(
				transformTilemap, transformed, &grid)
			* glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		Require(Near(isometricOrigin, glm::vec4(7.5f, 3.5f, 0.0f, 1.0f)),
			"Grid2D isometric layout transform is incorrect");

		using TomCat::Tilemap2DRuntime::GetCellRenderOrder;
		const glm::ivec2 lowerLeft{ -2, -1 };
		const glm::ivec2 lowerRight{ 3, -1 };
		const glm::ivec2 upperLeft{ -2, 4 };
		Require(GetCellRenderOrder(lowerLeft, TomCat::TilemapSortOrder2D::BottomLeft)
			< GetCellRenderOrder(lowerRight, TomCat::TilemapSortOrder2D::BottomLeft)
			&& GetCellRenderOrder(lowerRight, TomCat::TilemapSortOrder2D::BottomLeft)
			< GetCellRenderOrder(upperLeft, TomCat::TilemapSortOrder2D::BottomLeft),
			"Bottom-left tile render order is not row-major ascending");
		Require(GetCellRenderOrder(lowerRight, TomCat::TilemapSortOrder2D::BottomRight)
			< GetCellRenderOrder(lowerLeft, TomCat::TilemapSortOrder2D::BottomRight),
			"Bottom-right tile render order did not reverse X");
		Require(GetCellRenderOrder(upperLeft, TomCat::TilemapSortOrder2D::TopLeft)
			< GetCellRenderOrder(lowerLeft, TomCat::TilemapSortOrder2D::TopLeft),
			"Top-left tile render order did not reverse Y");
	}

	TomCat::ParticleSystem2D MakeParticleFixture()
	{
		TomCat::ParticleSystem2D system;
		system.PlayOnStart = false;
		system.Loop = true;
		system.Duration = 2.0f;
		system.EmissionRate = 12.0f;
		system.MaxParticles = 64;
		system.StartLifetime = 3.0f;
		system.StartSpeed = 4.0f;
		system.StartSize = 0.8f;
		system.EndSize = 0.2f;
		system.GravityScale = 0.35f;
		system.Direction = { 1.0f, 0.25f };
		system.SpreadDegrees = 70.0f;
		system.StartColor = { 1.0f, 0.8f, 0.4f, 1.0f };
		system.EndColor = { 0.2f, 0.4f, 1.0f, 0.0f };
		system.Seed = 0x12345678u;
		return system;
	}

	void RequireSameParticleState(const TomCat::ParticleSystem2D& left,
		const TomCat::ParticleSystem2D& right)
	{
		Require(left.RuntimePlaying == right.RuntimePlaying
			&& left.RuntimeInitialized == right.RuntimeInitialized
			&& Near(left.RuntimeTime, right.RuntimeTime)
			&& Near(left.RuntimeEmissionAccumulator,
				right.RuntimeEmissionAccumulator)
			&& left.RuntimeRandomState == right.RuntimeRandomState
			&& left.RuntimeParticles.size() == right.RuntimeParticles.size(),
			"Particle replay diverged in system runtime state");
		for (size_t index = 0; index < left.RuntimeParticles.size(); ++index)
		{
			const TomCat::Particle2D& a = left.RuntimeParticles[index];
			const TomCat::Particle2D& b = right.RuntimeParticles[index];
			Require(Near(a.Position, b.Position) && Near(a.Velocity, b.Velocity)
				&& Near(a.Age, b.Age) && Near(a.Lifetime, b.Lifetime)
				&& Near(a.StartSize, b.StartSize) && Near(a.EndSize, b.EndSize),
				"Particle replay diverged in particle state");
		}
	}

	void TestDeterministicParticleRuntime()
	{
		TomCat::ParticleSystem2D forwardProbe;
		Require(Near(forwardProbe.Direction, glm::vec2(1.0f, 0.0f)),
			"ParticleSystem2D default forward stopped using local +X");
		forwardProbe.PlayOnStart = false;
		forwardProbe.EmissionRate = 1.0f;
		forwardProbe.MaxParticles = 1;
		forwardProbe.StartSpeed = 3.0f;
		forwardProbe.SpreadDegrees = 0.0f;
		forwardProbe.GravityScale = 0.0f;
		forwardProbe.Direction = glm::vec2(0.0f);
		TomCat::ParticleSystem2DRuntime::Play(forwardProbe);
		TomCat::ParticleSystem2DRuntime::Update(forwardProbe, 1.0f);
		Require(forwardProbe.RuntimeParticles.size() == 1
			&& Near(forwardProbe.RuntimeParticles.front().Velocity,
				glm::vec2(3.0f, 0.0f)),
			"ParticleSystem2D zero direction did not fall back to local +X");

		TomCat::ParticleSystem2D system = MakeParticleFixture();
		system.RuntimePlaying = true;
		system.RuntimeInitialized = true;
		system.RuntimeTime = 9.0f;
		system.RuntimeEmissionAccumulator = 0.5f;
		system.RuntimeRandomState = 99;
		system.RuntimeParticles.push_back({});
		TomCat::ParticleSystem2DRuntime::Reset(system);
		Require(!system.RuntimePlaying && !system.RuntimeInitialized
			&& Near(system.RuntimeTime, 0.0f)
			&& Near(system.RuntimeEmissionAccumulator, 0.0f)
			&& system.RuntimeRandomState == system.Seed
			&& system.RuntimeParticles.empty(),
			"Particle reset left transient runtime state behind");

		constexpr std::array<float, 4> steps{ 0.05f, 0.12f, 0.07f, 0.2f };
		TomCat::ParticleSystem2DRuntime::Play(system);
		for (float step : steps)
			TomCat::ParticleSystem2DRuntime::Update(system, step);
		Require(!system.RuntimeParticles.empty(),
			"Particle fixture did not emit particles");
		const TomCat::ParticleSystem2D firstRun = system;

		TomCat::ParticleSystem2DRuntime::Reset(system);
		TomCat::ParticleSystem2DRuntime::Play(system);
		for (float step : steps)
			TomCat::ParticleSystem2DRuntime::Update(system, step);
		RequireSameParticleState(firstRun, system);

		const TomCat::Particle2D sampled{
			{}, {}, 1.5f, 3.0f, 0.8f, 0.2f };
		Require(Near(TomCat::ParticleSystem2DRuntime::EvaluateSize(sampled), 0.5f)
			&& Near(TomCat::ParticleSystem2DRuntime::EvaluateColor(system, sampled),
				glm::vec4(0.6f, 0.6f, 0.7f, 0.5f)),
			"Particle lifetime interpolation is incorrect");

		TomCat::ParticleSystem2D oneShot = MakeParticleFixture();
		oneShot.Loop = false;
		// Binary-exact values keep the boundary assertion independent of decimal
		// floating point rounding while still forcing the final update past Duration.
		oneShot.Duration = 0.375f;
		oneShot.EmissionRate = 16.0f;
		oneShot.StartLifetime = 10.0f;
		TomCat::ParticleSystem2DRuntime::Play(oneShot);
		TomCat::ParticleSystem2DRuntime::Update(oneShot, 0.25f);
		Require(oneShot.RuntimePlaying && oneShot.RuntimeParticles.size() == 4,
			"Non-looping particles stopped before Duration");
		TomCat::ParticleSystem2DRuntime::Update(oneShot, 0.25f);
		Require(!oneShot.RuntimePlaying && oneShot.RuntimeParticles.size() == 6,
			"Non-looping particles did not clamp emission to Duration");
		const size_t stoppedCount = oneShot.RuntimeParticles.size();
		TomCat::ParticleSystem2DRuntime::Update(oneShot, 0.5f);
		Require(!oneShot.RuntimePlaying
			&& oneShot.RuntimeParticles.size() == stoppedCount,
			"Non-looping particles continued emitting after Duration");

		TomCat::ParticleSystem2D zeroSeed;
		zeroSeed.Seed = 0;
		TomCat::ParticleSystem2DRuntime::Reset(zeroSeed);
		Require(zeroSeed.RuntimeRandomState == 1,
			"Particle reset did not canonicalize a zero seed");
	}

	void TestLightAttenuation()
	{
		using TomCat::Light2DRuntime::EvaluateAttenuation;
		Require(Near(EvaluateAttenuation(0.0f, 10.0f, 2.0f), 1.0f)
			&& Near(EvaluateAttenuation(5.0f, 10.0f, 1.0f), 0.5f)
			&& Near(EvaluateAttenuation(5.0f, 10.0f, 2.0f), 0.25f)
			&& Near(EvaluateAttenuation(10.0f, 10.0f, 1.0f), 0.0f)
			&& Near(EvaluateAttenuation(15.0f, 10.0f, 1.0f), 0.0f)
			&& Near(EvaluateAttenuation(-2.0f, 10.0f, 1.0f), 1.0f)
			&& Near(EvaluateAttenuation(1.0f, 0.0f, 1.0f), 0.0f)
			&& Near(EvaluateAttenuation(
				(std::numeric_limits<float>::quiet_NaN)(), 10.0f, 1.0f), 0.0f),
			"Light2D attenuation does not match the authored radius/falloff curve");
	}

	const TomCat::ComponentDescriptor& RequireDescriptor(uint64_t typeId,
		std::string_view stableName, size_t propertyCount)
	{
		const TomCat::ComponentDescriptor* descriptor =
			TomCat::ComponentRegistry::Get().Find(TomCat::UUID(typeId));
		Require(descriptor != nullptr && descriptor->StableName == stableName
			&& descriptor->Properties.size() == propertyCount
			&& descriptor->Add && descriptor->Remove && descriptor->Copy
			&& descriptor->Encode && descriptor->Decode
			&& descriptor->InspectorVisible && descriptor->UseGenericInspector
			&& descriptor->AddableInInspector,
			"Advanced 2D component descriptor is incomplete");
		return *descriptor;
	}

	void TestRegistrySceneRoundTripAndReferences()
	{
		RequireDescriptor(TomCat::ComponentIds::Tilemap2D,
			"TomCat.Tilemap2D", 5);
		RequireDescriptor(TomCat::ComponentIds::ParticleSystem2D,
			"TomCat.ParticleSystem2D", 19);
		RequireDescriptor(TomCat::ComponentIds::Light2D,
			"TomCat.Light2D", 6);
		RequireDescriptor(TomCat::ComponentIds::Grid2D,
			"TomCat.Grid2D", 4);
		RequireDescriptor(TomCat::ComponentIds::TilemapRenderer2D,
			"TomCat.TilemapRenderer2D", 7);

		auto scene = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity entity = scene->CreateEntityWithUUID(
			TomCat::UUID(0x2026000000000001ULL), "Advanced 2D Fixture");
		std::string error;
		TomCat::ComponentRegistry& registry = TomCat::ComponentRegistry::Get();
		Require(registry.Add(entity, TomCat::UUID(TomCat::ComponentIds::Tilemap2D),
			error) && registry.Add(entity,
				TomCat::UUID(TomCat::ComponentIds::ParticleSystem2D), error)
			&& registry.Add(entity, TomCat::UUID(TomCat::ComponentIds::Light2D),
				error)
			&& registry.Add(entity, TomCat::UUID(TomCat::ComponentIds::Grid2D), error)
			&& registry.Add(entity,
				TomCat::UUID(TomCat::ComponentIds::TilemapRenderer2D), error),
			"Advanced 2D descriptors could not add their components");
		Require(registry.Has(entity, TomCat::UUID(TomCat::ComponentIds::Tilemap2D))
			&& registry.Has(entity,
				TomCat::UUID(TomCat::ComponentIds::ParticleSystem2D))
			&& registry.Has(entity, TomCat::UUID(TomCat::ComponentIds::Light2D))
			&& registry.Has(entity, TomCat::UUID(TomCat::ComponentIds::Grid2D))
			&& registry.Has(entity,
				TomCat::UUID(TomCat::ComponentIds::TilemapRenderer2D)),
			"Advanced 2D registry membership is incorrect");

		auto& grid = entity.GetComponent<TomCat::Grid2D>();
		grid.CellSize = { 3.0f, 1.5f };
		grid.CellGap = { 0.25f, 0.5f };
		grid.Layout = TomCat::GridCellLayout2D::Hexagon;
		grid.Swizzle = TomCat::GridCellSwizzle2D::YXZ;
		auto& tileRenderer = entity.GetComponent<TomCat::TilemapRenderer2D>();
		tileRenderer.Enabled = false;
		tileRenderer.SortOrder = TomCat::TilemapSortOrder2D::TopRight;
		tileRenderer.Mode = TomCat::TilemapRendererMode2D::Individual;
		tileRenderer.DetectChunkCulling = TomCat::TilemapChunkCulling2D::Manual;
		tileRenderer.SortingLayer = 8;
		tileRenderer.OrderInLayer = -4;
		tileRenderer.MaterialHandle = TomCat::AssetHandle(6001);

		auto& tilemap = entity.GetComponent<TomCat::Tilemap2D>();
		tilemap.Enabled = false;
		tilemap.CellSize = { 1.25f, 2.5f };
		tilemap.CellGap = { 0.25f, -0.5f };
		tilemap.SortingLayer = -3;
		tilemap.OrderInLayer = 11;
		TomCat::TilemapCell cellA;
		cellA.Coordinate = { 4, -2 };
		cellA.SpriteHandle = TomCat::AssetHandle(4001);
		cellA.Tint = { 0.1f, 0.2f, 0.3f, 0.4f };
		cellA.FlipX = true;
		cellA.RotationQuarterTurns = 3;
		TomCat::TilemapCell cellB;
		cellB.Coordinate = { -1, 3 };
		cellB.SpriteHandle = TomCat::AssetHandle(4002);
		cellB.Tint = { 0.9f, 0.8f, 0.7f, 0.6f };
		cellB.FlipY = true;
		cellB.RotationQuarterTurns = 2;
		tilemap.Cells = { cellB, cellA };

		auto& particles = entity.GetComponent<TomCat::ParticleSystem2D>();
		particles.Enabled = false;
		particles.PlayOnStart = false;
		particles.Loop = false;
		particles.Duration = 2.75f;
		particles.EmissionRate = 17.5f;
		particles.MaxParticles = 37;
		particles.StartLifetime = 3.25f;
		particles.StartSpeed = 2.5f;
		particles.StartSize = 0.8f;
		particles.EndSize = 0.1f;
		particles.GravityScale = 0.4f;
		particles.Direction = { 0.6f, 0.8f };
		particles.SpreadDegrees = 42.0f;
		particles.StartColor = { 0.9f, 0.7f, 0.5f, 1.0f };
		particles.EndColor = { 0.2f, 0.3f, 0.4f, 0.1f };
		particles.SpriteHandle = TomCat::AssetHandle(5001);
		particles.SortingLayer = -2;
		particles.OrderInLayer = 9;
		particles.Seed = 991;
		particles.RuntimePlaying = true;
		particles.RuntimeInitialized = true;
		particles.RuntimeTime = 8.0f;
		particles.RuntimeParticles.push_back({});

		auto& light = entity.GetComponent<TomCat::Light2D>();
		light.Enabled = false;
		light.Type = TomCat::Light2DType::Global;
		light.Color = { 0.3f, 0.5f, 0.7f, 0.9f };
		light.Intensity = 2.25f;
		light.Radius = 13.5f;
		light.Falloff = 1.75f;

		std::string document;
		Require(TomCat::SceneArchiveCodec::Encode(scene, document, error),
			"Advanced 2D Scene 11 serialization failed");
		Require(document.find("TomCat.Tilemap2D") != std::string::npos
			&& document.find("TomCat.ParticleSystem2D") != std::string::npos
			&& document.find("TomCat.Light2D") != std::string::npos
			&& document.find("TomCat.Grid2D") != std::string::npos
			&& document.find("TomCat.TilemapRenderer2D") != std::string::npos,
			"Advanced 2D Scene 11 component records are missing");

		const YAML::Node root = YAML::Load(document);
		bool sawTileA = false;
		bool sawTileB = false;
		bool sawParticle = false;
		bool sawMaterial = false;
		Require(TomCat::AssetReferenceVisitor::VisitScene(root,
			[&](const TomCat::SerializedAssetReference& reference)
			{
				const uint64_t handle = static_cast<uint64_t>(reference.Handle);
				sawTileA = sawTileA || (reference.Kind
					== TomCat::SerializedAssetReferenceKind::TilemapCell
					&& handle == 4001
					&& reference.ExpectedType == TomCat::AssetType::Texture2D);
				sawTileB = sawTileB || (reference.Kind
					== TomCat::SerializedAssetReferenceKind::TilemapCell
					&& handle == 4002
					&& reference.ExpectedType == TomCat::AssetType::Texture2D);
				sawParticle = sawParticle || (reference.Kind
					== TomCat::SerializedAssetReferenceKind::Particle
					&& handle == 5001
					&& reference.ExpectedType == TomCat::AssetType::Texture2D);
				sawMaterial = sawMaterial || (reference.Kind
					== TomCat::SerializedAssetReferenceKind::Material
					&& handle == 6001
					&& reference.ExpectedType == TomCat::AssetType::Material);
				return true;
			}, error) && sawTileA && sawTileB && sawParticle && sawMaterial,
			"Cook traversal missed a Tilemap or Particle Sprite reference");

		auto decoded = TomCat::CreateRef<TomCat::Scene>();
		const std::vector<uint8_t> bytes(document.begin(), document.end());
		Require(TomCat::SceneArchiveCodec::Decode(bytes, decoded,
			"Advanced2DRegression.tomcat", false),
			"Advanced 2D Scene 11 deserialization failed");
		TomCat::Entity loaded = decoded->FindEntityByUUID(entity.GetUUID());
		Require(loaded && loaded.HasComponent<TomCat::Tilemap2D>()
			&& loaded.HasComponent<TomCat::ParticleSystem2D>()
			&& loaded.HasComponent<TomCat::Light2D>()
			&& loaded.HasComponent<TomCat::Grid2D>()
			&& loaded.HasComponent<TomCat::TilemapRenderer2D>(),
			"Advanced 2D components did not round-trip");

		const auto& loadedTilemap = loaded.GetComponent<TomCat::Tilemap2D>();
		Require(!loadedTilemap.Enabled
			&& Near(loadedTilemap.CellSize, tilemap.CellSize)
			&& Near(loadedTilemap.CellGap, tilemap.CellGap)
			&& loadedTilemap.SortingLayer == tilemap.SortingLayer
			&& loadedTilemap.OrderInLayer == tilemap.OrderInLayer
			&& loadedTilemap.Cells.size() == 2,
			"Tilemap authoring values did not round-trip");
		const TomCat::TilemapCell* loadedA =
			TomCat::Tilemap2DRuntime::FindCell(loadedTilemap, cellA.Coordinate);
		const TomCat::TilemapCell* loadedB =
			TomCat::Tilemap2DRuntime::FindCell(loadedTilemap, cellB.Coordinate);
		Require(loadedA && loadedB
			&& loadedA->SpriteHandle == cellA.SpriteHandle
			&& Near(loadedA->Tint, cellA.Tint) && loadedA->FlipX
			&& loadedA->RotationQuarterTurns == 3
			&& loadedB->SpriteHandle == cellB.SpriteHandle
			&& Near(loadedB->Tint, cellB.Tint) && loadedB->FlipY
			&& loadedB->RotationQuarterTurns == 2,
			"Tilemap cells did not round-trip");

		const auto& loadedParticles =
			loaded.GetComponent<TomCat::ParticleSystem2D>();
		Require(!loadedParticles.Enabled && !loadedParticles.PlayOnStart
			&& !loadedParticles.Loop
			&& Near(loadedParticles.Duration, particles.Duration)
			&& Near(loadedParticles.EmissionRate, particles.EmissionRate)
			&& loadedParticles.MaxParticles == particles.MaxParticles
			&& Near(loadedParticles.StartLifetime, particles.StartLifetime)
			&& Near(loadedParticles.StartSpeed, particles.StartSpeed)
			&& Near(loadedParticles.StartSize, particles.StartSize)
			&& Near(loadedParticles.EndSize, particles.EndSize)
			&& Near(loadedParticles.GravityScale, particles.GravityScale)
			&& Near(loadedParticles.Direction, particles.Direction)
			&& Near(loadedParticles.SpreadDegrees, particles.SpreadDegrees)
			&& Near(loadedParticles.StartColor, particles.StartColor)
			&& Near(loadedParticles.EndColor, particles.EndColor)
			&& loadedParticles.SpriteHandle == particles.SpriteHandle
			&& loadedParticles.SortingLayer == particles.SortingLayer
			&& loadedParticles.OrderInLayer == particles.OrderInLayer
			&& loadedParticles.Seed == particles.Seed,
			"Particle authoring values did not round-trip");
		Require(!loadedParticles.RuntimePlaying
			&& !loadedParticles.RuntimeInitialized
			&& Near(loadedParticles.RuntimeTime, 0.0f)
			&& loadedParticles.RuntimeParticles.empty(),
			"Particle transient runtime values leaked into Scene persistence");

		const auto& loadedLight = loaded.GetComponent<TomCat::Light2D>();
		Require(!loadedLight.Enabled && loadedLight.Type == light.Type
			&& Near(loadedLight.Color, light.Color)
			&& Near(loadedLight.Intensity, light.Intensity)
			&& Near(loadedLight.Radius, light.Radius)
			&& Near(loadedLight.Falloff, light.Falloff),
			"Light authoring values did not round-trip");

		const auto& loadedGrid = loaded.GetComponent<TomCat::Grid2D>();
		const auto& loadedRenderer =
			loaded.GetComponent<TomCat::TilemapRenderer2D>();
		Require(Near(loadedGrid.CellSize, grid.CellSize)
			&& Near(loadedGrid.CellGap, grid.CellGap)
			&& loadedGrid.Layout == grid.Layout
			&& loadedGrid.Swizzle == grid.Swizzle,
			"Grid2D authoring values did not round-trip");
		Require(!loadedRenderer.Enabled
			&& loadedRenderer.SortOrder == tileRenderer.SortOrder
			&& loadedRenderer.Mode == tileRenderer.Mode
			&& loadedRenderer.DetectChunkCulling
				== tileRenderer.DetectChunkCulling
			&& loadedRenderer.SortingLayer == tileRenderer.SortingLayer
			&& loadedRenderer.OrderInLayer == tileRenderer.OrderInLayer
			&& loadedRenderer.MaterialHandle == tileRenderer.MaterialHandle,
			"TilemapRenderer2D authoring values did not round-trip");

		const TomCat::Ref<TomCat::Scene> cloned = TomCat::Scene::Copy(decoded);
		const TomCat::Entity clonedEntity = cloned
			? cloned->FindEntityByUUID(entity.GetUUID()) : TomCat::Entity{};
		Require(clonedEntity
			&& clonedEntity.HasComponent<TomCat::Grid2D>()
			&& clonedEntity.HasComponent<TomCat::TilemapRenderer2D>()
			&& clonedEntity.GetComponent<TomCat::TilemapRenderer2D>().MaterialHandle
				== tileRenderer.MaterialHandle,
			"Scene clone omitted the Unity-style Tilemap components");
		Require(registry.Remove(entity,
			TomCat::UUID(TomCat::ComponentIds::TilemapRenderer2D), error)
			&& registry.Remove(entity, TomCat::UUID(TomCat::ComponentIds::Grid2D),
				error)
			&& !entity.HasComponent<TomCat::TilemapRenderer2D>()
			&& !entity.HasComponent<TomCat::Grid2D>(),
			"Component registry could not clear Tilemap renderer/grid data");
	}

}

int main()
{
	try
	{
		TomCat::Log::Init();
		TestEkitSceneLifetime();
		TestMultiSelectionPropertyTransaction();
		TestTilemapEditingAndTransform();
		TestDeterministicParticleRuntime();
		TestLightAttenuation();
		TestRegistrySceneRoundTripAndReferences();
		std::cout << "Advanced2DRegression: PASS\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Advanced2DRegression: FAIL: " << error.what() << '\n';
		return 1;
	}
}
