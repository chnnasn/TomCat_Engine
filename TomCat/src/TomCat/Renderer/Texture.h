#pragma once

#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <string>

#include "TomCat/Core/Base.h"

namespace TomCat {

	class  Texture
	{
	public:
		virtual ~Texture() = default;

		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
		virtual uint32_t GetRendererID() const = 0;

		virtual void SetData(const void* data, uint32_t size) = 0;

		virtual void Bind(uint32_t slot = 0) const = 0;

		virtual bool IsLoaded() const = 0;

		virtual bool operator==(const Texture& other) const = 0;

	private:

	};

	class Texture2D : public Texture
	{
	public:
		static Ref<Texture2D> Create(uint32_t width, uint32_t height);
		static Ref<Texture2D> Create(const std::filesystem::path& path);
		// Creates a texture from an encoded image held in memory. sourcePath is
		// diagnostic metadata only; the decoder never opens it.
		static Ref<Texture2D> Create(const void* encodedData, size_t encodedSize,
			const std::filesystem::path& sourcePath = {});

		virtual const std::filesystem::path& GetPath() const = 0;
	};
}
