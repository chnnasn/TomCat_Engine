#include "tcpch.h"
#include "ShaderArtifact.h"

#ifndef TC_PLATFORM_WEB
#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_cross.hpp>
#endif

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <tuple>

namespace TomCat {

	namespace {

		constexpr std::array<uint8_t, 4> kMagic = { 'T', 'C', 'S', 'H' };
		constexpr std::array<uint8_t, 4> kSpirvMagic = { 0x03, 0x02, 0x23, 0x07 };
		constexpr uint32_t kVersion = 1;
		constexpr uint32_t kHeaderSize = 36;
		constexpr uint32_t kStageRecordSize = 24;
		constexpr uint32_t kResourceRecordSize = 32;
		constexpr size_t kMaximumSourceBytes = 16 * 1024 * 1024;
		constexpr uint32_t kMaximumResources = 65536;

		struct SourceStage
		{
			ShaderArtifactStage Stage = ShaderArtifactStage::Vertex;
			std::string Source;
		};

		struct CompiledStage
		{
			ShaderArtifactStage Stage = ShaderArtifactStage::Vertex;
			std::vector<uint32_t> Spirv;
		};

		struct ReflectedResource
		{
			ShaderResourceKind Kind = ShaderResourceKind::UniformBuffer;
			uint32_t StageMask = 0;
			uint32_t DescriptorSet = UINT32_MAX;
			uint32_t Binding = UINT32_MAX;
			uint32_t Location = UINT32_MAX;
			uint32_t ArraySize = 1;
			uint32_t ByteSize = 0;
			std::string Name;
		};

		void AppendU32(std::vector<uint8_t>& output, uint32_t value)
		{
			for (uint32_t shift = 0; shift < 32; shift += 8)
				output.push_back(static_cast<uint8_t>(value >> shift));
		}

		void AppendU64(std::vector<uint8_t>& output, uint64_t value)
		{
			for (uint32_t shift = 0; shift < 64; shift += 8)
				output.push_back(static_cast<uint8_t>(value >> shift));
		}

		bool ReadU32(std::span<const uint8_t> bytes, size_t offset, uint32_t& value)
		{
			if (offset > bytes.size() || bytes.size() - offset < 4)
				return false;
			value = 0;
			for (uint32_t index = 0; index < 4; ++index)
				value |= static_cast<uint32_t>(bytes[offset + index]) << (index * 8);
			return true;
		}

		bool ReadU64(std::span<const uint8_t> bytes, size_t offset, uint64_t& value)
		{
			if (offset > bytes.size() || bytes.size() - offset < 8)
				return false;
			value = 0;
			for (uint32_t index = 0; index < 8; ++index)
				value |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8);
			return true;
		}

		std::string Lower(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
			return value;
		}

		std::string_view Trim(std::string_view value)
		{
			while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
				value.remove_prefix(1);
			while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
				value.remove_suffix(1);
			return value;
		}

		bool IsResourceName(std::string_view name)
		{
			if (name.empty() || name.size() > 1023)
				return false;
			return std::none_of(name.begin(), name.end(), [](unsigned char character)
			{
				return character == 0 || character < 0x20 || character == 0x7f;
			});
		}

		bool ParseStageName(std::string_view value, ShaderArtifactStage& stage)
		{
			if (value == "vertex") stage = ShaderArtifactStage::Vertex;
			else if (value == "fragment" || value == "pixel")
				stage = ShaderArtifactStage::Fragment;
			else return false;
			return true;
		}

#ifndef TC_PLATFORM_WEB
		shaderc_shader_kind ShaderKind(ShaderArtifactStage stage)
		{
			return stage == ShaderArtifactStage::Vertex
				? shaderc_glsl_vertex_shader : shaderc_glsl_fragment_shader;
		}
#endif

