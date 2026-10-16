#pragma once

// Included after RuntimeUIRegression's real GL, cooked-project and font helpers.
void TestPublishedFontSourceReuse()
{
	using namespace TomCat;
	HiddenOpenGLContext context;
	RequireUI(context.IsAvailable(), "font source reuse requires an OpenGL context");
	TemporaryUIProject environment;
	ProjectConfig config;
	config.Name = "Published Font Source Reuse";
	config.Template = "2D";
	config.AssetDirectory = "Assets";
	config.StartScene = "Main.tomcat";
	auto project = Project::CreateNew(environment.Root / "Project.tcproj", config);
	RequireUI(project != nullptr, "could not create font reuse project");
	const std::array<std::filesystem::path, 3> paths = {
		project->GetAssetPath() / "Primary.ttf",
		project->GetAssetPath() / "Fallback.ttf",
		project->GetAssetPath() / "Emoji.ttf"
	};
	WriteBinary(paths[0], ReadBinary(GetBuiltInFontAssetPath(GetDefaultRuntimeFontHandle())));
	WriteBinary(paths[1], DecodeBase64(SyntheticCJKFontBase64));
	WriteBinary(paths[2], DecodeBase64(SyntheticEmojiFontBase64));
	auto& assets = AssetManager::Get();
	RequireUI(assets.SetProject(project), "could not import font reuse sources");
	std::array<AssetHandle, 3> handles;
	for (size_t index = 0; index < handles.size(); ++index)
	{
		const auto* metadata = assets.Registry().GetMetadata(paths[index]);
		RequireUI(metadata && metadata->Type == AssetType::Font,
			"font reuse source was not imported");
		handles[index] = metadata->Handle;
	}
	auto fixture = BuildUIFixture(handles[0], AssetHandle(0), handles[1], handles[2]);
	const auto scenePath = project->GetAssetPath() / "Main.tomcat";
	RequireUI(SceneSerializer(fixture.Scene).Serialize(scenePath),
		"could not serialize font reuse scene");
	const auto* sceneMetadata = assets.Registry().GetMetadata(scenePath);
	RequireUI(sceneMetadata && project->SetStartSceneHandle(sceneMetadata->Handle)
		&& project->Save(), "could not configure font reuse entry scene");
	assets.ClearManagedCookPayload();
	const auto packagePath = environment.Root / "Build" / "FontReuse.tcpak";
	RequireUI(assets.CookToPackage(packagePath), "could not cook font reuse fixture");
	assets.Shutdown();
	RequireUI(assets.MountCookedPackage(packagePath), "could not mount font reuse fixture");

	auto& fonts = FontManager::Get();
	auto load = [&](std::string_view text) {
		return fonts.Load(handles[0], text, handles[1], handles[2]);
	};
	auto publish = [&](std::string_view text) {
		(void)load(text);
		(void)WaitForFontPreparation();
		RequireUI(fonts.PumpPublishes() == 1, "font reuse atlas was not published");
		auto result = load(text);
		RequireUI(result != nullptr, "font reuse published atlas is missing");
		return result;
	};
	const auto initial = publish("A");
	const auto& originalSources = initial->GetAtlas().ShapingSources;
	RequireUI(originalSources.size() == 3 && !originalSources[0].empty()
		&& !originalSources[1].empty() && !originalSources[2].empty(),
		"initial atlas did not retain its three verified shaping sources");

	// Corrupt only this temporary fixture, after publication. The general asset
	// API must still reject every fresh read. An atlas within the same generation
	// may use its already verified, immutable in-memory sources until invalidated.
	std::array<CookedAssetRange, 3> ranges;
	std::array<char, 3> originalBytes;
	auto patchSources = [&](bool corrupt) {
		std::fstream package(packagePath, std::ios::binary | std::ios::in | std::ios::out);
		RequireUI(static_cast<bool>(package), "could not reopen temporary font package");
		for (size_t index = 0; index < handles.size(); ++index)
		{
			RequireUI(assets.TryGetCookedAssetRange(handles[index], ranges[index])
				&& ranges[index].Size > 8, "font reuse source range is missing");
			const auto position = static_cast<std::streamoff>(ranges[index].Offset + 8);
			if (corrupt)
			{
				package.seekg(position);
				package.get(originalBytes[index]);
			}
			package.seekp(position);
			package.put(corrupt ? static_cast<char>(originalBytes[index] ^ 0x5a)
				: originalBytes[index]);
		}
		package.flush();
		RequireUI(package.good(), "could not patch temporary font package");
	};
	patchSources(true);
	for (const auto handle : handles)
		RequireUI(!assets.LoadImportedArtifact(handle).Succeeded(),
			"font source reuse weakened fresh cooked-asset SHA validation");
	RequireUI(load(MixedUTF8) == initial,
		"font growth discarded the visible atlas before publication");
	const auto grown = publish(MixedUTF8);
	RequireUI(grown != initial && grown->GetAtlas().ShapingSources == originalSources,
		"font growth reread or replaced retained generation sources");
	const auto* cjk = grown->GetAtlas().Find(0x4e2d);
	const auto* emoji = grown->GetAtlas().Find(0x1f600);
	RequireUI(cjk && cjk->SourceIndex == 1 && cjk->AlphaCoverage > 0
		&& emoji && emoji->SourceIndex == 2 && emoji->AlphaCoverage > 0,
		"retained font chain did not preserve fallback and emoji source indices");

	fonts.Release(handles[1]);
	RequireUI(!load(MixedUTF8), "releasing a fallback did not invalidate its font chain");
	const auto rejected = publish(MixedUTF8);
	RequireUI(!rejected->GetAtlas().UsesSourceFont
		&& std::all_of(rejected->GetAtlas().ShapingSources.begin(),
			rejected->GetAtlas().ShapingSources.end(), [](const auto& bytes) { return bytes.empty(); }),
		"released generation bypassed integrity checks using an old published atlas");
	RequireUI(grown->GetAtlas().ShapingSources == originalSources,
		"font invalidation mutated a retained reader's previous atlas");

	patchSources(false);
	// Missing sources are not negatively cached: a later growth retries them.
	const std::string moreText = std::string(MixedUTF8) + "\xce\xa9";
	const auto recovered = publish(moreText);
	RequireUI(recovered->GetAtlas().ShapingSources == originalSources,
		"font growth did not retry previously unavailable sources");
	fonts.ReleaseAll();
	RequireUI(!load("A"), "ReleaseAll preserved an old font generation");
	RequireUI(publish("A")->GetAtlas().ShapingSources == originalSources,
		"font reload after ReleaseAll did not recover verified sources");
	std::cout << "PASS published font source reuse, chain indices, integrity, invalidation and missing-source retry"
		<< std::endl;
}
