Anywheredoor/
├── CMakeLists.txt                  ← root cmake
|
├── agent/                          ← the C++ local daemon
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp                ← entry point, signal handling
│   │   ├── core/
│   │   │   ├── AgentConfig.h/.cpp  ← TOML/JSON config loader
│   │   │   ├── SyncQueue.h/.cpp    ← priority queue + retry
│   │   │   └── EventLoop.h/.cpp    ← event dispatch hub
│   │   ├── watcher/
│   │   │   ├── IFileWatcher.h      ← abstract interface
│   │   │   ├── LinuxWatcher.cpp    ← inotify
│   │   │   ├── MacWatcher.cpp      ← FSEvents
│   │   │   └── WinWatcher.cpp      ← ReadDirectoryChangesW
│   │   ├── delta/
│   │   │   ├── DeltaEngine.h/.cpp  ← diff + chunking
│   │   │   └── Chunker.h/.cpp      ← content-defined chunking (CDC)
│   │   ├── crypto/
│   │   │   ├── CryptoLayer.h/.cpp  ← AES-256-GCM encrypt/decrypt
│   │   │   ├── KeyManager.h/.cpp   ← ECDH key exchange, key storage
│   │   │   └── HashUtil.h/.cpp     ← SHA-256 integrity checks
│   │   └── transport/
│   │       ├── ITransport.h        ← abstract send/receive
│   │       ├── WebSocketTransport.h/.cpp
│   │       └── HttpTransport.h/.cpp
│   └── tests/
├── server/                         ← optional self-hosted backend (Go / Node is fine too)
│   ├── api/                        ← REST + WebSocket handlers
│   ├── store/                      ← blob + metadata DB layer
│   └── broker/                     ← push notification to dashboard
├── dashboard/                      ← web UI (React / plain HTML)
│   └── src/
├── config/
│   └── agent.toml                  ← watched paths, server URL, keys
├── third_party/
│   ├── openssl/
│   ├── websocketpp/ (or libwebsockets)
│   ├── nlohmann_json/
│   └── spdlog/
└── docs/
└── architecture.md