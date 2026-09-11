#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace TomCat {

	// Box2D exposes 16-bit category and mask fields, so project physics layers
	// intentionally use 16 stable slots rather than Unity's 32-slot limit.
	inline constexpr std::size_t Physics2DLayerCount = 16;

	struct TagsAndLayersSettings
	{
		std::vector<std::string> Tags{ "Untagged" };
		std::array<std::string, Physics2DLayerCount> LayerNames{};

		TagsAndLayersSettings()
		{
			LayerNames[0] = "Default";
		}

		bool operator==(const TagsAndLayersSettings&) const = default;
	};

	struct Physics2DSettings
	{
		std::array<uint16_t, Physics2DLayerCount> CollisionMasks{};

		Physics2DSettings()
		{
			CollisionMasks.fill(0xFFFF);
		}

		bool CanLayersCollide(uint8_t layerA, uint8_t layerB) const
		{
			if (layerA >= Physics2DLayerCount || layerB >= Physics2DLayerCount)
				return false;
			return (CollisionMasks[layerA] & (uint16_t(1) << layerB)) != 0;
		}

		void SetLayersCollide(uint8_t layerA, uint8_t layerB, bool enabled)
		{
			if (layerA >= Physics2DLayerCount || layerB >= Physics2DLayerCount)
				return;
			const uint16_t bitA = uint16_t(1) << layerA;
			const uint16_t bitB = uint16_t(1) << layerB;
			if (enabled)
			{
				CollisionMasks[layerA] |= bitB;
				CollisionMasks[layerB] |= bitA;
			}
			else
			{
				CollisionMasks[layerA] &= static_cast<uint16_t>(~bitB);
				CollisionMasks[layerB] &= static_cast<uint16_t>(~bitA);
			}
		}

		bool operator==(const Physics2DSettings&) const = default;
	};

	struct ProjectSettings
	{
		TagsAndLayersSettings TagsAndLayers;
		Physics2DSettings Physics2D;

		bool operator==(const ProjectSettings&) const = default;
	};

}
