#include "RuntimeUIRegression.h"

#include "TomCat/Asset/AssetJobSystem.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Renderer/Font.h"
#include "TomCat/Renderer/Framebuffer.h"
#include "TomCat/Renderer/RenderCommand.h"
#include "TomCat/Renderer/Renderer.h"
#include "TomCat/Runtime/RuntimeUI.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scene/Serialization/AssetReferenceVisitor.h"
#include "TomCat/Scene/Serialization/PrefabArchiveCodec.h"

#include <yaml-cpp/yaml.h>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
	constexpr const char* MixedUTF8 =
		"TomCat \xE4\xB8\xAD\xE6\x96\x87 \xF0\x9F\x98\x80";
	constexpr const char* PlayChineseUTF8 =
		"Play \xE6\xB8\xB8\xE6\x88\x8F";
	constexpr const char* ExitEmojiUTF8 = "Exit \xF0\x9F\x98\x80";
	// Self-authored, minimal TrueType fixtures. Keeping CJK and emoji in separate
	// faces makes the fallback order observable without relying on optional host
	// fonts in CI or redistributing a third-party font.
	constexpr std::string_view SyntheticCJKFontBase64 = R"(
AAEAAAAKAIAAAwAgT1MvMpMMsXsAAAEoAAAAYGNtYXB89DcxAAABlAAAADxnbHlmpSZsXQAAAdwAAACcaGVhZC8COgwAAACsAAAANmhoZWEE7gIeAAAA5AAAACRobXR4AlgAAAAAAYgAAAAKbG9jYQBiAEIAAAHQAAAACm1heHAACQASAAABCAAAACBuYW1laJAh1AAAAngAAAFlcG9zdEhZemYAAAPgAAAAOgABAAAAAQAAbBA9ZF8PPPUAAQPoAAAAAObK+zgAAAAA5sr7OABGAAACEgK8AAAAAwACAAAAAAAAAAEAAAMg/zgAAAJYAAAAjAHMAAEAAAAAAAAAAAAAAAAAAAABAAEAAAAEABAABAAAAAAAAgAAAAAAAAAAAAAAAAAAAAAAAwJYAZAABQAEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACAAAAAAAAAAAAAAAPz8/PwAATi1lhwMg/zgAAAMgAMgAAAAAAAAAAAAAAAAAAAAgAAACWAAAAAAAAAAAAAAAAAACAAAAAwAAABQAAwABAAAAFAAEACgAAAAGAAQAAQACTi1lh///AABOLWWH//+x1Zp8AAEAAAAAAAAAAAAUABQALgBOAAAAAgBQAAACCAK8AAMABwAAMyERIRMhESFQAbj+SFABGP7oArz9qAH0AAMARgAoAhICqAADAAcACwAANyERIRMzESMTMxEjRgHM/jRkUFC0UFAoAoD90AHg/iAB4AAEAEYAKAISAqgAAwAHAAsADwAANyERIRMhNSE1ITUhNSE1IUYBzP40UAEs/tQBLP7UASz+1CgCgP3QWnhaeFAAAAAKAH4AAQAAAAAAAQASAAAAAQAAAAAAAgAHABIAAQAAAAAAAwAaABkAAQAAAAAABAAaABkAAQAAAAAABgAaADMAAwABBAkAAQAkAE0AAwABBAkAAgAOAHEAAwABBAkAAwA0AH8AAwABBAkABAA0AH8AAwABBAkABgA0ALNUb21DYXRTeW50aGV0aWNDSktSZWd1bGFyVG9tQ2F0U3ludGhldGljQ0pLIFJlZ3VsYXJUb21DYXRTeW50aGV0aWNDSkstUmVndWxhcgBUAG8AbQBDAGEAdABTAHkAbgB0AGgAZQB0AGkAYwBDAEoASwBSAGUAZwB1AGwAYQByAFQAbwBtAEMAYQB0AFMAeQBuAHQAaABlAHQAaQBjAEMASgBLACAAUgBlAGcAdQBsAGEAcgBUAG8AbQBDAGEAdABTAHkAbgB0AGgAZQB0AGkAYwBDAEoASwAtAFIAZQBnAHUAbABhAHIAAAAAAgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAEAAAAAwECAQMHdW5pNEUyRAd1bmk2NTg3AAA=
)";
	constexpr std::string_view SyntheticEmojiFontBase64 = R"(
