//
// Created by deilsy on 01-06-2026.
//
#pragma once
#ifndef ANYWHEREDOOR_AGENTCONFIG_H
#define ANYWHEREDOOR_AGENTCONFIG_H
#include <filesystem>
#include <vector>

namespace awd{
    // files to be watched will have these properties
    struct WatchEntry {
        std::filesystem::path path; // absolute path to watch the files
        bool recursive{true}; // watch the subdirectories
        std::vector<std::string> ignorePatterns; // glob patterns: "*.tmp", ".git"
    };


    struct ServerConfig {
        std::string host;  // aws.anywheredoor.com
        uint16_t port{8000}; //for local development 8000 and for hosting it will be 443
        bool useTLS{false}; // for local no tls
        std::string apiBasePath; // /api/v1....
        std::string agentToken; //eykajsdkfjlair98...
    };

    struct TransportConfig {
        uint32_t  connectTimeoutMs{5000};
        uint32_t  readTimeoutMs{30000};
        uint32_t  maxRetriesBeforeJournal{3};
        uint32_t  retryBackoffBaseMs{500};  // doubles each retry (exp backoff)
        uint32_t  maxBackoffMs{30000};
        bool      preferWebSocket{true};    // fall back to HTTPS REST if false/unavailable
    };

    struct LoggingConfig {
        std::string level{"info"};          // trace | debug | info | warn | error
        std::string filePath;               // empty = stdout only
        bool        rotateDaily{true};
        uint32_t    maxFileSizeMb{10};
    };

    struct CryptoConfig {
        std::string  keyStorePath;          // path to encrypted key store file
        std::string  salt;     // hex-encoded salt (stored in config)
        bool         zeroKnowledge{true};   // server never sees plaintext
        uint32_t     chunkSizeBytes{1 << 20}; // 1 MiB default CDC chunk target
    };

    struct AgentConfig {
        std::string agentId;
        std::vector<WatchEntry> watchEntries;
        ServerConfig serverConfig;
        LoggingConfig loggingConfig;
        TransportConfig transportConfig;
        CryptoConfig crypto;
    };




    class AgentConfigLoader {
    public:
        // used static function here later we need not to declare the object since this method will be binded to the class itself
        static AgentConfig loadFromFile(const std::filesystem::path& path);
        static void saveToFile(const AgentConfig &cfg, const std::filesystem::path &configPath);
        static void resolveSecret(awd::AgentConfig& config);
        static void validate(const awd::AgentConfig& config);

    private:
        // used static function here later we need not to declare the object since this method will be binded to the class itself
        static WatchEntry   parseWatchEntry(const void* tomlTable);
        static ServerConfig parseServerConfig(const void* tomlTable);
        static TransportConfig parseTransportConfig(const void* tomlTable);
        static LoggingConfig   parseLoggingConfig(const void* tomlTable);
        static std::string generateAgentId();
        static CryptoConfig parseCryptoConfig(const void* tomlTable);
    };
}

#endif //ANYWHEREDOOR_AGENTCONFIG_H
