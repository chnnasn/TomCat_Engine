#include <TomCat/Core/Log.h>
#include <TomCat/Scene/Entity.h>
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
        std::cout << "PASS current scene roundtrip; unsupported schemas rejected without changing live scene\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