		bool ParseSourceStages(std::span<const uint8_t> bytes,
			const std::filesystem::path& sourcePath, std::vector<SourceStage>& stages,
			std::string& error)
		{
			stages.clear();
			if (bytes.empty() || bytes.size() > kMaximumSourceBytes
				|| std::find(bytes.begin(), bytes.end(), uint8_t{ 0 }) != bytes.end())
			{
				error = "shader source is empty, oversized, or contains NUL bytes";
				return false;
			}
			std::string source(bytes.begin(), bytes.end());
			if (source.size() >= 3 && static_cast<uint8_t>(source[0]) == 0xef
				&& static_cast<uint8_t>(source[1]) == 0xbb
				&& static_cast<uint8_t>(source[2]) == 0xbf)
				source.erase(0, 3);
			const std::string extension = Lower(sourcePath.extension().string());
			if ((extension == ".vert" || extension == ".frag")
				&& source.find("#type") == std::string::npos)
			{
				stages.push_back({ extension == ".vert" ? ShaderArtifactStage::Vertex
					: ShaderArtifactStage::Fragment, std::move(source) });
				return true;
			}

			struct Marker { ShaderArtifactStage Stage; size_t MarkerOffset; size_t SourceOffset; };
			std::vector<Marker> markers;
			size_t cursor = 0;
			while (cursor <= source.size())
			{
				const size_t end = source.find('\n', cursor);
				const size_t lineEnd = end == std::string::npos ? source.size() : end;
				std::string_view line(source.data() + cursor, lineEnd - cursor);
				line = Trim(line);
				if (line.starts_with("#type"))
				{
					if (line.size() == 5 || !std::isspace(static_cast<unsigned char>(line[5])))
					{
						error = "malformed #type shader declaration";
						return false;
					}
					ShaderArtifactStage stage;
					if (!ParseStageName(Trim(line.substr(5)), stage))
					{
						error = "shader contains an unsupported #type stage";
						return false;
					}
					if (std::any_of(markers.begin(), markers.end(), [&](const Marker& marker)
						{ return marker.Stage == stage; }))
					{
						error = "shader contains a duplicate #type stage";
						return false;
					}
					markers.push_back({ stage, cursor,
						end == std::string::npos ? source.size() : end + 1 });
				}
				if (end == std::string::npos)
					break;
				cursor = end + 1;
			}
			if (markers.empty())
			{
				error = "combined shader source contains no #type declarations";
				return false;
			}
			for (size_t index = 0; index < markers.size(); ++index)
			{
				const size_t end = index + 1 < markers.size()
					? markers[index + 1].MarkerOffset : source.size();
				if (markers[index].SourceOffset >= end
					|| Trim(std::string_view(source).substr(markers[index].SourceOffset,
						end - markers[index].SourceOffset)).empty())
				{
					error = "shader #type stage has no source";
					return false;
				}
				stages.push_back({ markers[index].Stage,
					source.substr(markers[index].SourceOffset,
						end - markers[index].SourceOffset) });
			}
			std::sort(stages.begin(), stages.end(), [](const SourceStage& left,
				const SourceStage& right)
				{ return static_cast<uint32_t>(left.Stage) < static_cast<uint32_t>(right.Stage); });
			return true;
		}

#ifndef TC_PLATFORM_WEB
		uint32_t ArraySize(const spirv_cross::SPIRType& type)
		{
			if (type.array.empty())
				return 1;
			uint64_t count = 1;
			for (size_t index = 0; index < type.array.size(); ++index)
			{
				if (index >= type.array_size_literal.size()
					|| !type.array_size_literal[index] || type.array[index] == 0)
					return 0;
				count *= type.array[index];
				if (count > UINT32_MAX)
					return 0;
			}
			return static_cast<uint32_t>(count);
		}

		uint32_t DecorationOrMax(const spirv_cross::Compiler& compiler,
			const spirv_cross::Resource& resource, spv::Decoration decoration)
		{
			return compiler.has_decoration(resource.id, decoration)
				? compiler.get_decoration(resource.id, decoration) : UINT32_MAX;
		}

		void ReflectList(const spirv_cross::Compiler& compiler,
			const spirv_cross::SmallVector<spirv_cross::Resource>& list,
			ShaderResourceKind kind, ShaderArtifactStage stage,
			std::vector<ReflectedResource>& resources)
		{
			for (const spirv_cross::Resource& item : list)
			{
				ReflectedResource resource;
				resource.Kind = kind;
				resource.StageMask = static_cast<uint32_t>(stage);
				resource.DescriptorSet = DecorationOrMax(compiler, item, spv::DecorationDescriptorSet);
				resource.Binding = DecorationOrMax(compiler, item, spv::DecorationBinding);
				resource.Location = DecorationOrMax(compiler, item, spv::DecorationLocation);
				const spirv_cross::SPIRType& type = compiler.get_type(item.type_id);
				resource.ArraySize = ArraySize(type);
				if (kind == ShaderResourceKind::UniformBuffer
					|| kind == ShaderResourceKind::StorageBuffer
					|| kind == ShaderResourceKind::PushConstant)
				{
					const size_t byteSize = compiler.get_declared_struct_size(type);
					resource.ByteSize = byteSize > UINT32_MAX ? UINT32_MAX
						: static_cast<uint32_t>(byteSize);
				}
				resource.Name = item.name.empty() ? compiler.get_name(item.id) : item.name;
				if (resource.Name.empty())
					resource.Name = "resource_" + std::to_string(item.id);
				resources.push_back(std::move(resource));
			}
		}

