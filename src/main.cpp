#include "core/AgentConfig.h"
#include <spdlog/spdlog.h>
#include <filesystem>

int main(int argc, char* argv[]) {

    std::filesystem::path cfgPath;

    if (argc > 1) {
        cfgPath = argv[1];
    } else {
        // Default: same directory as the executable
        cfgPath = std::filesystem::current_path() / "agent.toml";
    }

    spdlog::info("AnyWhereDoor agent starting...");
    spdlog::info("Loading config from: {}", cfgPath.string());

    awd::AgentConfig config;
    try {
        config = awd::AgentConfigLoader::loadFromFile(cfgPath);
    } catch (const std::exception& e) {
        spdlog::critical("Failed to load config: {}", e.what());
        return 1;
    }

    // Persist auto-generated agentId back to disk
    try {
        awd::AgentConfigLoader::saveToFile(config, cfgPath);
    } catch (const std::exception& e) {
        spdlog::warn("Could not save config (agentId may not persist): {}", e.what());
    }

    spdlog::info("Agent ID : {}", config.agentId);
    spdlog::info("Server   : {}:{}", config.serverConfig.host, config.serverConfig.port);
    spdlog::info("Watches  : {} path(s) configured", config.watchEntries.size());
    for (auto& w : config.watchEntries)
        spdlog::info("  -> {}", w.path.string());

    // EventLoop and other subsystems will be wired here in Phase 2
    spdlog::info("Config loaded OK. Subsystems not yet implemented — exiting cleanly.");
    return 0;
}