AAEAAAAKAIAAAwAgT1MvMkTfRfMAAAEoAAAAYGNtYXAAHuy0AAABkAAAAFBnbHlmMSMU6AAAAegAAABkaGVhZC8COgwAAACsAAAANmhoZWEE7gIeAAAA5AAAACRobXR4AlgAAAAAAYgAAAAIbG9jYQAUAEYAAAHgAAAACG1heHAACAASAAABCAAAACBuYW1lF2F0AwAAAkwAAAF3cG9zdDytYkgAAAPEAAAALwABAAAAAQAApG/kf18PPPUAAQPoAAAAAObK+zgAAAAA5sr7OABGAAACEgK8AAAAAwACAAAAAAAAAAEAAAMg/zgAAAJYAAAAjAHMAAEAAAAAAAAAAAAAAAAAAAABAAEAAAADABAABAAAAAAAAgAAAAAAAAAAAAAAAAAAAAAAAwJYAZAABQAEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAgAAAAAAAAAAAAAAPz8/PwAA/////wMg/zgAAAMgAMgAAAAAAAAAAAAAAAAAAAAgAAACWAAAAAAAAAAAAAMAAAADAAAAHAADAAEAAAAcAAMACgAAADQABAAYAAAAAgACAAAAAP//AAD//wABAAAADAAAAAAAHAAAAAAAAAABAAH2AAAB9gAAAAACAAAAFAAUADIAAgBQAAACCAK8AAMABwAAMyERIRMhESFQAbj+SFABGP7oArz9qAH0AAQARgAoAhICqAADAAcACwAPAAA3IREhFzM1IxczNSMDMzUjRgHM/jRkUFC0UFCl5uYoAoD6UFBQ/phQAAAACgB+AAEAAAAAAAEAFAAAAAEAAAAAAAIABwAUAAEAAAAAAAMAHAAbAAEAAAAAAAQAHAAbAAEAAAAAAAYAHAA3AAMAAQQJAAEAKABTAAMAAQQJAAIADgB7AAMAAQQJAAMAOACJAAMAAQQJAAQAOACJAAMAAQQJAAYAOADBVG9tQ2F0U3ludGhldGljRW1vamlSZWd1bGFyVG9tQ2F0U3ludGhldGljRW1vamkgUmVndWxhclRvbUNhdFN5bnRoZXRpY0Vtb2ppLVJlZ3VsYXIAVABvAG0AQwBhAHQAUwB5AG4AdABoAGUAdABpAGMARQBtAG8AagBpAFIAZQBnAHUAbABhAHIAVABvAG0AQwBhAHQAUwB5AG4AdABoAGUAdABpAGMARQBtAG8AagBpACAAUgBlAGcAdQBsAGEAcgBUAG8AbQBDAGEAdABTAHkAbgB0AGgAZQB0AGkAYwBFAG0AbwBqAGkALQBSAGUAZwB1AGwAYQByAAACAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAMAAAADAQIGdTFGNjAwAA==
)";

	void RequireUI(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	bool Near(float first, float second, float epsilon = 1.0e-3f)
	{
		return std::abs(first - second) <= epsilon;
	}

	std::vector<uint8_t> DecodeBase64(std::string_view encoded)
	{
		auto digit = [](char value)
		{
			if (value >= 'A' && value <= 'Z') return value - 'A';
			if (value >= 'a' && value <= 'z') return value - 'a' + 26;
			if (value >= '0' && value <= '9') return value - '0' + 52;
			if (value == '+') return 62;
			if (value == '/') return 63;
			return -1;
		};
		std::vector<uint8_t> bytes;
		uint32_t buffer = 0;
		int availableBits = 0;
		for (char value : encoded)
		{
			if (value == '=')
				break;
			const int decoded = digit(value);
			if (decoded < 0)
				continue;
			buffer = (buffer << 6) | static_cast<uint32_t>(decoded);
			availableBits += 6;
			if (availableBits >= 8)
			{
				availableBits -= 8;
				bytes.push_back(static_cast<uint8_t>(
					(buffer >> availableBits) & 0xffu));
			}
		}
		return bytes;
	}

	std::vector<uint8_t> ReadBinary(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		RequireUI(static_cast<bool>(input), "could not open Runtime UI binary fixture");
		return { std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>() };
	}

	void WriteBinary(const std::filesystem::path& path,
		const std::vector<uint8_t>& bytes)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		RequireUI(static_cast<bool>(output), "could not create Runtime UI fixture");
		if (!bytes.empty())
			output.write(reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		RequireUI(output.good(), "could not write Runtime UI fixture");
	}

	std::filesystem::path FindRepositoryFile(const std::filesystem::path& relative)
	{
		auto search = [&](std::filesystem::path cursor)
		{
			std::error_code error;
			for (uint32_t depth = 0; depth < 10 && !cursor.empty(); ++depth)
			{
				const std::filesystem::path candidate = cursor / relative;
				if (std::filesystem::is_regular_file(candidate, error) && !error)
					return candidate;
				error.clear();
				const std::filesystem::path parent = cursor.parent_path();
				if (parent == cursor)
					break;
				cursor = parent;
			}
			return std::filesystem::path{};
		};
		if (std::filesystem::path found = search(std::filesystem::current_path());
			!found.empty())
			return found;
		const std::filesystem::path source(__FILE__);
		return search(source.is_absolute() ? source.parent_path()
			: std::filesystem::absolute(source).parent_path());
	}

	struct UIFixture
	{
		TomCat::Ref<TomCat::Scene> Scene;
		TomCat::Entity Canvas;
		TomCat::Entity Panel;
		TomCat::Entity First;
		TomCat::Entity Second;
	};

	UIFixture BuildUIFixture(TomCat::AssetHandle font = TomCat::AssetHandle(0),
		TomCat::AssetHandle image = TomCat::AssetHandle(0),
		TomCat::AssetHandle fallbackFont = TomCat::AssetHandle(0),
		TomCat::AssetHandle emojiFont = TomCat::AssetHandle(0))
	{
		UIFixture result;
		result.Scene = TomCat::CreateRef<TomCat::Scene>();
		result.Scene->SetSceneName("Runtime UI regression");
		result.Canvas = result.Scene->CreateEntityWithUUID(TomCat::UUID(10001),
			"Canvas");
		auto& canvas = result.Canvas.AddComponent<TomCat::Canvas>();
		canvas.ReferenceResolution = { 1920.0f, 1080.0f };
		canvas.MatchWidthOrHeight = 0.5f;
		result.Canvas.AddComponent<TomCat::UIEventSystem>();

		result.Panel = result.Scene->CreateEntityWithUUID(TomCat::UUID(10002),
			"Clipped panel");
		auto& panelRect = result.Panel.AddComponent<TomCat::RectTransform>();
		panelRect.AnchorMin = panelRect.AnchorMax = { 0.5f, 0.5f };
		panelRect.Pivot = { 0.5f, 0.5f };
		panelRect.SizeDelta = { 400.0f, 100.0f };
		panelRect.ClipChildren = true;
		auto& panelImage = result.Panel.AddComponent<TomCat::UIImage>();
		panelImage.Image = image;
		panelImage.PreserveAspect = true;
		auto& group = result.Panel.AddComponent<TomCat::UILayoutGroup>();
		group.Direction = TomCat::UILayoutDirection::Vertical;
		group.Padding = { 10.0f, 10.0f, 10.0f, 10.0f };
		group.Spacing = 5.0f;
		group.ControlChildSize = true;
		group.ChildSize = { 120.0f, 60.0f };
		RequireUI(result.Scene->SetParent(result.Panel, result.Canvas),
			"could not parent Runtime UI panel");

		result.First = result.Scene->CreateEntityWithUUID(TomCat::UUID(10003),
			"First button");
		result.First.AddComponent<TomCat::RectTransform>();
		result.First.AddComponent<TomCat::UIImage>();
		result.First.AddComponent<TomCat::UIButton>();
		auto& firstText = result.First.AddComponent<TomCat::UIText>();
		firstText.Font = font;
		firstText.FallbackFont = fallbackFont;
		firstText.EmojiFont = emojiFont;
		firstText.Text = PlayChineseUTF8;
		firstText.Alignment = TomCat::TextAlignment::Center;
		RequireUI(result.Scene->SetParent(result.First, result.Panel),
			"could not parent first Runtime UI button");

		result.Second = result.Scene->CreateEntityWithUUID(TomCat::UUID(10004),
			"Second button");
		result.Second.AddComponent<TomCat::RectTransform>();
		result.Second.AddComponent<TomCat::UIImage>();
		result.Second.AddComponent<TomCat::UIButton>();
		auto& secondText = result.Second.AddComponent<TomCat::UIText>();
		secondText.Font = font;
		secondText.FallbackFont = fallbackFont;
		secondText.EmojiFont = emojiFont;
		secondText.Text = ExitEmojiUTF8;
		RequireUI(result.Scene->SetParent(result.Second, result.Panel),
			"could not parent second Runtime UI button");
		return result;
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
			m_Window = glfwCreateWindow(64, 64, "TomCat Runtime UI Golden", nullptr,
				nullptr);
			if (!m_Window)
			{
				m_UnavailableReason = "an OpenGL 4.6 context is unavailable";
				Cleanup();
				return;
			}
			glfwMakeContextCurrent(m_Window);
			if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(
				glfwGetProcAddress)) == 0 || !GLAD_GL_VERSION_4_6)
			{
				m_UnavailableReason = "OpenGL 4.6 entry points are unavailable";
				Cleanup();
				return;
			}
			// Mark first so a partially initialized Renderer2D is released if shader
			// creation throws. Constructors do not run their destructor on failure.
			m_RendererInitialized = true;
			try
			{
				TomCat::Renderer::Init();
			}
			catch (...)
			{
				Cleanup();
				throw;
			}
		}

		~HiddenOpenGLContext()
		{
			Cleanup();
		}

		bool IsAvailable() const { return m_RendererInitialized; }
		const std::string& GetUnavailableReason() const
		{
			return m_UnavailableReason;
		}

		HiddenOpenGLContext(const HiddenOpenGLContext&) = delete;
		HiddenOpenGLContext& operator=(const HiddenOpenGLContext&) = delete;

	private:
		void Cleanup()
		{
			if (m_RendererInitialized)
			{
				TomCat::FontManager::Get().ReleaseAll();
				TomCat::AssetManager::Get().ReleaseAll();
				TomCat::Renderer::Shutdown();
				m_RendererInitialized = false;
			}
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
		bool m_RendererInitialized = false;
		std::string m_UnavailableReason;
	};

	std::vector<uint8_t> CaptureRuntimeUI(TomCat::Scene& scene, uint32_t width,
		uint32_t height, float dpi)
	{
		TomCat::FramebufferSpecification specification;
		specification.Width = width;
		specification.Height = height;
		specification.Attachments = { TomCat::FramebufferTextureFormat::RGBA8 };
		TomCat::Ref<TomCat::Framebuffer> framebuffer =
			TomCat::Framebuffer::Create(specification);
		RequireUI(framebuffer != nullptr,
			"could not create the Runtime UI screenshot framebuffer");
		framebuffer->Bind();
		TomCat::RenderCommand::SetClearColor({ 8.0f / 255.0f, 12.0f / 255.0f,
			18.0f / 255.0f, 1.0f });
		TomCat::RenderCommand::Clear();
		TomCat::RuntimeUISystem::RenderScreen(scene, width, height, dpi);
		glFinish();
		std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, static_cast<GLsizei>(width),
			static_cast<GLsizei>(height), GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		const GLenum readError = glGetError();
		framebuffer->Unbind();
		RequireUI(readError == GL_NO_ERROR,
			"OpenGL failed to read the Runtime UI RGBA screenshot");
		return pixels;
	}

	uint8_t ScreenshotClass(const uint8_t* pixel)
	{
		if (pixel[0] < 20 && pixel[1] < 24 && pixel[2] < 30)
			return 0;
		if (pixel[0] > 120 && pixel[0] > pixel[1] * 2)
			return 1;
		if (pixel[1] > 120 && pixel[1] > pixel[0] * 2)
			return 2;
		if (pixel[2] > 100 && pixel[2] > pixel[0] * 2)
			return 3;
		if (pixel[0] > 170 && pixel[1] > 170 && pixel[2] > 170)
			return 4;
		return 5;
	}

	uint64_t ScreenshotGoldenHash(std::span<const uint8_t> pixels,
		uint32_t width, uint32_t height)
	{
		uint64_t hash = 1469598103934665603ull;
		for (uint32_t row = 0; row < 45; ++row)
		{
			for (uint32_t column = 0; column < 80; ++column)
			{
				const uint32_t x = std::min(width - 1,
					static_cast<uint32_t>((column + 0.37) * width / 80.0));
				const uint32_t y = std::min(height - 1,
					static_cast<uint32_t>((row + 0.41) * height / 45.0));
				const size_t offset = (static_cast<size_t>(y) * width + x) * 4u;
				hash ^= ScreenshotClass(pixels.data() + offset);
				hash *= 1099511628211ull;
			}
		}
		return hash;
	}

	void TestUTF8AndDeterministicFontAtlas()
	{
		bool valid = false;
		const std::vector<uint32_t> decoded = TomCat::FontAtlasBuilder::DecodeUTF8(
			MixedUTF8, &valid);
		RequireUI(valid && std::find(decoded.begin(), decoded.end(), 0x4e2du)
			!= decoded.end() && std::find(decoded.begin(), decoded.end(), 0x6587u)
			!= decoded.end() && std::find(decoded.begin(), decoded.end(), 0x1f600u)
			!= decoded.end(), "UTF-8 Chinese/English/emoji decoding failed");
		const std::string malformed("\xf0\x28\x8c\x28", 4);
		const std::vector<uint32_t> repaired =
			TomCat::FontAtlasBuilder::DecodeUTF8(malformed, &valid);
		RequireUI(!valid && !repaired.empty()
			&& repaired.front() == TomCat::FontAtlasBuilder::ReplacementCodepoint,
			"malformed UTF-8 did not produce a replacement glyph");
		const std::filesystem::path fontPath = FindRepositoryFile(
			"Editor/TomCatInut/Packages/fonts/opensans/OpenSans-Regular.ttf");
		RequireUI(!fontPath.empty(), "could not locate OpenSans font fixture");
		const std::vector<uint8_t> bytes = ReadBinary(fontPath);
		const std::vector<uint32_t> requested(decoded.begin(), decoded.end());
		TomCat::FontAtlasData first, second;
		RequireUI(TomCat::FontAtlasBuilder::Build(bytes, requested, first)
			&& TomCat::FontAtlasBuilder::Build(bytes, requested, second),
			"valid OpenSans font did not build an atlas");
		RequireUI(first.DeterministicHash == second.DeterministicHash
			&& first.Width == second.Width && first.Height == second.Height
			&& first.PixelsRGBA == second.PixelsRGBA,
			"font atlas output is not deterministic");
		const TomCat::FontGlyph* latin = first.Find('T');
		const TomCat::FontGlyph* chinese = first.Find(0x4e2d);
		const TomCat::FontGlyph* emoji = first.Find(0x1f600);
		RequireUI(latin && !latin->UsesFallback && chinese && chinese->UsesFallback
			&& emoji && emoji->UsesFallback,
			"source, missing CJK, or emoji fallback glyph selection is wrong");

		const std::vector<uint8_t> cjkBytes =
			DecodeBase64(SyntheticCJKFontBase64);
		const std::vector<uint8_t> emojiBytes =
			DecodeBase64(SyntheticEmojiFontBase64);
		const std::array<std::span<const uint8_t>, 3> sourceChain = {
			std::span<const uint8_t>(bytes),
			std::span<const uint8_t>(cjkBytes),
			std::span<const uint8_t>(emojiBytes)
		};
		TomCat::FontAtlasData chained, chainedAgain;
		RequireUI(TomCat::FontAtlasBuilder::Build(sourceChain, requested, chained)
			&& TomCat::FontAtlasBuilder::Build(
				sourceChain, requested, chainedAgain),
			"primary/CJK/emoji font chain did not build an atlas");
		latin = chained.Find('T');
		chinese = chained.Find(0x4e2d);
		const TomCat::FontGlyph* secondChinese = chained.Find(0x6587);
		emoji = chained.Find(0x1f600);
		RequireUI(chained.DeterministicHash == chainedAgain.DeterministicHash
			&& chained.PixelsRGBA == chainedAgain.PixelsRGBA
			&& latin && latin->SourceIndex == 0 && !latin->UsesFallback
			&& latin->AlphaCoverage > 0
			&& chinese && secondChinese
			&& chinese->SourceIndex == 1 && secondChinese->SourceIndex == 1
			&& chinese->UsesFallback && secondChinese->UsesFallback
			&& !chinese->IsProceduralFallback
			&& chinese->AlphaCoverage > 0 && secondChinese->AlphaCoverage > 0
			&& emoji && emoji->SourceIndex == 2 && emoji->UsesFallback
			&& !emoji->IsProceduralFallback && emoji->AlphaCoverage > 0,
			"font chain did not rasterize real Latin/CJK/emoji glyphs in order");

		std::vector<uint32_t> excessive;
		excessive.reserve(70000);
		for (uint32_t value = 0x1000; excessive.size() < 70000; ++value)
			if (value < 0xd800 || value > 0xdfff)
				excessive.push_back(value);
		TomCat::FontAtlasData unchanged;
		unchanged.Width = 7;
		RequireUI(!TomCat::FontAtlasBuilder::Build(
			std::span<const uint8_t>{}, excessive, unchanged)
			&& unchanged.Width == 7,
			"oversized codepoint set was not rejected transactionally");

		const TomCat::TextLayoutResult normal = TomCat::TextLayoutEngine::Build(
			first, "TomCat", 24.0f, 0.0f, TomCat::TextAlignment::Left);
		const TomCat::TextLayoutResult dpi150 = TomCat::TextLayoutEngine::Build(
			first, "TomCat", 36.0f, 0.0f, TomCat::TextAlignment::Left);
		RequireUI(normal.Width > 0.0f && Near(dpi150.Width, normal.Width * 1.5f)
			&& Near(dpi150.Height, normal.Height * 1.5f),
			"UIText font metrics did not scale with Canvas DPI");
	}

	void TestLayoutClippingAspectAndInput()
	{
		UIFixture fixture = BuildUIFixture();
		fixture.Canvas.GetComponent<TomCat::Tag>().ActiveSelf = false;
		const TomCat::RuntimeUILayoutSnapshot inactiveGameplay =
			TomCat::RuntimeUISystem::BuildLayout(*fixture.Scene, 1920, 1080, 96.0f);
		const TomCat::RuntimeUILayoutSnapshot inactiveEditor =
			TomCat::RuntimeUISystem::BuildLayout(*fixture.Scene, 1920, 1080, 96.0f,
				TomCat::RuntimeUIVisibilityMode::Editor);
		RequireUI(inactiveGameplay.RenderOrder.empty()
			&& inactiveEditor.Rectangles.contains(fixture.Canvas.GetUUID())
			&& inactiveEditor.Rectangles.contains(fixture.First.GetUUID()),
			"inactive gameplay UI was not kept editable only in the Scene view");
		fixture.Canvas.GetComponent<TomCat::Tag>().ActiveSelf = true;
		RequireUI(fixture.Scene->SetEditorHidden(fixture.Canvas, true),
			"could not hide the Runtime UI fixture in the Scene view");
		const TomCat::RuntimeUILayoutSnapshot hiddenGameplay =
			TomCat::RuntimeUISystem::BuildLayout(*fixture.Scene, 1920, 1080, 96.0f);
		const TomCat::RuntimeUILayoutSnapshot hiddenEditor =
			TomCat::RuntimeUISystem::BuildLayout(*fixture.Scene, 1920, 1080, 96.0f,
				TomCat::RuntimeUIVisibilityMode::Editor);
		RequireUI(hiddenGameplay.Rectangles.contains(fixture.Canvas.GetUUID())
			&& hiddenGameplay.Rectangles.contains(fixture.First.GetUUID())
			&& hiddenEditor.RenderOrder.empty(),
			"editor-only hidden state leaked into gameplay UI visibility");
		RequireUI(fixture.Scene->SetEditorHidden(fixture.Canvas, false),
			"could not restore the Runtime UI fixture Scene visibility");
		TomCat::Entity stretched = fixture.Scene->CreateEntityWithUUID(
			TomCat::UUID(10006), "Stretched pivot probe");
		auto& stretchedRect = stretched.AddComponent<TomCat::RectTransform>();
		stretchedRect.AnchorMin = { 0.25f, 0.25f };
		stretchedRect.AnchorMax = { 0.75f, 0.75f };
		stretchedRect.Pivot = { 0.0f, 1.0f };
		stretchedRect.SizeDelta = { 0.0f, 0.0f };
		RequireUI(fixture.Scene->SetParent(stretched, fixture.Canvas),
			"could not parent stretched RectTransform probe");
		struct Viewport { uint32_t Width; uint32_t Height; float BaseScale; };
		const Viewport viewports[] = {
			{ 1920, 1080, 1.0f },
			{ 1440, 1080, std::sqrt(0.75f) },
			{ 2560, 1080, std::sqrt(4.0f / 3.0f) }
		};
		for (const Viewport& viewport : viewports)
		{
			for (float dpi : { 96.0f, 144.0f, 192.0f })
			{
				const float scale = viewport.BaseScale * dpi / 96.0f;
				const TomCat::RuntimeUILayoutSnapshot first =
					TomCat::RuntimeUISystem::BuildLayout(*fixture.Scene,
						viewport.Width, viewport.Height, dpi);
				const TomCat::RuntimeUILayoutSnapshot second =
					TomCat::RuntimeUISystem::BuildLayout(*fixture.Scene,
						viewport.Width, viewport.Height, dpi);
				const TomCat::UIRect panel = first.Rectangles.at(TomCat::UUID(10002));
				const TomCat::UIRect button = first.Rectangles.at(TomCat::UUID(10003));
				const TomCat::UIRect clipped = first.Clips.at(TomCat::UUID(10004));
				const TomCat::UIRect stretchedValue = first.Rectangles.at(
					TomCat::UUID(10006));
				RequireUI(Near(first.Scales.at(TomCat::UUID(10001)), scale)
					&& Near(panel.Width, 400.0f * scale)
					&& Near(panel.Height, 100.0f * scale)
					&& Near(panel.X, (viewport.Width - panel.Width) * 0.5f)
					&& Near(panel.Y, (viewport.Height - panel.Height) * 0.5f),
					"16:9/4:3/ultrawide or DPI Canvas layout is wrong");
				RequireUI(Near(button.X, panel.X + 10.0f * scale)
					&& Near(button.Y, panel.Y + 30.0f * scale)
					&& Near(button.Width, 120.0f * scale)
					&& Near(button.Height, 60.0f * scale)
					&& Near(clipped.Height, 25.0f * scale),
					"layout group or Rect clipping geometry is wrong");
				RequireUI(Near(stretchedValue.X, viewport.Width * 0.25f)
					&& Near(stretchedValue.Y, viewport.Height * 0.25f)
					&& Near(stretchedValue.Width, viewport.Width * 0.5f)
					&& Near(stretchedValue.Height, viewport.Height * 0.5f),
					"stretched RectTransform did not apply Pivot inside its anchor range");
				RequireUI(first.Rectangles.at(TomCat::UUID(10003)).X
					== second.Rectangles.at(TomCat::UUID(10003)).X
					&& first.Clips.at(TomCat::UUID(10004)).Height
					== second.Clips.at(TomCat::UUID(10004)).Height,
					"Runtime UI layout is not deterministic");
			}
		}

		TomCat::UIImageGeometry geometry;
		RequireUI(TomCat::RuntimeUISystem::BuildImageGeometry(
			{ 0.0f, 0.0f, 200.0f, 100.0f }, { 75.0f, 0.0f, 50.0f, 100.0f },
			1.0f, { 0.25f, 0.5f }, { 0.75f, 1.0f }, true, geometry)
			&& Near(geometry.Rect.X, 75.0f) && Near(geometry.Rect.Width, 50.0f)
			&& Near(geometry.UVMin.x, 0.375f) && Near(geometry.UVMax.x, 0.625f),
			"UIImage PreserveAspect/sub-sprite UV clipping composition is wrong");
		const glm::vec2 mapped = TomCat::RuntimeUISystem::MapPointerToViewport(
			{ 470.0f, 350.0f }, { 400.0f, 300.0f });
		RequireUI(mapped == glm::vec2(70.0f, 50.0f),
			"Editor Game viewport pointer-origin mapping is wrong");

		const TomCat::RuntimeUILayoutSnapshot layout =
			TomCat::RuntimeUISystem::BuildLayout(*fixture.Scene, 1920, 1080, 96.0f);
		const TomCat::UIRect firstRect = layout.Rectangles.at(TomCat::UUID(10003));
		const TomCat::UIRect secondRect = layout.Rectangles.at(TomCat::UUID(10004));
		const TomCat::UIRect secondVisible = TomCat::UIRect::Intersect(secondRect,
			layout.Clips.at(TomCat::UUID(10004)));
		TomCat::RuntimeUIInputFrame input;
		input.PointerPosition = { firstRect.X + 10.0f,
			1080.0f - (firstRect.Y + 10.0f) };
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(fixture.First.GetComponent<TomCat::UIButton>().RuntimeFocused
			&& !TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"UI hover did not focus deterministically or suppressed Gameplay");
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});
		RequireUI(!TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"idle UIEventSystem permanently captured Gameplay input");
		input.PointerPosition = { firstRect.X + 10.0f,
			1080.0f - (firstRect.Y + 10.0f) };
		input.MousePressed = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"UI pointer press did not capture Gameplay for its frame");
		input.MousePressed = false;
		input.MouseHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"UI pointer hold did not retain Gameplay capture");
		input.MouseHeld = false;
		input.MouseReleased = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::IsGameplayInputCaptured()
			&& TomCat::RuntimeUISystem::WasButtonClicked(fixture.First)
			&& TomCat::RuntimeUISystem::GetButtonClickSerial(fixture.First) == 1,
			"mouse release did not capture or click the focused button");

		TomCat::Entity overlay = fixture.Scene->CreateEntityWithUUID(
			TomCat::UUID(10005), "Raycast overlay");
		auto& overlayRect = overlay.AddComponent<TomCat::RectTransform>();
		overlayRect.AnchorMin = overlayRect.AnchorMax = { 0.0f, 0.0f };
		overlayRect.Pivot = { 0.0f, 0.0f };
		overlayRect.AnchoredPosition = { firstRect.X, firstRect.Y };
		overlayRect.SizeDelta = { firstRect.Width, firstRect.Height };
		auto& overlayImage = overlay.AddComponent<TomCat::UIImage>();
		auto& overlayText = overlay.AddComponent<TomCat::UIText>();
		overlayText.RaycastTarget = true;
		RequireUI(fixture.Scene->SetParent(overlay, fixture.Canvas),
			"could not parent Runtime UI raycast overlay");
		input = {};
		input.PointerPosition = { firstRect.X + 10.0f,
			1080.0f - (firstRect.Y + 10.0f) };
		input.MousePressed = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(!fixture.First.GetComponent<TomCat::UIButton>().RuntimePressed
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"topmost UIImage raycast target did not block the button below");
		overlayImage.RaycastTarget = false;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(!fixture.First.GetComponent<TomCat::UIButton>().RuntimePressed,
			"topmost UIText raycast target did not block the button below");
		overlayText.RaycastTarget = false;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(fixture.First.GetComponent<TomCat::UIButton>().RuntimePressed,
			"RaycastTarget=false overlay did not pass through to the button below");
		input.MousePressed = false;
		input.MouseReleased = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::GetButtonClickSerial(fixture.First) == 2,
			"raycast pass-through did not complete the underlying button click");

		input = {};
		input.KeyboardMoveNext = true;
		input.KeyboardMoveNextHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(fixture.Second.GetComponent<TomCat::UIButton>().RuntimeFocused
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"keyboard navigation press did not move focus or capture Gameplay");
		input = {};
		input.KeyboardMoveNextHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(fixture.Second.GetComponent<TomCat::UIButton>().RuntimeFocused
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"keyboard navigation hold repeated or lost Gameplay ownership");
		input = {};
		input.KeyboardMoveNextReleased = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(fixture.Second.GetComponent<TomCat::UIButton>().RuntimeFocused
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"keyboard navigation release frame was not owned by Runtime UI");
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});
		RequireUI(!TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"keyboard navigation ownership survived its release frame");
		input = {};
		input.KeyboardMoveNext = true;
		input.KeyboardMoveNextHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(fixture.First.GetComponent<TomCat::UIButton>().RuntimeFocused
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"next keyboard navigation press did not start a new UI interaction");
		input = {};
		input.KeyboardMoveNextReleased = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});

		input = {};
		input.KeyboardSubmit = true;
		input.KeyboardSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::GetButtonClickSerial(fixture.First) == 3
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"keyboard submit press did not click or capture Gameplay");
		input = {};
		input.KeyboardSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::GetButtonClickSerial(fixture.First) == 3
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"held keyboard submit repeated its click or lost Gameplay ownership");
		input = {};
		input.KeyboardSubmitReleased = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::GetButtonClickSerial(fixture.First) == 3
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"keyboard submit release repeated its click or leaked to Gameplay");
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});
		RequireUI(!TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"keyboard submit ownership survived its release frame");
		input = {};
		input.KeyboardSubmit = true;
		input.KeyboardSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::GetButtonClickSerial(fixture.First) == 4
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"next keyboard submit press did not produce one new click");
		input = {};
		input.KeyboardSubmitReleased = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});

		input = {};
		input.GamepadMovePrevious = true;
		input.GamepadMovePreviousHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(fixture.Second.GetComponent<TomCat::UIButton>().RuntimeFocused
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"gamepad navigation press did not move focus or capture Gameplay");
		input = {};
		input.GamepadMovePreviousHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(fixture.Second.GetComponent<TomCat::UIButton>().RuntimeFocused
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"gamepad navigation hold repeated or lost Gameplay ownership");
		input = {};
		input.GamepadMovePreviousReleased = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"gamepad navigation release frame was not owned by Runtime UI");
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});
		RequireUI(!TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"gamepad navigation ownership survived its release frame");
		input = {};
		input.GamepadMovePrevious = true;
		input.GamepadMovePreviousHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(fixture.First.GetComponent<TomCat::UIButton>().RuntimeFocused
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"next gamepad navigation press did not start a new UI interaction");
		input = {};
		input.GamepadMovePreviousReleased = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});

		input = {};
		input.GamepadSubmit = true;
		input.GamepadSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::GetButtonClickSerial(fixture.First) == 5
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"gamepad submit press did not click or capture Gameplay");
		input = {};
		input.GamepadSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::GetButtonClickSerial(fixture.First) == 5
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"held gamepad submit repeated its click or lost Gameplay ownership");
		input = {};
		input.GamepadSubmitReleased = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::GetButtonClickSerial(fixture.First) == 5
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"gamepad submit release repeated its click or leaked to Gameplay");
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});
		RequireUI(!TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"gamepad submit ownership survived its release frame");
		input = {};
		input.GamepadSubmit = true;
		input.GamepadSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::GetButtonClickSerial(fixture.First) == 6,
			"next gamepad submit press did not produce one new click");
		input = {};
		input.GamepadSubmitReleased = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});

		auto& firstButton = fixture.First.GetComponent<TomCat::UIButton>();
		auto& secondButton = fixture.Second.GetComponent<TomCat::UIButton>();
		firstButton.RuntimeFocused = true;
		secondButton.RuntimeFocused = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});
		RequireUI(firstButton.RuntimeFocused && !secondButton.RuntimeFocused,
			"Runtime UI did not repair duplicate focus deterministically");
		input = {};
		input.KeyboardSubmit = true;
		input.KeyboardSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"focused control did not acquire keyboard Submit ownership");
		firstButton.RuntimePressed = true;
		firstButton.Enabled = false;
		input = {};
		input.KeyboardSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(!firstButton.RuntimeFocused && !firstButton.RuntimePressed
			&& secondButton.RuntimeFocused
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"disabled focused button retained state or transferred its held input");
		input = {};
		input.KeyboardSubmitReleased = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"disabled focused button leaked its owned keyboard release to Gameplay");
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});
		RequireUI(!TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"disabled focused button kept ownership after keyboard release");
		firstButton.Enabled = true;
		RequireUI(TomCat::RuntimeUISystem::FocusButton(*fixture.Scene, fixture.First),
			"could not focus first button for pointer-refocus ownership regression");

		input = {};
		input.GamepadSubmit = true;
		input.GamepadSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		input = {};
		input.GamepadSubmitHeld = true;
		input.MousePressed = true;
		input.PointerPosition = { secondVisible.X + secondVisible.Width * 0.5f,
			1080.0f - (secondVisible.Y + secondVisible.Height * 0.5f) };
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(secondButton.RuntimeFocused
			&& TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"mouse refocus transferred an already-owned gamepad Submit to Gameplay");
		input.MousePressed = false;
		input.MouseReleased = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		input = {};
		input.GamepadSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"mouse refocus ended held gamepad ownership before physical release");
		input = {};
		input.GamepadSubmitReleased = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"mouse-refocused gamepad release frame leaked to Gameplay");
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});
		RequireUI(!TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"mouse-refocused gamepad ownership survived physical release");

		input = {};
		input.GamepadSubmit = true;
		input.GamepadSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"gamepad Submit did not acquire ownership before focus loss");
		input = {};
		input.WindowFocused = false;
		input.GamepadSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(!TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"window focus loss did not clear gamepad ownership");
		input = {};
		input.GamepadSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(!TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"held gamepad input reacquired ownership after window focus returned");

		input = {};
		input.KeyboardSubmit = true;
		input.KeyboardSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"keyboard Submit did not acquire ownership before scene stop");
		fixture.Scene->OnRuntimeStop();
		RequireUI(!TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"scene stop did not clear Runtime UI control ownership");
		input = {};
		input.KeyboardSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(!TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"held keyboard input reacquired ownership after scene stop");

		input = {};
		input.GamepadSubmit = true;
		input.GamepadSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"gamepad Submit did not acquire ownership before scene switch");
		UIFixture switched = BuildUIFixture();
		input = {};
		input.GamepadSubmitHeld = true;
		TomCat::RuntimeUISystem::UpdateWithInput(*switched.Scene, 1920, 1080,
			96.0f, input);
		RequireUI(!TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"scene switch transferred held gamepad ownership to the next scene");

		fixture.Canvas.GetComponent<TomCat::UIEventSystem>().ConsumeGameplayInput = false;
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});
		RequireUI(!TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"UIEventSystem ConsumeGameplayInput=false still captured Gameplay");
	}

	void TestFixedInputCaptureSnapshot()
	{
		UIFixture fixture = BuildUIFixture();
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});
		const TomCat::RuntimeUILayoutSnapshot layout =
			TomCat::RuntimeUISystem::BuildLayout(*fixture.Scene, 1920, 1080,
				96.0f);
		const TomCat::UIRect firstRect =
			layout.Rectangles.at(fixture.First.GetUUID());
		const glm::vec2 pointer(firstRect.X + 10.0f,
			1080.0f - (firstRect.Y + 10.0f));
		auto& button = fixture.First.GetComponent<TomCat::UIButton>();
		const bool focusedBefore = button.RuntimeFocused;
		const bool pressedBefore = button.RuntimePressed;
		const bool hoveredBefore = button.RuntimeHovered;
		const uint64_t clickSerialBefore = button.RuntimeClickSerial;

		TomCat::RuntimeUIInputFrame press;
		press.PointerPosition = pointer;
		press.MousePressed = true;
		TomCat::RuntimeUISystem::PrepareFixedInputCaptureWithInput(
			*fixture.Scene, 1920, 1080, 96.0f, press);
		RequireUI(TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"fixed input prepass did not capture a current-frame UI press");
		RequireUI(button.RuntimeFocused == focusedBefore
			&& button.RuntimePressed == pressedBefore
			&& button.RuntimeHovered == hoveredBefore
			&& button.RuntimeClickSerial == clickSerialBefore,
			"fixed input prepass dispatched or mutated Runtime UI interaction state");

		// A catch-up substep has no one-shot edges. The frozen decision must remain
		// stable until the display UI update, rather than exposing held input to
		// Gameplay on the second physics tick.
		TomCat::RuntimeUISystem::PrepareFixedInputCaptureWithInput(
			*fixture.Scene, 1920, 1080, 96.0f, {});
		RequireUI(TomCat::RuntimeUISystem::IsGameplayInputCaptured()
			&& button.RuntimeClickSerial == clickSerialBefore,
			"catch-up fixed substep recomputed or dispatched the UI capture");

		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, press);
		RequireUI(button.RuntimePressed
			&& button.RuntimeClickSerial == clickSerialBefore,
			"display UI update did not commit the prepared press exactly once");

		TomCat::RuntimeUIInputFrame release;
		release.PointerPosition = pointer;
		release.MouseReleased = true;
		TomCat::RuntimeUISystem::PrepareFixedInputCaptureWithInput(
			*fixture.Scene, 1920, 1080, 96.0f, release);
		RequireUI(TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"fixed input prepass did not preserve pointer ownership on release");
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, release);
		RequireUI(button.RuntimeClickSerial == clickSerialBefore + 1,
			"display UI update did not dispatch one click after fixed preparation");

		TomCat::RuntimeUISystem::PrepareFixedInputCaptureWithInput(
			*fixture.Scene, 1920, 1080, 96.0f, {});
		RequireUI(!TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"display UI update requeued an interaction already seen by fixed input");
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});

		// If several display frames pass without a physics tick, their UI capture
		// must follow ScriptEngine's accumulated fixed edge batch.
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, press);
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, release);
		RequireUI(button.RuntimeClickSerial == clickSerialBefore + 2,
			"no-fixed display frames did not complete their UI click");
		TomCat::RuntimeUISystem::PrepareFixedInputCaptureWithInput(
			*fixture.Scene, 1920, 1080, 96.0f, {});
		RequireUI(TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"UI capture from no-fixed display frames was lost before physics");
		TomCat::RuntimeUISystem::PrepareFixedInputCaptureWithInput(
			*fixture.Scene, 1920, 1080, 96.0f, {});
		RequireUI(TomCat::RuntimeUISystem::IsGameplayInputCaptured()
			&& button.RuntimeClickSerial == clickSerialBefore + 2,
			"accumulated UI capture was not stable across catch-up substeps");
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});
		TomCat::RuntimeUISystem::PrepareFixedInputCaptureWithInput(
			*fixture.Scene, 1920, 1080, 96.0f, {});
		RequireUI(!TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"consumed accumulated UI capture survived into another fixed frame");
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});

		fixture.Canvas.GetComponent<TomCat::UIEventSystem>()
			.ConsumeGameplayInput = false;
		TomCat::RuntimeUISystem::PrepareFixedInputCaptureWithInput(
			*fixture.Scene, 1920, 1080, 96.0f, press);
		RequireUI(!TomCat::RuntimeUISystem::IsGameplayInputCaptured(),
			"fixed input prepass ignored ConsumeGameplayInput=false");
		TomCat::RuntimeUISystem::UpdateWithInput(*fixture.Scene, 1920, 1080,
			96.0f, {});
	}

	void TestSceneAndPrefabRoundTrip()
	{
		UIFixture fixture = BuildUIFixture(TomCat::AssetHandle(4242),
			TomCat::AssetHandle(4343), TomCat::AssetHandle(4244),
			TomCat::AssetHandle(4245));
		fixture.First.GetComponent<TomCat::UIButton>().RuntimeClickSerial = 99;
		std::string document, error;
		RequireUI(TomCat::SceneSerializer(fixture.Scene).SerializeDocument(document,
			error), "Runtime UI Scene 11 serialization failed");
		RequireUI(document.find("TomCat.UIText") != std::string::npos
			&& document.find(PlayChineseUTF8) != std::string::npos,
			"Runtime UI registered components/text are missing from Scene 11");
		auto decoded = TomCat::CreateRef<TomCat::Scene>();
		RequireUI(TomCat::SceneSerializer(decoded).DeserializeDocument(
			std::vector<uint8_t>(document.begin(), document.end()),
			"RuntimeUIRegression.tomcat", false),
			"Runtime UI Scene 11 deserialization failed");
		TomCat::Entity loaded = decoded->FindEntityByUUID(TomCat::UUID(10003));
		RequireUI(loaded && loaded.HasComponent<TomCat::RectTransform>()
			&& loaded.HasComponent<TomCat::UIImage>()
			&& loaded.HasComponent<TomCat::UIText>()
			&& loaded.HasComponent<TomCat::UIButton>()
			&& loaded.GetComponent<TomCat::UIText>().Text == PlayChineseUTF8
			&& loaded.GetComponent<TomCat::UIText>().Font
				== TomCat::AssetHandle(4242)
			&& loaded.GetComponent<TomCat::UIText>().FallbackFont
				== TomCat::AssetHandle(4244)
			&& loaded.GetComponent<TomCat::UIText>().EmojiFont
				== TomCat::AssetHandle(4245)
			&& loaded.GetComponent<TomCat::UIButton>().RuntimeClickSerial == 0,
			"Runtime UI authoring fields or transient reset did not round-trip");

		const YAML::Node root = YAML::Load(document);
		bool sawFont = false, sawFallbackFont = false, sawEmojiFont = false;
		bool sawImage = false;
		RequireUI(TomCat::AssetReferenceVisitor::VisitScene(root,
			[&](const TomCat::SerializedAssetReference& reference)
			{
				sawFont = sawFont || (reference.Kind
					== TomCat::SerializedAssetReferenceKind::Font
					&& static_cast<uint64_t>(reference.Handle) == 4242
					&& reference.ExpectedType == TomCat::AssetType::Font);
				sawFallbackFont = sawFallbackFont || (reference.Kind
					== TomCat::SerializedAssetReferenceKind::Font
					&& static_cast<uint64_t>(reference.Handle) == 4244
					&& reference.ExpectedType == TomCat::AssetType::Font);
				sawEmojiFont = sawEmojiFont || (reference.Kind
					== TomCat::SerializedAssetReferenceKind::Font
					&& static_cast<uint64_t>(reference.Handle) == 4245
					&& reference.ExpectedType == TomCat::AssetType::Font);
				sawImage = sawImage || (reference.Kind
					== TomCat::SerializedAssetReferenceKind::UIImage
					&& static_cast<uint64_t>(reference.Handle) == 4343
					&& reference.ExpectedType == TomCat::AssetType::Texture2D);
				return true;
			}, error) && sawFont && sawFallbackFont && sawEmojiFont && sawImage,
			"Scene/Cook traversal missed a UIText font-chain or UIImage asset");

		TomCat::PrefabArchive archive;
		RequireUI(TomCat::PrefabArchiveCodec::CaptureSubtree(fixture.Scene,
			fixture.Canvas, archive, error), "Runtime UI Prefab capture failed");
		std::string prefabDocument;
		RequireUI(TomCat::PrefabArchiveCodec::Encode(archive, prefabDocument, error),
			"Runtime UI Prefab encode failed");
		TomCat::PrefabArchive decodedPrefab;
		RequireUI(TomCat::PrefabArchiveCodec::Decode(
			std::vector<uint8_t>(prefabDocument.begin(), prefabDocument.end()),
			"RuntimeUIRegression.tcprefab", decodedPrefab, error),
			"Runtime UI Prefab decode failed");
		TomCat::Scene destination;
		TomCat::PrefabInstantiateOptions options;
		options.ResolveAssets = false;
		TomCat::PrefabInstantiationResult instance;
		RequireUI(TomCat::PrefabArchiveCodec::Instantiate(decodedPrefab, destination,
			options, instance, error) && instance.Entities.size() == 4
			&& instance.Root.HasComponent<TomCat::Canvas>()
			&& instance.Root.HasComponent<TomCat::UIEventSystem>(),
			"Runtime UI Prefab did not instantiate its complete hierarchy");
	}

	class TemporaryUIProject final
	{
	public:
		TemporaryUIProject()
		{
			TomCat::AssetManager::Get().Shutdown();
			Root = std::filesystem::temp_directory_path()
				/ ("tomcat_runtime_ui_" + std::to_string(
					static_cast<uint64_t>(TomCat::UUID())));
		}
		~TemporaryUIProject()
		{
			TomCat::AssetManager::Get().Shutdown();
			std::error_code ignored;
			std::filesystem::remove_all(Root, ignored);
		}
		std::filesystem::path Root;
	};

	std::vector<uint8_t> MakeTwoByOneTGA()
	{
		std::vector<uint8_t> bytes(18 + 8, 0);
		bytes[2] = 2;
		bytes[12] = 2;
		bytes[14] = 1;
		bytes[16] = 32;
		bytes[17] = 0x28;
		bytes[18] = 0; bytes[19] = 0; bytes[20] = 255; bytes[21] = 255;
		bytes[22] = 0; bytes[23] = 255; bytes[24] = 0; bytes[25] = 255;
		return bytes;
	}

	TomCat::FontStreamingStats WaitForFontPreparation()
	{
		TomCat::FontManager& fonts = TomCat::FontManager::Get();
		const auto deadline = std::chrono::steady_clock::now()
			+ std::chrono::seconds(10);
		for (;;)
		{
			// A zero publication budget retries the non-blocking backlog without
			// touching Texture2D, so this helper is valid without an OpenGL context.
			(void)fonts.PumpPublishes(0, 0);
			const TomCat::FontStreamingStats stats = fonts.GetStreamingStats();
			if (stats.PreparedCount > 0 && stats.JobsInFlight == 0)
				return stats;
			if (std::chrono::steady_clock::now() >= deadline)
				throw std::runtime_error(
					"timed out waiting for asynchronous font preparation");
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	void TestCookedRuntimeUIRoundTrip()
	{
		TemporaryUIProject environment;
		TomCat::ProjectConfig config;
		config.Name = "Runtime UI Cook Regression";
		config.Template = "2D";
		config.AssetDirectory = "Assets";
		config.StartScene = "Main.tomcat";
		auto project = TomCat::Project::CreateNew(
			environment.Root / "Project.tcproj", config);
		RequireUI(project != nullptr, "could not create Runtime UI Cook project");
		const std::filesystem::path sourceFont = FindRepositoryFile(
			"Editor/TomCatInut/Packages/fonts/opensans/OpenSans-Regular.ttf");
		RequireUI(!sourceFont.empty(), "could not locate Cook font source");
		const std::filesystem::path fontPath = project->GetAssetPath() / "UIFont.ttf";
		const std::filesystem::path fallbackFontPath =
			project->GetAssetPath() / "UIFallback.ttf";
		const std::filesystem::path emojiFontPath =
			project->GetAssetPath() / "UIEmoji.ttf";
		const std::filesystem::path imagePath = project->GetAssetPath() / "Panel.tga";
		const std::vector<uint8_t> fontBytes = ReadBinary(sourceFont);
		WriteBinary(fontPath, fontBytes);
		WriteBinary(fallbackFontPath, DecodeBase64(SyntheticCJKFontBase64));
		WriteBinary(emojiFontPath, DecodeBase64(SyntheticEmojiFontBase64));
		WriteBinary(imagePath, MakeTwoByOneTGA());

		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		RequireUI(assets.SetProject(project),
			"could not initialize assets for Runtime UI Cook");
		const TomCat::AssetMetadata* fontMetadata = assets.Registry().GetMetadata(fontPath);
		const TomCat::AssetMetadata* fallbackFontMetadata =
			assets.Registry().GetMetadata(fallbackFontPath);
		const TomCat::AssetMetadata* emojiFontMetadata =
			assets.Registry().GetMetadata(emojiFontPath);
		const TomCat::AssetMetadata* imageMetadata = assets.Registry().GetMetadata(imagePath);
		RequireUI(fontMetadata && fontMetadata->Type == TomCat::AssetType::Font
			&& fallbackFontMetadata
			&& fallbackFontMetadata->Type == TomCat::AssetType::Font
			&& emojiFontMetadata
			&& emojiFontMetadata->Type == TomCat::AssetType::Font
			&& imageMetadata && imageMetadata->Type == TomCat::AssetType::Texture2D,
			"Runtime UI font-chain/image source types were not imported");
		const TomCat::AssetHandle fontHandle = fontMetadata->Handle;
		const TomCat::AssetHandle fallbackFontHandle = fallbackFontMetadata->Handle;
		const TomCat::AssetHandle emojiFontHandle = emojiFontMetadata->Handle;
		const TomCat::AssetHandle imageHandle = imageMetadata->Handle;
		const TomCat::AssetLoadResult importedFont =
			assets.LoadImportedArtifact(fontHandle);
		RequireUI(importedFont.Succeeded()
			&& importedFont.Artifact.Format == "font/sfnt-v1",
			"SFNT font importer did not publish the expected artifact");

		UIFixture fixture = BuildUIFixture(fontHandle, imageHandle,
			fallbackFontHandle, emojiFontHandle);
		const std::filesystem::path scenePath = project->GetAssetPath() / "Main.tomcat";
		RequireUI(TomCat::SceneSerializer(fixture.Scene).Serialize(scenePath),
			"could not serialize Runtime UI Cook scene");
		const TomCat::AssetMetadata* sceneMetadata = assets.Registry().GetMetadata(scenePath);
		RequireUI(sceneMetadata && sceneMetadata->Type == TomCat::AssetType::Scene,
			"Runtime UI Cook scene was not imported");
		const TomCat::AssetHandle sceneHandle = sceneMetadata->Handle;
		RequireUI(project->SetStartScene("Main.tomcat")
			&& project->SetStartSceneHandle(sceneHandle) && project->Save(),
			"could not configure Runtime UI entry Scene");
		const auto fontReferences = assets.FindReferences(fontHandle);
		const auto fallbackFontReferences = assets.FindReferences(fallbackFontHandle);
		const auto emojiFontReferences = assets.FindReferences(emojiFontHandle);
		const auto imageReferences = assets.FindReferences(imageHandle);
		const auto referencesScene = [&](const auto& references)
		{
			return std::any_of(references.begin(), references.end(),
				[&](const TomCat::AssetReference& reference)
				{ return reference.ReferencingAsset == sceneHandle; });
		};
		RequireUI(referencesScene(fontReferences)
			&& referencesScene(fallbackFontReferences)
			&& referencesScene(emojiFontReferences)
			&& referencesScene(imageReferences),
			"Runtime UI font-chain/image were absent from the reference graph");

		assets.ClearManagedCookPayload();
		const std::filesystem::path packagePath =
			environment.Root / "Build" / "RuntimeUI.tcpak";
		RequireUI(assets.CookToPackage(packagePath),
			"Cook rejected valid Runtime UI Font/UIImage dependencies");
		assets.Shutdown();
		RequireUI(assets.MountCookedPackage(packagePath),
			"could not mount Runtime UI Cook package");
		std::vector<uint8_t> cookedPrimary;
		std::vector<uint8_t> cookedFallback;
		std::vector<uint8_t> cookedEmoji;
		std::vector<uint8_t> cookedImage;
		TomCat::AssetType type = TomCat::AssetType::None;
		RequireUI(assets.ReadAssetBytes(fontHandle, cookedPrimary, &type)
			&& type == TomCat::AssetType::Font && !cookedPrimary.empty()
			&& assets.ReadAssetBytes(fallbackFontHandle, cookedFallback, &type)
			&& type == TomCat::AssetType::Font && !cookedFallback.empty()
			&& assets.ReadAssetBytes(emojiFontHandle, cookedEmoji, &type)
			&& type == TomCat::AssetType::Font && !cookedEmoji.empty()
			&& assets.ReadAssetBytes(imageHandle, cookedImage, &type)
			&& type == TomCat::AssetType::Texture2D && !cookedImage.empty(),
			"Cook omitted a Runtime UI font-chain or image dependency");
		const std::array<std::span<const uint8_t>, 3> cookedFontChain = {
			std::span<const uint8_t>(cookedPrimary),
			std::span<const uint8_t>(cookedFallback),
			std::span<const uint8_t>(cookedEmoji)
		};
		const std::vector<uint32_t> cookedCodepoints =
			TomCat::FontAtlasBuilder::DecodeUTF8(MixedUTF8);
		TomCat::FontAtlasData cookedAtlas;
		RequireUI(TomCat::FontAtlasBuilder::Build(cookedFontChain,
			cookedCodepoints, cookedAtlas),
			"Cooked Player font-chain bytes could not build a glyph atlas");
		const TomCat::FontGlyph* latin = cookedAtlas.Find('T');
		const TomCat::FontGlyph* cjk = cookedAtlas.Find(0x4e2d);
		const TomCat::FontGlyph* emoji = cookedAtlas.Find(0x1f600);
		RequireUI(latin && latin->SourceIndex == 0 && latin->AlphaCoverage > 0
			&& cjk && cjk->SourceIndex == 1 && cjk->AlphaCoverage > 0
			&& emoji && emoji->SourceIndex == 2 && emoji->AlphaCoverage > 0,
			"Cooked Player did not preserve primary/CJK/emoji fallback selection");

		TomCat::FontManager& fonts = TomCat::FontManager::Get();
		fonts.ReleaseAll();
		TomCat::AssetJobSystem& jobs = TomCat::AssetJobSystem::Get();
		const TomCat::AssetJobSystem::Limits previousJobLimits = jobs.GetLimits();
		TomCat::AssetJobSystem::Limits singleWorkerLimits = previousJobLimits;
		singleWorkerLimits.WorkerCount = 1;
		jobs.Configure(singleWorkerLimits);
		RequireUI(!fonts.Load(fontHandle, MixedUTF8, fallbackFontHandle,
			emojiFontHandle),
			"first FontManager load synchronously published an atlas");
		const TomCat::FontStreamingStats preparedStats = WaitForFontPreparation();
		RequireUI(preparedStats.PreparedCount == 1
			&& preparedStats.PreparedBytes > 0
			&& preparedStats.PublishedCount == 0,
			"font preparation touched GL or escaped the bounded prepared queue");
		fonts.Release(fontHandle);
		const TomCat::FontStreamingStats releasedStats = fonts.GetStreamingStats();
		RequireUI(releasedStats.BacklogCount == 0
			&& releasedStats.JobsInFlight == 0
			&& releasedStats.PreparedCount == 0
			&& releasedStats.PreparedBytes == 0
			&& releasedStats.PublishedCount == 0,
			"font release retained prepared or published generation state");
		(void)fonts.Load(fontHandle, MixedUTF8, fallbackFontHandle, emojiFontHandle);
		fonts.ReleaseAll();
		const TomCat::FontStreamingStats cancelledStats = fonts.GetStreamingStats();
		RequireUI(cancelledStats.BacklogCount == 0
			&& cancelledStats.JobsInFlight == 0
			&& cancelledStats.PreparedCount == 0
			&& cancelledStats.PreparedBytes == 0
			&& cancelledStats.PublishedCount == 0,
			"font ReleaseAll did not wait for generation cancellation");
		jobs.Configure(previousJobLimits);

		auto loaded = TomCat::CreateRef<TomCat::Scene>();
		RequireUI(TomCat::SceneSerializer(loaded).Deserialize(sceneHandle),
			"Player path could not deserialize cooked Runtime UI Scene 11");
		TomCat::Entity button = loaded->FindEntityByUUID(TomCat::UUID(10003));
		RequireUI(button && button.HasComponent<TomCat::UIText>()
			&& button.HasComponent<TomCat::UIButton>()
			&& button.GetComponent<TomCat::UIText>().Font == fontHandle
			&& button.GetComponent<TomCat::UIText>().FallbackFont
				== fallbackFontHandle
			&& button.GetComponent<TomCat::UIText>().EmojiFont == emojiFontHandle
			&& button.GetComponent<TomCat::UIText>().Text == PlayChineseUTF8,
			"cooked Player Runtime UI fields did not round-trip");

		TomCat::Entity panel = loaded->FindEntityByUUID(TomCat::UUID(10002));
		TomCat::Entity second = loaded->FindEntityByUUID(TomCat::UUID(10004));
		RequireUI(panel && second, "cooked Player UI screenshot hierarchy is incomplete");
		panel.GetComponent<TomCat::UIImage>().Image = TomCat::AssetHandle(0);
		panel.GetComponent<TomCat::UIImage>().PreserveAspect = false;
		panel.GetComponent<TomCat::UIImage>().Color = {
			32.0f / 255.0f, 64.0f / 255.0f, 176.0f / 255.0f, 1.0f };
		button.GetComponent<TomCat::UIImage>().Color = {
			208.0f / 255.0f, 40.0f / 255.0f, 48.0f / 255.0f, 1.0f };
		second.GetComponent<TomCat::UIImage>().Color = {
			40.0f / 255.0f, 184.0f / 255.0f, 72.0f / 255.0f, 1.0f };
		second.GetComponent<TomCat::UIText>().Alignment = TomCat::TextAlignment::Center;

		struct ScreenshotCase
		{
			uint32_t Width;
			uint32_t Height;
			float DPI;
			uint64_t Golden;
		};
		const ScreenshotCase screenshotCases[] = {
			{ 1920, 1080, 96.0f, 4695793982536209286ull },
			{ 1920, 1080, 144.0f, 11026995505587184344ull },
			{ 1920, 1080, 192.0f, 5128307722957280469ull },
			{ 1440, 1080, 96.0f, 575169338650063814ull },
			{ 1440, 1080, 144.0f, 14558682433443321873ull },
			{ 1440, 1080, 192.0f, 7519808789850018399ull },
			{ 2560, 1080, 96.0f, 5765257838663896052ull },
			{ 2560, 1080, 144.0f, 9642452743191543441ull },
			{ 2560, 1080, 192.0f, 7229357654729139054ull }
		};
		HiddenOpenGLContext context;
		if (!context.IsAvailable())
		{
			std::cout << "SKIP Runtime UI RGBA golden screenshots: "
				<< context.GetUnavailableReason() << std::endl;
			return;
		}
		const std::string screenshotText =
			button.GetComponent<TomCat::UIText>().Text + " "
			+ second.GetComponent<TomCat::UIText>().Text;
		RequireUI(!fonts.Load(fontHandle, "Runtime UI", fallbackFontHandle,
			emojiFontHandle),
			"screenshot font unexpectedly bypassed asynchronous preparation");
		(void)WaitForFontPreparation();
		RequireUI(fonts.PumpPublishes(1,
			32ULL * 1024ULL * 1024ULL) == 1,
			"prepared screenshot font was not published on the GL thread");
		const TomCat::Ref<TomCat::RuntimeFont> originalFont = fonts.Load(
			fontHandle, "Runtime UI", fallbackFontHandle, emojiFontHandle);
		RequireUI(originalFont && originalFont->GetTexture()
			&& !originalFont->GetAtlas().Glyphs.contains(0x1f600u),
			"published base screenshot font was not visible to render calls");
		const TomCat::Ref<TomCat::RuntimeFont> growingFont = fonts.Load(
			fontHandle, screenshotText, fallbackFontHandle, emojiFontHandle);
		RequireUI(growingFont == originalFont && growingFont->GetTexture(),
			"glyph growth discarded the previously published atlas");
		(void)WaitForFontPreparation();
		RequireUI(fonts.Load(fontHandle, screenshotText, fallbackFontHandle,
			emojiFontHandle) == originalFont,
			"prepared glyph growth became visible before main-thread publication");
		RequireUI(fonts.PumpPublishes(1,
			32ULL * 1024ULL * 1024ULL) == 1,
			"prepared glyph growth was not published on the GL thread");
		const TomCat::Ref<TomCat::RuntimeFont> grownFont = fonts.Load(
			fontHandle, screenshotText, fallbackFontHandle, emojiFontHandle);
		RequireUI(grownFont && grownFont != originalFont
			&& grownFont->GetAtlas().Glyphs.contains(0x1f600u)
			&& originalFont->GetTexture()
			&& !originalFont->GetAtlas().Glyphs.contains(0x1f600u),
			"glyph growth did not atomically replace and preserve the old atlas");
		for (const ScreenshotCase& screenshot : screenshotCases)
		{
			const std::vector<uint8_t> pixels = CaptureRuntimeUI(*loaded,
				screenshot.Width, screenshot.Height, screenshot.DPI);
			const uint64_t actual = ScreenshotGoldenHash(pixels,
				screenshot.Width, screenshot.Height);
			if (actual != screenshot.Golden)
			{
				throw std::runtime_error("Runtime UI RGBA golden mismatch for "
					+ std::to_string(screenshot.Width) + "x"
					+ std::to_string(screenshot.Height) + " @ "
					+ std::to_string(screenshot.DPI) + " DPI: expected "
					+ std::to_string(screenshot.Golden) + ", got "
					+ std::to_string(actual));
			}
		}
	}

}

namespace TomCat::Tests {

	void RunRuntimeUIRegression()
	{
		TestUTF8AndDeterministicFontAtlas();
		TestLayoutClippingAspectAndInput();
		TestFixedInputCaptureSnapshot();
		TestSceneAndPrefabRoundTrip();
		TestCookedRuntimeUIRoundTrip();
	}

}