		void ReflectStage(const CompiledStage& stage,
			std::vector<ReflectedResource>& output)
		{
			spirv_cross::Compiler compiler(stage.Spirv);
			const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
			ReflectList(compiler, resources.uniform_buffers, ShaderResourceKind::UniformBuffer, stage.Stage, output);
			ReflectList(compiler, resources.storage_buffers, ShaderResourceKind::StorageBuffer, stage.Stage, output);
			ReflectList(compiler, resources.sampled_images, ShaderResourceKind::SampledImage, stage.Stage, output);
			ReflectList(compiler, resources.separate_images, ShaderResourceKind::SeparateImage, stage.Stage, output);
			ReflectList(compiler, resources.separate_samplers, ShaderResourceKind::SeparateSampler, stage.Stage, output);
			ReflectList(compiler, resources.storage_images, ShaderResourceKind::StorageImage, stage.Stage, output);
			ReflectList(compiler, resources.push_constant_buffers, ShaderResourceKind::PushConstant, stage.Stage, output);
			ReflectList(compiler, resources.stage_inputs, ShaderResourceKind::StageInput, stage.Stage, output);
			ReflectList(compiler, resources.stage_outputs, ShaderResourceKind::StageOutput, stage.Stage, output);
			ReflectList(compiler, resources.gl_plain_uniforms, ShaderResourceKind::PlainUniform, stage.Stage, output);
		}

