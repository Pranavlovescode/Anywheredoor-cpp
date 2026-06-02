//
// Created by deilsy on 01-06-2026.
//

#include "AgentConfig.h"
#include <random>
#include <toml++/toml.h>
#include <spdlog/spdlog.h>

namespace awd {
    template<typename T>
    static T tomlGet(const toml::table& tbl,
                     std::string_view key,
                     T defaultVal)
        {
            if (auto v = tbl[key].template value<T>())
                return *v;
            return defaultVal;
        }


    std::string AgentConfigLoader::generateAgentId() {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<int64_t> dist;
        uint64_t hi = dist(gen);
        uint64_t lo = dist(gen);

        // Stamp version (4) and variant bits per RFC 4122
        hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

        std::ostringstream oss;
        oss << std::hex << std::setfill('0')
            << std::setw(8)  << ((hi >> 32) & 0xFFFFFFFF) << '-'
            << std::setw(4)  << ((hi >> 16) & 0xFFFF)     << '-'
            << std::setw(4)  << (hi & 0xFFFF)              << '-'
            << std::setw(4)  << ((lo >> 48) & 0xFFFF)      << '-'
            << std::setw(12) << (lo & 0xFFFFFFFFFFFFULL);
        return oss.str();
    }

    WatchEntry AgentConfigLoader::parseWatchEntry(const void *tomlTable) {
        const auto& tbl = *static_cast<const toml::table*>(tomlTable);

        WatchEntry entry;

        // 'path' is mandatory
        auto pathStr = tbl["path"].value<std::string>();
        if (!pathStr)
            throw std::runtime_error("Each [[watch]] entry must have a 'path' field.");

        entry.path      = std::filesystem::absolute(*pathStr);
        entry.recursive = tomlGet(tbl, "recursive", true);

        if (auto arr = tbl["ignore"].as_array()) {
            for (auto& elem : *arr) {
                if (auto s = elem.value<std::string>())
                    entry.ignorePatterns.push_back(*s);
            }
        }

        return entry;
    }

    ServerConfig AgentConfigLoader::parseServerConfig(const void* rawTable)
    {
        const auto& tbl = *static_cast<const toml::table*>(rawTable);

        ServerConfig cfg;
        cfg.host        = tomlGet<std::string>(tbl, "host",         "localhost");
        cfg.port        = tomlGet<uint16_t>   (tbl, "port",         443);
        cfg.useTLS      = tomlGet<bool>       (tbl, "use_tls",      true);
        cfg.apiBasePath = tomlGet<std::string>(tbl, "api_base_path","/api/v1");
        cfg.agentToken  = tomlGet<std::string>(tbl, "agent_token",  "$env");
        // "$env" is a sentinel — resolveSecrets() will replace it with
        // the value of AWD_AGENT_TOKEN environment variable
        return cfg;
    }

    CryptoConfig AgentConfigLoader::parseCryptoConfig(const void* rawTable)
    {
        const auto& tbl = *static_cast<const toml::table*>(rawTable);

        CryptoConfig cfg;
        cfg.keyStorePath       = tomlGet<std::string>(tbl, "key_store_path",  "~/.awd/keys.dat");
        cfg.salt  = tomlGet<std::string>(tbl, "key_derivation_salt", "");
        cfg.zeroKnowledge      = tomlGet<bool>       (tbl, "zero_knowledge",  true);
        cfg.chunkSizeBytes     = tomlGet<uint32_t>   (tbl, "chunk_size_bytes",1 << 20);
        return cfg;
    }

    TransportConfig AgentConfigLoader::parseTransportConfig(const void* rawTable)
    {
        const auto& tbl = *static_cast<const toml::table*>(rawTable);

        TransportConfig cfg;
        cfg.connectTimeoutMs        = tomlGet<uint32_t>(tbl, "connect_timeout_ms",        5000);
        cfg.readTimeoutMs           = tomlGet<uint32_t>(tbl, "read_timeout_ms",           30000);
        cfg.maxRetriesBeforeJournal = tomlGet<uint32_t>(tbl, "max_retries_before_journal",3);
        cfg.retryBackoffBaseMs      = tomlGet<uint32_t>(tbl, "retry_backoff_base_ms",     500);
        cfg.maxBackoffMs            = tomlGet<uint32_t>(tbl, "max_backoff_ms",            30000);
        cfg.preferWebSocket         = tomlGet<bool>    (tbl, "prefer_websocket",          true);
        return cfg;
    }

