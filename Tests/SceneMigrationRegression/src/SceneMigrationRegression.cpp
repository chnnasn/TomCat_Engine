#include <TomCat/Core/Log.h>
#include <TomCat/Scene/Entity.h>
#include <TomCat/Scene/Components.h>
#include <TomCat/Scene/ComponentRegistry.h>
#include <TomCat/Scene/SceneSerializer.h>
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <stdexcept>

int main()
{
    TomCat::Log::Init();
    try
    {
        auto source = TomCat::CreateRef<TomCat::Scene>();
        const auto id = source->CreateEntity("Roundtrip").GetUUID();
        std::string text, error;
        if (!TomCat::SceneSerializer(source).SerializeDocument(text, error))
            throw std::runtime_error(error);
        for (uint32_t version : {0u, 8u, 9u, 10u, 11u, 12u})
        {
            auto document = YAML::Load(text);
            document["SchemaVersion"] = version;
            YAML::Emitter out; out << document;
            const std::string encoded(out.c_str());
            const std::vector<uint8_t> bytes(encoded.begin(), encoded.end());
            auto target = TomCat::CreateRef<TomCat::Scene>();
            const auto sentinel = target->CreateEntity("Keep on failure").GetUUID();
            const bool expected = version == TomCat::SceneSerializer::CurrentSchemaVersion;
            if (TomCat::SceneSerializer::ValidateCurrentFormat(bytes, "version-test") != expected
                || TomCat::SceneSerializer(target).DeserializeDocument(bytes, "version-test", false) != expected)
                throw std::runtime_error("wrong schema acceptance: " + std::to_string(version));
            if (!target->FindEntityByUUID(expected ? id : sentinel))
                throw std::runtime_error("decode violated scene transaction semantics");
        }
        // A registry payload that disagrees with the legacy field must name the
        // entity and print both values, so authors can fix the YAML directly.
        {
            auto scene = TomCat::CreateRef<TomCat::Scene>();
            TomCat::Entity entity = scene->CreateEntity("Mirror Probe");
            entity.AddComponent<TomCat::SpriteRenderer>().TilingFactor = 2.5f;
            YAML::Emitter encoded;
            std::string error;
            encoded << YAML::BeginMap;
            if (!TomCat::ComponentRegistry::Get().EncodeComponents(entity, encoded, error))
                throw std::runtime_error("could not encode components: " + error);
            encoded << YAML::EndMap;
            const YAML::Node components = YAML::Load(encoded.c_str())["Components"];
            entity.GetComponent<TomCat::SpriteRenderer>().TilingFactor = 3.0f;
            if (TomCat::ComponentRegistry::Get().DecodeComponents(entity, components, error))
                throw std::runtime_error("mismatched mirrored payload was accepted");
            for (const char* expected : { "'Mirror Probe'", "TilingFactor",
                "legacy field = 3", "registry payload = 2.5" })
            {
                if (error.find(expected) == std::string::npos)
                    throw std::runtime_error("mismatch diagnostic lacks '"
                        + std::string(expected) + "': " + error);
            }
        }
        std::cout << "PASS current scene roundtrip; unsupported schemas rejected without changing live scene; mirrored-payload mismatches report entity and values\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