		auto ResourceKey(const ReflectedResource& value)
		{
			return std::tie(value.Kind, value.DescriptorSet, value.Binding, value.Location,
				value.ArraySize, value.ByteSize, value.Name);
		}

#endif
		bool ReadName(std::span<const uint8_t> bytes, uint32_t stringOffset,
			uint32_t stringEnd, uint32_t offset, std::string_view& name)
		{
			if (offset < stringOffset || offset >= stringEnd)
				return false;
			const auto begin = bytes.begin() + offset;
			const auto end = std::find(begin, bytes.begin() + stringEnd, uint8_t{ 0 });
			if (end == bytes.begin() + stringEnd)
				return false;
			name = std::string_view(reinterpret_cast<const char*>(bytes.data() + offset),
				static_cast<size_t>(end - begin));
			return IsResourceName(name);
		}

	}

	bool IsShaderArtifact(std::span<const uint8_t> bytes)
	{
		return bytes.size() >= kMagic.size()
			&& std::equal(kMagic.begin(), kMagic.end(), bytes.begin());
	}

	bool ParseShaderArtifact(std::span<const uint8_t> bytes,
		ShaderArtifactView& artifact, std::string& error)
	{
		artifact = {};
		error.clear();
		uint32_t version = 0, target = 0, stageCount = 0, resourceCount = 0;
		uint32_t stageOffset = 0, resourceOffset = 0, stringOffset = 0, totalSize = 0;
		if (bytes.size() < kHeaderSize || !IsShaderArtifact(bytes)
			|| !ReadU32(bytes, 4, version) || version != kVersion
			|| !ReadU32(bytes, 8, target)
			|| (target != static_cast<uint32_t>(ShaderArtifactTarget::OpenGL)
				&& target != static_cast<uint32_t>(ShaderArtifactTarget::Vulkan))
			|| !ReadU32(bytes, 12, stageCount) || stageCount == 0 || stageCount > 2
			|| !ReadU32(bytes, 16, resourceCount) || resourceCount > kMaximumResources
			|| !ReadU32(bytes, 20, stageOffset) || !ReadU32(bytes, 24, resourceOffset)
			|| !ReadU32(bytes, 28, stringOffset) || !ReadU32(bytes, 32, totalSize)
			|| totalSize != bytes.size())
		{
			error = "shader artifact header is invalid or unsupported";
			return false;
		}
		const uint64_t expectedResources = static_cast<uint64_t>(kHeaderSize)
			+ static_cast<uint64_t>(stageCount) * kStageRecordSize;
		const uint64_t expectedStrings = expectedResources
			+ static_cast<uint64_t>(resourceCount) * kResourceRecordSize;
		if (stageOffset != kHeaderSize || resourceOffset != expectedResources
			|| stringOffset != expectedStrings || stringOffset >= bytes.size())
		{
			error = "shader artifact table layout is invalid";
			return false;
		}

		struct StageRecord
		{
			uint32_t Stage = 0, NameOffset = 0;
			uint64_t CodeOffset = 0, CodeSize = 0;
		};
		std::vector<StageRecord> stageRecords;
		uint64_t firstCode = bytes.size();
		uint32_t previousStage = 0;
		for (uint32_t index = 0; index < stageCount; ++index)
		{
			const size_t recordOffset = stageOffset + static_cast<size_t>(index) * kStageRecordSize;
			StageRecord record;
			if (!ReadU32(bytes, recordOffset, record.Stage)
				|| !ReadU32(bytes, recordOffset + 4, record.NameOffset)
				|| !ReadU64(bytes, recordOffset + 8, record.CodeOffset)
				|| !ReadU64(bytes, recordOffset + 16, record.CodeSize)
				|| record.Stage < static_cast<uint32_t>(ShaderArtifactStage::Vertex)
				|| record.Stage > static_cast<uint32_t>(ShaderArtifactStage::Fragment)
				|| record.Stage <= previousStage || record.CodeSize < 20
				|| record.CodeSize % 4 != 0 || record.CodeOffset % 4 != 0
				|| record.CodeOffset > bytes.size() || record.CodeSize > bytes.size() - record.CodeOffset)
			{
				error = "shader artifact contains an invalid stage record";
				return false;
			}
			previousStage = record.Stage;
			firstCode = (std::min)(firstCode, record.CodeOffset);
			stageRecords.push_back(record);
		}
		if (firstCode <= stringOffset || firstCode > UINT32_MAX)
		{
			error = "shader artifact string table is invalid";
			return false;
		}
		uint64_t expectedCode = firstCode;
		for (const StageRecord& record : stageRecords)
		{
			if (record.CodeOffset != expectedCode
				|| !std::equal(kSpirvMagic.begin(), kSpirvMagic.end(),
					bytes.begin() + static_cast<size_t>(record.CodeOffset)))
			{
				error = "shader artifact contains invalid or non-contiguous SPIR-V";
				return false;
			}
			expectedCode += record.CodeSize;
		}
		if (expectedCode != bytes.size())
		{
			error = "shader artifact has trailing or missing stage data";
			return false;
		}

		artifact.Target = static_cast<ShaderArtifactTarget>(target);
		for (const StageRecord& record : stageRecords)
		{
			ShaderArtifactStageView stage;
			stage.Stage = static_cast<ShaderArtifactStage>(record.Stage);
			if (!ReadName(bytes, stringOffset, static_cast<uint32_t>(firstCode),
				record.NameOffset, stage.EntryPoint) || stage.EntryPoint != "main")
			{
				error = "shader artifact stage entry point is invalid";
				artifact = {};
				return false;
			}
			stage.Spirv = bytes.subspan(static_cast<size_t>(record.CodeOffset),
				static_cast<size_t>(record.CodeSize));
			artifact.Stages.push_back(stage);
		}
		for (uint32_t index = 0; index < resourceCount; ++index)
		{
			const size_t record = resourceOffset + static_cast<size_t>(index) * kResourceRecordSize;
			uint32_t kind = 0, nameOffset = 0;
			ShaderResourceView resource;
			if (!ReadU32(bytes, record, kind)
				|| kind < static_cast<uint32_t>(ShaderResourceKind::UniformBuffer)
				|| kind > static_cast<uint32_t>(ShaderResourceKind::PlainUniform)
				|| !ReadU32(bytes, record + 4, resource.StageMask)
				|| resource.StageMask == 0 || (resource.StageMask & ~3U) != 0
				|| !ReadU32(bytes, record + 8, resource.DescriptorSet)
				|| !ReadU32(bytes, record + 12, resource.Binding)
				|| !ReadU32(bytes, record + 16, resource.Location)
				|| !ReadU32(bytes, record + 20, resource.ArraySize)
				|| !ReadU32(bytes, record + 24, resource.ByteSize)
				|| !ReadU32(bytes, record + 28, nameOffset)
				|| !ReadName(bytes, stringOffset, static_cast<uint32_t>(firstCode),
					nameOffset, resource.Name))
			{
				error = "shader artifact contains an invalid reflection record";
				artifact = {};
				return false;
			}
			resource.Kind = static_cast<ShaderResourceKind>(kind);
			artifact.Resources.push_back(resource);
		}
		return true;
	}

	bool BuildShaderArtifact(std::span<const uint8_t> source,
		const std::filesystem::path& sourcePath,
		const AssetImportSettings& settings, std::string_view backend,
		std::vector<uint8_t>& artifact, std::string& error)
	{
#ifdef TC_PLATFORM_WEB
		artifact.clear();
		error = "Offline shader compilation requires the desktop cooker";
		return false;
#else
		artifact.clear();
		error.clear();
		std::vector<SourceStage> sourceStages;
		if (!ParseSourceStages(source, sourcePath, sourceStages, error))
			return false;

		const std::string normalizedBackend = Lower(std::string(backend));
		ShaderArtifactTarget target;
		if (normalizedBackend.empty() || normalizedBackend == "opengl")
			target = ShaderArtifactTarget::OpenGL;
		else if (normalizedBackend == "vulkan")
			target = ShaderArtifactTarget::Vulkan;
		else
		{
			error = "shader backend is unsupported; expected opengl or vulkan";
			return false;
		}
		bool optimize = true;
		bool warningsAsErrors = false;
		for (const auto& [key, value] : settings)
		{
			if (key != "optimize" && key != "warningsAsErrors")
			{
				error = "shader import setting '" + key + "' is unsupported";
				return false;
			}
			const std::string normalized = Lower(value);
			if (normalized != "true" && normalized != "false")
			{
				error = "shader import setting '" + key + "' must be true or false";
				return false;
			}
			if (key == "optimize") optimize = normalized == "true";
			else warningsAsErrors = normalized == "true";
		}

		shaderc::Compiler compiler;
		shaderc::CompileOptions options;
		auto configure = [&](shaderc::CompileOptions& configured, bool optimizeCode)
		{
			if (target == ShaderArtifactTarget::OpenGL)
				configured.SetTargetEnvironment(shaderc_target_env_opengl,
					shaderc_env_version_opengl_4_5);
			else
				configured.SetTargetEnvironment(shaderc_target_env_vulkan,
					shaderc_env_version_vulkan_1_2);
			configured.SetAutoBindUniforms(true);
			configured.SetAutoMapLocations(true);
			if (optimizeCode)
				configured.SetOptimizationLevel(shaderc_optimization_level_performance);
			if (warningsAsErrors)
				configured.SetWarningsAsErrors();
		};
		configure(options, optimize);

		std::vector<CompiledStage> compiled;
		std::vector<ReflectedResource> resources;
		try
		{
			for (const SourceStage& stage : sourceStages)
			{
				const shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
					stage.Source, ShaderKind(stage.Stage), "TomCatAsset.glsl", options);
				if (result.GetCompilationStatus() != shaderc_compilation_status_success)
				{
					error = "offline shader compilation failed: " + result.GetErrorMessage();
					return false;
				}
				CompiledStage output;
				output.Stage = stage.Stage;
				output.Spirv.assign(result.cbegin(), result.cend());
				if (output.Spirv.size() < 5 || output.Spirv.front() != 0x07230203U)
				{
					error = "offline shader compiler returned invalid SPIR-V";
					return false;
				}
				if (optimize)
				{
					// Optimization strips OpName records on some shaderc versions. Compile
					// a deterministic unoptimized reflection module while keeping the
					// executable module optimized in the artifact.
					shaderc::CompileOptions reflectionOptions;
					configure(reflectionOptions, false);
					const shaderc::SpvCompilationResult reflectionResult =
						compiler.CompileGlslToSpv(stage.Source, ShaderKind(stage.Stage),
							"TomCatAsset.glsl", reflectionOptions);
					if (reflectionResult.GetCompilationStatus()
						!= shaderc_compilation_status_success)
					{
						error = "offline shader reflection compile failed: "
							+ reflectionResult.GetErrorMessage();
						return false;
					}
					CompiledStage reflection;
					reflection.Stage = stage.Stage;
					reflection.Spirv.assign(reflectionResult.cbegin(), reflectionResult.cend());
					ReflectStage(reflection, resources);
				}
				else
					ReflectStage(output, resources);
				compiled.push_back(std::move(output));
			}
		}
		catch (const std::exception& exception)
		{
			error = std::string("shader reflection failed: ") + exception.what();
			return false;
		}

		std::sort(resources.begin(), resources.end(), [](const ReflectedResource& left,
			const ReflectedResource& right) { return ResourceKey(left) < ResourceKey(right); });
		std::vector<ReflectedResource> merged;
		for (ReflectedResource& resource : resources)
		{
			if (!IsResourceName(resource.Name))
			{
				error = "shader reflection produced an invalid resource name";
				return false;
			}
			if (!merged.empty() && ResourceKey(merged.back()) == ResourceKey(resource))
				merged.back().StageMask |= resource.StageMask;
			else
				merged.push_back(std::move(resource));
		}
		if (merged.size() > kMaximumResources)
		{
			error = "shader reflection exceeds the resource limit";
			return false;
		}

		const uint64_t stringOffset64 = static_cast<uint64_t>(kHeaderSize)
			+ static_cast<uint64_t>(compiled.size()) * kStageRecordSize
			+ static_cast<uint64_t>(merged.size()) * kResourceRecordSize;
		if (stringOffset64 > UINT32_MAX)
		{
			error = "shader artifact tables are too large";
			return false;
		}
		const uint32_t stringOffset = static_cast<uint32_t>(stringOffset64);
		std::vector<uint8_t> strings;
		std::map<std::string, uint32_t> stringOffsets;
		auto intern = [&](const std::string& value)
		{
			const auto found = stringOffsets.find(value);
			if (found != stringOffsets.end()) return found->second;
			const uint32_t offset = stringOffset + static_cast<uint32_t>(strings.size());
			strings.insert(strings.end(), value.begin(), value.end());
			strings.push_back(0);
			stringOffsets.emplace(value, offset);
			return offset;
		};
		const uint32_t entryPointOffset = intern("main");
		for (const ReflectedResource& resource : merged)
			(void)intern(resource.Name);
		while ((stringOffset + strings.size()) % 4 != 0)
			strings.push_back(0);

		uint64_t codeOffset = stringOffset64 + strings.size();
		std::vector<uint64_t> codeOffsets;
		for (const CompiledStage& stage : compiled)
		{
			codeOffsets.push_back(codeOffset);
			codeOffset += static_cast<uint64_t>(stage.Spirv.size()) * sizeof(uint32_t);
		}
		if (codeOffset > UINT32_MAX)
		{
			error = "shader artifact exceeds the 4 GiB format limit";
			return false;
		}

		artifact.reserve(static_cast<size_t>(codeOffset));
		artifact.insert(artifact.end(), kMagic.begin(), kMagic.end());
		AppendU32(artifact, kVersion);
		AppendU32(artifact, static_cast<uint32_t>(target));
		AppendU32(artifact, static_cast<uint32_t>(compiled.size()));
		AppendU32(artifact, static_cast<uint32_t>(merged.size()));
		AppendU32(artifact, kHeaderSize);
		AppendU32(artifact, kHeaderSize + static_cast<uint32_t>(compiled.size()) * kStageRecordSize);
		AppendU32(artifact, stringOffset);
		AppendU32(artifact, static_cast<uint32_t>(codeOffset));
		for (size_t index = 0; index < compiled.size(); ++index)
		{
			AppendU32(artifact, static_cast<uint32_t>(compiled[index].Stage));
			AppendU32(artifact, entryPointOffset);
			AppendU64(artifact, codeOffsets[index]);
			AppendU64(artifact, static_cast<uint64_t>(compiled[index].Spirv.size()) * sizeof(uint32_t));
		}
		for (const ReflectedResource& resource : merged)
		{
			AppendU32(artifact, static_cast<uint32_t>(resource.Kind));
			AppendU32(artifact, resource.StageMask);
			AppendU32(artifact, resource.DescriptorSet);
			AppendU32(artifact, resource.Binding);
			AppendU32(artifact, resource.Location);
			AppendU32(artifact, resource.ArraySize);
			AppendU32(artifact, resource.ByteSize);
			AppendU32(artifact, stringOffsets.at(resource.Name));
		}
		artifact.insert(artifact.end(), strings.begin(), strings.end());
		for (const CompiledStage& stage : compiled)
			for (uint32_t word : stage.Spirv) AppendU32(artifact, word);

		ShaderArtifactView validation;
		if (!ParseShaderArtifact(artifact, validation, error))
		{
			artifact.clear();
			return false;
		}
		return true;
#endif
	}

}