    LoggingConfig AgentConfigLoader::parseLoggingConfig(const void* rawTable)
    {
        const auto& tbl = *static_cast<const toml::table*>(rawTable);

        LoggingConfig cfg;
        cfg.level          = tomlGet<std::string>(tbl, "level",           "info");
        cfg.filePath       = tomlGet<std::string>(tbl, "file_path",       "");
        cfg.rotateDaily    = tomlGet<bool>       (tbl, "rotate_daily",    true);
        cfg.maxFileSizeMb  = tomlGet<uint32_t>   (tbl, "max_file_size_mb",10);
        return cfg;
    }

    AgentConfig AgentConfigLoader::loadFromFile(const std::filesystem::path& configPath)
    {
        if (!std::filesystem::exists(configPath))
            throw std::runtime_error("Config file not found: " + configPath.string());

        toml::table root;
        try {
            root = toml::parse_file(configPath.string());
        } catch (const toml::parse_error& e) {
            throw std::runtime_error(
                std::string("TOML parse error in ") + configPath.string()
                + " at line " + std::to_string(e.source().begin.line)
                + ": " + std::string(e.description()));
        }

        AgentConfig config;

        // agentId — auto-generate if missing
        config.agentId = tomlGet<std::string>(root, "agent_id", "");
        if (config.agentId.empty()) {
            config.agentId = generateAgentId();
            spdlog::info("[AgentConfig] Generated new agent_id: {}", config.agentId);
        }

        // [[watch]] array of tables
        if (auto arr = root["watch"].as_array()) {
            for (auto& elem : *arr) {
                if (auto* tbl = elem.as_table())
                    config.watchEntries.push_back(parseWatchEntry(tbl));
            }
        }

        // [server], [crypto], [transport], [logging] — optional tables with defaults
        if (auto* tbl = root["server"].as_table())
            config.serverConfig = parseServerConfig(tbl);

        if (auto* tbl = root["crypto"].as_table())
            config.crypto = parseCryptoConfig(tbl);

        if (auto* tbl = root["transport"].as_table())
            config.transportConfig = parseTransportConfig(tbl);

        if (auto* tbl = root["logging"].as_table())
            config.loggingConfig = parseLoggingConfig(tbl);

        resolveSecret(config);
        validate(config);

        spdlog::info("[AgentConfig] Loaded config from {}", configPath.string());
        return config;
    }

