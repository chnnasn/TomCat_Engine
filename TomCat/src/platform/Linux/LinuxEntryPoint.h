#pragma once
#include "TomCat/Core/ApplicationPaths.h"
#include <cstdlib>
#include <cstring>
#include <vector>

// Linux argv is already UTF-8. Resolve the Editor project before changing the
// resource directory; Player arguments retain the caller's working directory.
int main(int argc, char** argv)
{
#ifdef TC_APPLICATION_PRODUCT
    const auto product = TC_APPLICATION_PRODUCT;
#else
    const auto product = TomCat::ApplicationPaths::IdentifyCurrentExecutable();
#endif
    bool profile = false;
    if (const char* setting = std::getenv("TOMCAT_PROFILE")) {
        const std::string_view value(setting);
        profile = value == "1" || value == "true" || value == "on" || value == "yes";
    }
    std::vector<char*> arguments;
    for (int i = 0; i < argc; ++i) {
        if (i && std::strcmp(argv[i], "--product-info-json") == 0) {
            const auto name = TomCat::ApplicationPaths::GetProductDirectoryName(product);
            std::printf("{\"product\":\"%.*s\",\"version\":\"%.*s\",\"buildId\":\"%.*s\",\"projectFormatOldest\":%u,\"projectFormatCurrent\":%u}\n",
                int(name.size()), name.data(), int(TomCat::Version::ProductVersion.size()), TomCat::Version::ProductVersion.data(),
                int(TomCat::Version::EngineBuildID.size()), TomCat::Version::EngineBuildID.data(),
                TomCat::Version::ProjectFormatOldest, TomCat::Version::ProjectFormatCurrent);
            return 0;
        }
        if (i && std::strcmp(argv[i], "--profile") == 0) profile = true;
        else arguments.push_back(argv[i]);
    }
    const int count = static_cast<int>(arguments.size());
    arguments.push_back(nullptr);
    int exitCode = 70;
    std::string projectPath;
    try {
        if (const auto executable = TomCat::ApplicationPaths::GetExecutablePath()) {
            TomCat::ApplicationPaths::SetRuntimePackageRoot(executable->parent_path());
            if (product == TomCat::ApplicationProduct::Editor && count > 1 && arguments[1][0] != '-') {
                projectPath = std::filesystem::absolute(arguments[1]).string();
                arguments[1] = projectPath.data();
            }
            if (product == TomCat::ApplicationProduct::Editor || product == TomCat::ApplicationProduct::Hub)
                std::filesystem::current_path(executable->parent_path());
        }
        TomCat::Log::Init(product);
        TomCat::CrashReporter::Install(product);
        if (profile) {
            const auto root = TomCat::ApplicationPaths::GetProductDataRoot(product);
            const auto directory = root.value_or(std::filesystem::current_path()) / "Profiles";
            std::filesystem::create_directories(directory);
            TC_PROFILE_BEGIN_SESSION("Application", directory / "TomCatProfile.json");
        }
        TomCat::Scope<TomCat::Application> app(TomCat::CreateApplication({count, arguments.data()}));
        if (!app) throw std::runtime_error("CreateApplication returned null");
        app->Run();
        exitCode = app->GetExitCode();
        app.reset();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "TomCat: %s\n", error.what());
        (void)TomCat::CrashReporter::WriteReport(error.what());
    } catch (...) {
        std::fprintf(stderr, "TomCat: unhandled application error\n");
        (void)TomCat::CrashReporter::WriteReport("Unhandled application error");
    }
    if (profile) TC_PROFILE_END_SESSION();
    TomCat::ApplicationPaths::ClearRuntimeGameDataPaths();
    TomCat::ApplicationPaths::ClearRuntimeEditorRoot();
    TomCat::ApplicationPaths::ClearRuntimePackageRoot();
    TomCat::Log::Shutdown();
    TomCat::CrashReporter::Uninstall();
    return exitCode;
}
