#include "tcpch.h"
#include "Advanced2DAuthoringAssets.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <exception>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace TomCat {

	namespace {

		constexpr size_t kMaximumDocumentBytes = 16 * 1024 * 1024;
		constexpr size_t kMaximumFramesOrTiles = 1'000'000;
		constexpr size_t kMaximumControllerItems = 65'536;
		constexpr float kMaximumSampleRate = 10'000.0f;
		constexpr float kMaximumFrameDuration = 3600.0f;
		constexpr float kMaximumGridValue = 1'000'000.0f;
		constexpr int32_t kMaximumCoordinate = 100'000'000;

		bool IsValidName(std::string_view name)
		{
			if (name.empty() || name.size() > 127)
				return false;
			const auto first = static_cast<unsigned char>(name.front());
			const auto last = static_cast<unsigned char>(name.back());
			if (std::isspace(first) || std::isspace(last))
				return false;
			return std::none_of(name.begin(), name.end(), [](unsigned char value)
			{
				return value < 0x20 || value == 0x7f;
			});
		}

		bool HasExactKeys(const YAML::Node& node,
			std::initializer_list<std::string_view> required,
			std::string_view context, std::string& error)
		{
			if (!node || !node.IsMap())
			{
				error = std::string(context) + " must be a map";
				return false;
			}
			std::unordered_set<std::string> seen;
			for (const auto& pair : node)
			{
				if (!pair.first.IsScalar())
				{
					error = std::string(context) + " contains a non-scalar key";
					return false;
				}
				const std::string key = pair.first.Scalar();
				if (!seen.emplace(key).second)
				{
					error = std::string(context) + " contains duplicate key '" + key + "'";
					return false;
				}
				if (std::find(required.begin(), required.end(), key) == required.end())
				{
					error = std::string(context) + " contains unknown key '" + key + "'";
					return false;
				}
			}
			for (const std::string_view key : required)
			{
				if (seen.find(std::string(key)) == seen.end())
				{
					error = std::string(context) + " is missing key '" + std::string(key) + "'";
					return false;
				}
			}
			return true;
		}

		bool ParseDocument(std::string_view document, std::string_view rootName,
			YAML::Node& payload, std::string& error)
		{
			if (document.empty() || document.size() > kMaximumDocumentBytes
				|| document.find('\0') != std::string_view::npos)
			{
				error = "authoring asset is empty, oversized, or contains NUL bytes";
				return false;
			}
			const YAML::Node root = YAML::Load(std::string(document));
			if (!HasExactKeys(root, { rootName }, "$", error))
				return false;
			payload = root[std::string(rootName)];
			if (!payload || !payload.IsMap())
			{
				error = std::string("$.") + std::string(rootName) + " must be a map";
				return false;
			}
			return true;
		}

		bool RequireSequence(const YAML::Node& node, size_t maximum,
			std::string_view context, std::string& error)
		{
			if (!node || !node.IsSequence() || node.size() > maximum)
			{
				error = std::string(context) + " must be an array with at most "
					+ std::to_string(maximum) + " entries";
				return false;
			}
			return true;
		}

		bool ParseVec2(const YAML::Node& node, glm::vec2& value,
			std::string_view context, std::string& error)
		{
			if (!node || !node.IsSequence() || node.size() != 2)
			{
				error = std::string(context) + " must be a two-number array";
				return false;
			}
			value = { node[0].as<float>(), node[1].as<float>() };
			if (!std::isfinite(value.x) || !std::isfinite(value.y))
			{
				error = std::string(context) + " must contain finite numbers";
				return false;
			}
			return true;
		}

		bool ParseIVec2(const YAML::Node& node, glm::ivec2& value,
			std::string_view context, std::string& error)
		{
			if (!node || !node.IsSequence() || node.size() != 2)
			{
				error = std::string(context) + " must be a two-integer array";
				return false;
			}
			value = { node[0].as<int32_t>(), node[1].as<int32_t>() };
			return true;
		}

		void EmitVec2(YAML::Emitter& output, const glm::vec2& value)
		{
			output << YAML::Flow << YAML::BeginSeq << value.x << value.y << YAML::EndSeq;
		}

		void EmitIVec2(YAML::Emitter& output, const glm::ivec2& value)
		{
			output << YAML::Flow << YAML::BeginSeq << value.x << value.y << YAML::EndSeq;
		}

		bool VisitReference(AssetHandle handle, AssetType type,
			AuthoringAssetReferenceKind kind, std::string path,
			const AuthoringAssetReferenceVisitor& visitor, std::string& error)
		{
			AuthoringAssetReference reference;
			reference.Handle = handle;
			reference.ExpectedType = type;
			reference.Kind = kind;
			reference.PropertyPath = std::move(path);
			if (!visitor(reference))
			{
				if (error.empty())
					error = "Asset reference was rejected at " + reference.PropertyPath;
				return false;
			}
			return true;
		}

		bool ValidateVisitor(const AuthoringAssetReferenceVisitor& visitor,
			std::string& error)
		{
			error.clear();
			if (visitor)
				return true;
			error = "Asset reference visitor callback is empty";
			return false;
		}

		const AnimatorParameter* FindParameter(
			const AnimatorControllerAsset& asset, std::string_view name)
		{
			const auto found = std::find_if(asset.Parameters.begin(),
				asset.Parameters.end(), [&](const AnimatorParameter& parameter)
				{
					return parameter.Name == name;
				});
			return found == asset.Parameters.end() ? nullptr : &*found;
		}

		bool IsConditionModeValid(AnimatorParameterType type,
			AnimatorConditionMode mode)
		{
			if (type == AnimatorParameterType::Bool
				|| type == AnimatorParameterType::Trigger)
				return mode == AnimatorConditionMode::If
					|| mode == AnimatorConditionMode::IfNot;
			if (type == AnimatorParameterType::Float)
				return mode == AnimatorConditionMode::Greater
					|| mode == AnimatorConditionMode::Less;
			return mode == AnimatorConditionMode::Greater
				|| mode == AnimatorConditionMode::Less
				|| mode == AnimatorConditionMode::Equals
				|| mode == AnimatorConditionMode::NotEqual;
		}

	}

	bool AnimationClipAssetCodec::Validate(const AnimationClipAsset& asset,
		std::string& error)
	{
		error.clear();
		if (asset.Version != Advanced2DAuthoringAssetSchemaVersion)
		{
			error = "Animation Clip Version must be 1";
			return false;
		}
		if (!IsValidName(asset.Clip.Name))
		{
			error = "Animation Clip Name is empty, too long, or contains invalid whitespace";
			return false;
		}
		if (!std::isfinite(asset.SampleRate) || asset.SampleRate <= 0.0f
			|| asset.SampleRate > kMaximumSampleRate)
		{
			error = "Animation Clip SampleRate must be in the range (0, 10000]";
			return false;
		}
		if (asset.Clip.Frames.size() > kMaximumFramesOrTiles)
		{
			error = "Animation Clip has too many frames";
			return false;
		}
		for (size_t index = 0; index < asset.Clip.Frames.size(); ++index)
		{
			const SpriteAnimationFrame& frame = asset.Clip.Frames[index];
			if (static_cast<uint64_t>(frame.SpriteHandle) == 0)
			{
				error = "Animation Clip Frames[" + std::to_string(index)
					+ "].SpriteHandle must be non-zero";
				return false;
			}
			if (!std::isfinite(frame.DurationSeconds)
				|| frame.DurationSeconds <= 0.0f
				|| frame.DurationSeconds > kMaximumFrameDuration)
			{
				error = "Animation Clip Frames[" + std::to_string(index)
					+ "].DurationSeconds must be in the range (0, 3600]";
				return false;
			}
		}
		return true;
	}

	bool AnimationClipAssetCodec::Encode(const AnimationClipAsset& asset,
		std::string& document, std::string& error)
	{
		document.clear();
		if (!Validate(asset, error))
			return false;
		YAML::Emitter output;
		output << YAML::BeginMap << YAML::Key << "TomCatAnimationClip"
			<< YAML::Value << YAML::BeginMap
			<< YAML::Key << "Version" << YAML::Value << asset.Version
			<< YAML::Key << "Name" << YAML::Value << asset.Clip.Name
			<< YAML::Key << "Loop" << YAML::Value << asset.Clip.Loop
			<< YAML::Key << "SampleRate" << YAML::Value << asset.SampleRate
			<< YAML::Key << "Frames" << YAML::Value << YAML::BeginSeq;
		for (const SpriteAnimationFrame& frame : asset.Clip.Frames)
		{
			output << YAML::BeginMap
				<< YAML::Key << "SpriteHandle" << YAML::Value
				<< static_cast<uint64_t>(frame.SpriteHandle)
				<< YAML::Key << "DurationSeconds" << YAML::Value
				<< frame.DurationSeconds << YAML::EndMap;
		}
		output << YAML::EndSeq << YAML::EndMap << YAML::EndMap;
		if (!output.good())
		{
			error = std::string("Could not encode Animation Clip: ")
				+ output.GetLastError();
			return false;
		}
		document = output.c_str();
		return true;
	}

	bool AnimationClipAssetCodec::Decode(std::string_view document,
		AnimationClipAsset& asset, std::string& error)
	{
		error.clear();
		try
		{
			YAML::Node node;
			if (!ParseDocument(document, "TomCatAnimationClip", node, error)
				|| !HasExactKeys(node,
					{ "Version", "Name", "Loop", "SampleRate", "Frames" },
					"$.TomCatAnimationClip", error))
				return false;
			AnimationClipAsset decoded;
			decoded.Version = node["Version"].as<uint32_t>();
			decoded.Clip.Name = node["Name"].as<std::string>();
			decoded.Clip.Loop = node["Loop"].as<bool>();
			decoded.SampleRate = node["SampleRate"].as<float>();
			const YAML::Node frames = node["Frames"];
			if (!RequireSequence(frames, kMaximumFramesOrTiles,
				"$.TomCatAnimationClip.Frames", error))
				return false;
			decoded.Clip.Frames.reserve(frames.size());
			for (size_t index = 0; index < frames.size(); ++index)
			{
				const std::string path = "$.TomCatAnimationClip.Frames["
					+ std::to_string(index) + "]";
				if (!HasExactKeys(frames[index],
					{ "SpriteHandle", "DurationSeconds" }, path, error))
					return false;
				SpriteAnimationFrame frame;
				frame.SpriteHandle = AssetHandle(
					frames[index]["SpriteHandle"].as<uint64_t>());
				frame.DurationSeconds = frames[index]["DurationSeconds"].as<float>();
				decoded.Clip.Frames.push_back(frame);
			}
			if (!Validate(decoded, error))
				return false;
			asset = std::move(decoded);
			return true;
		}
		catch (const std::exception& exception)
		{
			error = std::string("Could not decode Animation Clip: ") + exception.what();
			return false;
		}
	}

	bool AnimationClipAssetCodec::Decode(std::span<const uint8_t> bytes,
		AnimationClipAsset& asset, std::string& error)
	{
		const std::string_view document = bytes.empty() ? std::string_view{}
			: std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		return Decode(document, asset, error);
	}

	bool AnimationClipAssetCodec::VisitAssetReferences(
		const AnimationClipAsset& asset,
		const AuthoringAssetReferenceVisitor& visitor, std::string& error)
	{
		if (!ValidateVisitor(visitor, error) || !Validate(asset, error))
			return false;
		for (size_t index = 0; index < asset.Clip.Frames.size(); ++index)
		{
			if (!VisitReference(asset.Clip.Frames[index].SpriteHandle,
				AssetType::Texture2D, AuthoringAssetReferenceKind::AnimationFrame,
				"$.TomCatAnimationClip.Frames[" + std::to_string(index)
					+ "].SpriteHandle", visitor, error))
				return false;
		}
		return true;
	}

	bool AnimatorControllerAssetCodec::Validate(
		const AnimatorControllerAsset& asset, std::string& error)
	{
		error.clear();
		if (asset.Version != Advanced2DAuthoringAssetSchemaVersion)
		{
			error = "Animator Controller Version must be 1";
			return false;
		}
		if (asset.Parameters.size() > kMaximumControllerItems
			|| asset.States.size() > kMaximumControllerItems
			|| asset.Transitions.size() > kMaximumControllerItems)
		{
			error = "Animator Controller exceeds its item limit";
			return false;
		}

		std::unordered_set<std::string> parameterNames;
		for (size_t index = 0; index < asset.Parameters.size(); ++index)
		{
			const AnimatorParameter& parameter = asset.Parameters[index];
			if (!IsValidName(parameter.Name)
				|| !parameterNames.emplace(parameter.Name).second)
			{
				error = "Animator Controller has an invalid or duplicate parameter at index "
					+ std::to_string(index);
				return false;
			}
			if (parameter.Type < AnimatorParameterType::Bool
				|| parameter.Type > AnimatorParameterType::Trigger
				|| !std::isfinite(parameter.FloatValue))
			{
				error = "Animator Controller parameter at index "
					+ std::to_string(index) + " has an invalid type or value";
				return false;
			}
		}

		std::unordered_set<std::string> stateNames;
		for (size_t index = 0; index < asset.States.size(); ++index)
		{
			const AnimatorControllerAsset::State& state = asset.States[index];
			if (!IsValidName(state.Name)
				|| static_cast<uint64_t>(state.ClipHandle) == 0
				|| !stateNames.emplace(state.Name).second
				|| !std::isfinite(state.Speed) || state.Speed <= 0.0f)
			{
				error = "Animator Controller has an invalid or duplicate state at index "
					+ std::to_string(index);
				return false;
			}
		}
		if ((!asset.States.empty() && stateNames.find(asset.InitialState)
			== stateNames.end()) || (asset.States.empty() && !asset.InitialState.empty()))
		{
			error = "Animator Controller InitialState must name an existing state";
			return false;
		}

		for (size_t index = 0; index < asset.Transitions.size(); ++index)
		{
			const AnimatorTransition& transition = asset.Transitions[index];
			const std::string prefix = "Animator Controller transition at index "
				+ std::to_string(index);
			if ((!transition.AnyState
					&& stateNames.find(transition.FromState) == stateNames.end())
				|| stateNames.find(transition.ToState) == stateNames.end())
			{
				error = prefix + " references an unknown state";
				return false;
			}
			if (!std::isfinite(transition.ExitTime) || transition.ExitTime < -1.0f
				|| transition.ExitTime > 1.0f)
			{
				error = prefix + " has ExitTime outside [-1, 1]";
				return false;
			}
			if (transition.ExitTime < 0.0f && transition.Conditions.empty())
			{
				error = prefix + " needs an exit time or at least one condition";
				return false;
			}
			if (transition.Conditions.size() > kMaximumControllerItems)
			{
				error = prefix + " has too many conditions";
				return false;
			}
			for (size_t conditionIndex = 0;
				conditionIndex < transition.Conditions.size(); ++conditionIndex)
			{
				const AnimatorCondition& condition =
					transition.Conditions[conditionIndex];
				const AnimatorParameter* parameter = FindParameter(asset,
					condition.Parameter);
				if (!parameter || condition.Mode < AnimatorConditionMode::If
					|| condition.Mode > AnimatorConditionMode::NotEqual
					|| !std::isfinite(condition.Threshold)
					|| !IsConditionModeValid(parameter->Type, condition.Mode))
				{
					error = prefix + " has an invalid condition at index "
						+ std::to_string(conditionIndex);
					return false;
				}
			}
		}
		return true;
	}

	bool AnimatorControllerAssetCodec::Encode(
		const AnimatorControllerAsset& asset, std::string& document,
		std::string& error)
	{
		document.clear();
		if (!Validate(asset, error))
			return false;
		YAML::Emitter output;
		output << YAML::BeginMap << YAML::Key << "TomCatAnimatorController"
			<< YAML::Value << YAML::BeginMap
			<< YAML::Key << "Version" << YAML::Value << asset.Version
			<< YAML::Key << "InitialState" << YAML::Value << asset.InitialState
			<< YAML::Key << "Parameters" << YAML::Value << YAML::BeginSeq;
		for (const AnimatorParameter& parameter : asset.Parameters)
		{
			output << YAML::BeginMap
				<< YAML::Key << "Name" << YAML::Value << parameter.Name
				<< YAML::Key << "Type" << YAML::Value
				<< static_cast<uint32_t>(parameter.Type)
				<< YAML::Key << "BoolValue" << YAML::Value << parameter.BoolValue
				<< YAML::Key << "IntValue" << YAML::Value << parameter.IntValue
				<< YAML::Key << "FloatValue" << YAML::Value << parameter.FloatValue
				<< YAML::EndMap;
		}
		output << YAML::EndSeq << YAML::Key << "States" << YAML::Value
			<< YAML::BeginSeq;
		for (const AnimatorControllerAsset::State& state : asset.States)
		{
			output << YAML::BeginMap
				<< YAML::Key << "Name" << YAML::Value << state.Name
				<< YAML::Key << "ClipHandle" << YAML::Value
				<< static_cast<uint64_t>(state.ClipHandle)
				<< YAML::Key << "Speed" << YAML::Value << state.Speed
				<< YAML::EndMap;
		}
		output << YAML::EndSeq << YAML::Key << "Transitions" << YAML::Value
			<< YAML::BeginSeq;
		for (const AnimatorTransition& transition : asset.Transitions)
		{
			output << YAML::BeginMap
				<< YAML::Key << "FromState" << YAML::Value << transition.FromState
				<< YAML::Key << "ToState" << YAML::Value << transition.ToState
				<< YAML::Key << "AnyState" << YAML::Value << transition.AnyState
				<< YAML::Key << "ExitTime" << YAML::Value << transition.ExitTime
				<< YAML::Key << "Conditions" << YAML::Value << YAML::BeginSeq;
			for (const AnimatorCondition& condition : transition.Conditions)
			{
				output << YAML::BeginMap
					<< YAML::Key << "Parameter" << YAML::Value << condition.Parameter
					<< YAML::Key << "Mode" << YAML::Value
					<< static_cast<uint32_t>(condition.Mode)
					<< YAML::Key << "Threshold" << YAML::Value << condition.Threshold
					<< YAML::EndMap;
			}
			output << YAML::EndSeq << YAML::EndMap;
		}
		output << YAML::EndSeq << YAML::EndMap << YAML::EndMap;
		if (!output.good())
		{
			error = std::string("Could not encode Animator Controller: ")
				+ output.GetLastError();
			return false;
		}
		document = output.c_str();
		return true;
	}

	bool AnimatorControllerAssetCodec::Decode(std::string_view document,
		AnimatorControllerAsset& asset, std::string& error)
	{
		error.clear();
		try
		{
			YAML::Node node;
			if (!ParseDocument(document, "TomCatAnimatorController", node, error)
				|| !HasExactKeys(node, { "Version", "InitialState", "Parameters",
					"States", "Transitions" }, "$.TomCatAnimatorController", error))
				return false;
			AnimatorControllerAsset decoded;
			decoded.Version = node["Version"].as<uint32_t>();
			decoded.InitialState = node["InitialState"].as<std::string>();

			const YAML::Node parameters = node["Parameters"];
			const YAML::Node states = node["States"];
			const YAML::Node transitions = node["Transitions"];
			if (!RequireSequence(parameters, kMaximumControllerItems,
				"$.TomCatAnimatorController.Parameters", error)
				|| !RequireSequence(states, kMaximumControllerItems,
					"$.TomCatAnimatorController.States", error)
				|| !RequireSequence(transitions, kMaximumControllerItems,
					"$.TomCatAnimatorController.Transitions", error))
				return false;

			decoded.Parameters.reserve(parameters.size());
			for (size_t index = 0; index < parameters.size(); ++index)
			{
				const std::string path = "$.TomCatAnimatorController.Parameters["
					+ std::to_string(index) + "]";
				if (!HasExactKeys(parameters[index], { "Name", "Type", "BoolValue",
					"IntValue", "FloatValue" }, path, error))
					return false;
				AnimatorParameter parameter;
				parameter.Name = parameters[index]["Name"].as<std::string>();
				const uint32_t type = parameters[index]["Type"].as<uint32_t>();
				if (type > static_cast<uint32_t>(AnimatorParameterType::Trigger))
				{
					error = path + ".Type is unsupported";
					return false;
				}
				parameter.Type = static_cast<AnimatorParameterType>(type);
				parameter.BoolValue = parameters[index]["BoolValue"].as<bool>();
				parameter.IntValue = parameters[index]["IntValue"].as<int32_t>();
				parameter.FloatValue = parameters[index]["FloatValue"].as<float>();
				decoded.Parameters.push_back(std::move(parameter));
			}

			decoded.States.reserve(states.size());
			for (size_t index = 0; index < states.size(); ++index)
			{
				const std::string path = "$.TomCatAnimatorController.States["
					+ std::to_string(index) + "]";
				if (!HasExactKeys(states[index],
					{ "Name", "ClipHandle", "Speed" },
					path, error))
					return false;
				AnimatorControllerAsset::State state;
				state.Name = states[index]["Name"].as<std::string>();
				state.ClipHandle = AssetHandle(
					states[index]["ClipHandle"].as<uint64_t>());
				state.Speed = states[index]["Speed"].as<float>();
				decoded.States.push_back(std::move(state));
			}

			decoded.Transitions.reserve(transitions.size());
			for (size_t index = 0; index < transitions.size(); ++index)
			{
				const std::string path = "$.TomCatAnimatorController.Transitions["
					+ std::to_string(index) + "]";
				if (!HasExactKeys(transitions[index], { "FromState", "ToState",
					"AnyState", "ExitTime", "Conditions" }, path, error))
					return false;
				AnimatorTransition transition;
				transition.FromState = transitions[index]["FromState"].as<std::string>();
				transition.ToState = transitions[index]["ToState"].as<std::string>();
				transition.AnyState = transitions[index]["AnyState"].as<bool>();
				transition.ExitTime = transitions[index]["ExitTime"].as<float>();
				const YAML::Node conditions = transitions[index]["Conditions"];
				if (!RequireSequence(conditions, kMaximumControllerItems,
					path + ".Conditions", error))
					return false;
				transition.Conditions.reserve(conditions.size());
				for (size_t conditionIndex = 0;
					conditionIndex < conditions.size(); ++conditionIndex)
				{
					const std::string conditionPath = path + ".Conditions["
						+ std::to_string(conditionIndex) + "]";
					if (!HasExactKeys(conditions[conditionIndex],
						{ "Parameter", "Mode", "Threshold" }, conditionPath, error))
						return false;
					AnimatorCondition condition;
					condition.Parameter = conditions[conditionIndex]["Parameter"]
						.as<std::string>();
					const uint32_t mode = conditions[conditionIndex]["Mode"]
						.as<uint32_t>();
					if (mode > static_cast<uint32_t>(AnimatorConditionMode::NotEqual))
					{
						error = conditionPath + ".Mode is unsupported";
						return false;
					}
					condition.Mode = static_cast<AnimatorConditionMode>(mode);
					condition.Threshold = conditions[conditionIndex]["Threshold"]
						.as<float>();
					transition.Conditions.push_back(std::move(condition));
				}
				decoded.Transitions.push_back(std::move(transition));
			}

			if (!Validate(decoded, error))
				return false;
			asset = std::move(decoded);
			return true;
		}
		catch (const std::exception& exception)
		{
			error = std::string("Could not decode Animator Controller: ")
				+ exception.what();
			return false;
		}
	}

	bool AnimatorControllerAssetCodec::Decode(std::span<const uint8_t> bytes,
		AnimatorControllerAsset& asset, std::string& error)
	{
		const std::string_view document = bytes.empty() ? std::string_view{}
			: std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		return Decode(document, asset, error);
	}

	bool AnimatorControllerAssetCodec::VisitAssetReferences(
		const AnimatorControllerAsset& asset,
		const AuthoringAssetReferenceVisitor& visitor, std::string& error)
	{
		if (!ValidateVisitor(visitor, error) || !Validate(asset, error))
			return false;
		for (size_t index = 0; index < asset.States.size(); ++index)
		{
			if (!VisitReference(asset.States[index].ClipHandle,
				AssetType::AnimationClip,
				AuthoringAssetReferenceKind::AnimatorStateMotion,
				"$.TomCatAnimatorController.States[" + std::to_string(index)
					+ "].ClipHandle", visitor, error))
				return false;
		}
		return true;
	}

	bool TilePaletteAssetCodec::Validate(const TilePaletteAsset& asset,
		std::string& error)
	{
		error.clear();
		if (asset.Version != Advanced2DAuthoringAssetSchemaVersion)
		{
			error = "Tile Palette Version must be 1";
			return false;
		}
		if (!std::isfinite(asset.CellSize.x) || !std::isfinite(asset.CellSize.y)
			|| asset.CellSize.x <= 0.0f || asset.CellSize.y <= 0.0f
			|| asset.CellSize.x > kMaximumGridValue
			|| asset.CellSize.y > kMaximumGridValue)
		{
			error = "Tile Palette CellSize must contain values in the range (0, 1000000]";
			return false;
		}
		if (!std::isfinite(asset.CellGap.x) || !std::isfinite(asset.CellGap.y)
			|| std::abs(asset.CellGap.x) > kMaximumGridValue
			|| std::abs(asset.CellGap.y) > kMaximumGridValue)
		{
			error = "Tile Palette CellGap must contain finite bounded values";
			return false;
		}
		if (asset.Tiles.size() > kMaximumFramesOrTiles)
		{
			error = "Tile Palette has too many tiles";
			return false;
		}
		std::unordered_set<uint64_t> coordinates;
		for (size_t index = 0; index < asset.Tiles.size(); ++index)
		{
			const TilePaletteEntry& tile = asset.Tiles[index];
			if (std::abs(static_cast<int64_t>(tile.Coordinate.x)) > kMaximumCoordinate
				|| std::abs(static_cast<int64_t>(tile.Coordinate.y)) > kMaximumCoordinate)
			{
				error = "Tile Palette tile coordinate at index "
					+ std::to_string(index) + " is out of range";
				return false;
			}
			const uint64_t key = (static_cast<uint64_t>(
				static_cast<uint32_t>(tile.Coordinate.x)) << 32)
				| static_cast<uint32_t>(tile.Coordinate.y);
			if (!coordinates.emplace(key).second)
			{
				error = "Tile Palette has a duplicate coordinate at index "
					+ std::to_string(index);
				return false;
			}
			if (static_cast<uint64_t>(tile.SpriteHandle) == 0)
			{
				error = "Tile Palette Tiles[" + std::to_string(index)
					+ "].SpriteHandle must be non-zero";
				return false;
			}
		}
		return true;
	}

	bool TilePaletteAssetCodec::Encode(const TilePaletteAsset& asset,
		std::string& document, std::string& error)
	{
		document.clear();
		if (!Validate(asset, error))
			return false;
		YAML::Emitter output;
		output << YAML::BeginMap << YAML::Key << "TomCatTilePalette"
			<< YAML::Value << YAML::BeginMap
			<< YAML::Key << "Version" << YAML::Value << asset.Version
			<< YAML::Key << "CellSize" << YAML::Value;
		EmitVec2(output, asset.CellSize);
		output << YAML::Key << "CellGap" << YAML::Value;
		EmitVec2(output, asset.CellGap);
		output << YAML::Key << "Tiles" << YAML::Value << YAML::BeginSeq;
		for (const TilePaletteEntry& tile : asset.Tiles)
		{
			output << YAML::BeginMap << YAML::Key << "Coordinate" << YAML::Value;
			EmitIVec2(output, tile.Coordinate);
			output << YAML::Key << "SpriteHandle" << YAML::Value
				<< static_cast<uint64_t>(tile.SpriteHandle) << YAML::EndMap;
		}
		output << YAML::EndSeq << YAML::EndMap << YAML::EndMap;
		if (!output.good())
		{
			error = std::string("Could not encode Tile Palette: ")
				+ output.GetLastError();
			return false;
		}
		document = output.c_str();
		return true;
	}

	bool TilePaletteAssetCodec::Decode(std::string_view document,
		TilePaletteAsset& asset, std::string& error)
	{
		error.clear();
		try
		{
			YAML::Node node;
			if (!ParseDocument(document, "TomCatTilePalette", node, error)
				|| !HasExactKeys(node,
					{ "Version", "CellSize", "CellGap", "Tiles" },
					"$.TomCatTilePalette", error))
				return false;
			TilePaletteAsset decoded;
			decoded.Version = node["Version"].as<uint32_t>();
			if (!ParseVec2(node["CellSize"], decoded.CellSize,
				"$.TomCatTilePalette.CellSize", error)
				|| !ParseVec2(node["CellGap"], decoded.CellGap,
					"$.TomCatTilePalette.CellGap", error))
				return false;
			const YAML::Node tiles = node["Tiles"];
			if (!RequireSequence(tiles, kMaximumFramesOrTiles,
				"$.TomCatTilePalette.Tiles", error))
				return false;
			decoded.Tiles.reserve(tiles.size());
			for (size_t index = 0; index < tiles.size(); ++index)
			{
				const std::string path = "$.TomCatTilePalette.Tiles["
					+ std::to_string(index) + "]";
				if (!HasExactKeys(tiles[index],
					{ "Coordinate", "SpriteHandle" }, path, error))
					return false;
				TilePaletteEntry tile;
				if (!ParseIVec2(tiles[index]["Coordinate"], tile.Coordinate,
					path + ".Coordinate", error))
					return false;
				tile.SpriteHandle = AssetHandle(
					tiles[index]["SpriteHandle"].as<uint64_t>());
				decoded.Tiles.push_back(tile);
			}
			if (!Validate(decoded, error))
				return false;
			asset = std::move(decoded);
			return true;
		}
		catch (const std::exception& exception)
		{
			error = std::string("Could not decode Tile Palette: ") + exception.what();
			return false;
		}
	}

	bool TilePaletteAssetCodec::Decode(std::span<const uint8_t> bytes,
		TilePaletteAsset& asset, std::string& error)
	{
		const std::string_view document = bytes.empty() ? std::string_view{}
			: std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		return Decode(document, asset, error);
	}

	bool TilePaletteAssetCodec::VisitAssetReferences(const TilePaletteAsset& asset,
		const AuthoringAssetReferenceVisitor& visitor, std::string& error)
	{
		if (!ValidateVisitor(visitor, error) || !Validate(asset, error))
			return false;
		for (size_t index = 0; index < asset.Tiles.size(); ++index)
		{
			if (!VisitReference(asset.Tiles[index].SpriteHandle,
				AssetType::Texture2D, AuthoringAssetReferenceKind::TilePaletteEntry,
				"$.TomCatTilePalette.Tiles[" + std::to_string(index)
					+ "].SpriteHandle", visitor, error))
				return false;
		}
		return true;
	}

}