    void AgentConfigLoader::saveToFile(const AgentConfig& cfg,
                                   const std::filesystem::path& configPath)
    {
        // Read existing file first to preserve user comments / formatting
        // then only patch the agent_id line. Simple approach: overwrite.
        std::ofstream out(configPath);
        if (!out)
            throw std::runtime_error("Cannot write config: " + configPath.string());

        out << "agent_id = \"" << cfg.agentId << "\"\n\n";

        for (auto& w : cfg.watchEntries) {
            out << "[[watch]]\n";
            out << "path      = \"" << w.path.string() << "\"\n";
            out << "recursive = " << (w.recursive ? "true" : "false") << "\n";
            if (!w.ignorePatterns.empty()) {
                out << "ignore = [";
                for (size_t i = 0; i < w.ignorePatterns.size(); ++i) {
                    out << "\"" << w.ignorePatterns[i] << "\"";
                    if (i + 1 < w.ignorePatterns.size()) out << ", ";
                }
                out << "]\n";
            }
            out << "\n";
        }

        out << "[server]\n"
            << "host           = \"" << cfg.serverConfig.host           << "\"\n"
            << "port           = "   << cfg.serverConfig.port            << "\n"
            << "use_tls        = "   << (cfg.serverConfig.useTLS ? "true":"false") << "\n"
            << "agent_token    = \"$env\"\n\n"; // never write token to disk

        out << "[crypto]\n"
            << "key_store_path      = \"" << cfg.crypto.keyStorePath       << "\"\n"
            << "key_derivation_salt = \"" << cfg.crypto.salt  << "\"\n"
            << "zero_knowledge      = "   << (cfg.crypto.zeroKnowledge ? "true":"false") << "\n"
            << "chunk_size_bytes    = "   << cfg.crypto.chunkSizeBytes      << "\n\n";

        out << "[transport]\n"
            << "connect_timeout_ms         = " << cfg.transportConfig.connectTimeoutMs        << "\n"
            << "read_timeout_ms            = " << cfg.transportConfig.readTimeoutMs           << "\n"
            << "max_retries_before_journal = " << cfg.transportConfig.maxRetriesBeforeJournal << "\n"
            << "retry_backoff_base_ms      = " << cfg.transportConfig.retryBackoffBaseMs      << "\n"
            << "max_backoff_ms             = " << cfg.transportConfig.maxBackoffMs            << "\n"
            << "prefer_websocket           = " << (cfg.transportConfig.preferWebSocket ? "true":"false") << "\n\n";

        out << "[logging]\n"
            << "level            = \"" << cfg.loggingConfig.level         << "\"\n"
            << "file_path        = \"" << cfg.loggingConfig.filePath      << "\"\n"
            << "rotate_daily     = "   << (cfg.loggingConfig.rotateDaily ? "true":"false") << "\n"
            << "max_file_size_mb = "   << cfg.loggingConfig.maxFileSizeMb << "\n";
    }
    void AgentConfigLoader::resolveSecret(AgentConfig& config)
        {
            if (config.serverConfig.agentToken == "$env") {
                const char* tok = std::getenv("AWD_AGENT_TOKEN");
                if (tok)
                    config.serverConfig.agentToken = tok;
                else
                    spdlog::warn("[AgentConfig] AWD_AGENT_TOKEN not set; agent will be unauthenticated.");
            }

            // Expand ~ in paths
            auto expandHome = [](const std::string& p) -> std::string {
                if (p.rfind("~/", 0) == 0) {
                    const char* home = std::getenv("HOME");
    #ifdef AWD_PLATFORM_WINDOWS
                    if (!home) home = std::getenv("USERPROFILE");
    #endif
                    if (home) return std::string(home) + p.substr(1);
                }
                return p;
            };

            // config.crypto.keyStorePath = expandHome(config.crypto.keyStorePath);
            if (!config.loggingConfig.filePath.empty())
                config.loggingConfig.filePath = expandHome(config.loggingConfig.filePath);
        }

    void AgentConfigLoader::validate(const AgentConfig& config)
    {
        if (config.watchEntries.empty())
            throw std::runtime_error("[AgentConfig] No [[watch]] entries defined.");

        for (auto& w : config.watchEntries) {
            if (!std::filesystem::exists(w.path))
                spdlog::warn("[AgentConfig] Watch path does not exist (yet): {}", w.path.string());
            // We warn rather than throw: the directory may be created later (USB mount, etc.)
        }

        if (config.serverConfig.host.empty())
            throw std::runtime_error("[AgentConfig] server.host is empty.");

        if (config.serverConfig.port == 0)
            throw std::runtime_error("[AgentConfig] server.port is 0.");

        const std::vector<std::string> validLevels{"trace","debug","info","warn","error"};
        bool levelOk = false;
        for (auto& l : validLevels)
            if (config.loggingConfig.level == l) { levelOk = true; break; }
        if (!levelOk)
            throw std::runtime_error("[AgentConfig] logging.level must be one of: trace, debug, info, warn, error.");

        if (config.crypto.chunkSizeBytes < 65536)
            throw std::runtime_error("[AgentConfig] crypto.chunk_size_bytes must be >= 65536 (64 KiB).");
    }
}
