# EventLoop Implementation Guide

## Overview

The `EventLoop` is the **central event dispatch hub** of the Anywheredoor agent. It coordinates communications between different subsystems (file watchers, crypto, transport, etc.) by managing an event queue and dispatching events to registered handlers.

## Architecture


### Event Priority

Events are prioritized:

```cpp
enum class EventPriority {
    LOW,               // Background operations
    NORMAL,            // Regular operations
    HIGH,              // User-triggered actions
    CRITICAL           // System-critical events
};
```

### Event Structures

**Base Event:**
```cpp
struct Event {
    EventType type;
    EventPriority priority;
    std::string sourceId;      // e.g., "file_watcher", "transport"
    int64_t timestamp;         // milliseconds since epoch
    std::string payload;       // Serialized additional data (JSON)
};
```

**Specialized Events:**
- `FileEvent`: File system events with path, size, hash
- `SyncEvent`: Sync operations with sync ID and retry tracking
- `TransportEvent`: Transport layer events with error codes

## Usage Examples

### 1. Starting the EventLoop

```cpp
#include "core/EventLoop.h"

int main() {
    event::EventLoop eventLoop;
    eventLoop.schedule();  // schedules the task and adds in the queue
    eventLoop.loop();     // Starts the loop
    // ... post events ...
    
    eventLoop.stop();   // Graceful shutdown
    return 0;
}
```

### 2. Subscribing to File Events

```cpp
// Subscribe to file modification events
auto subscriberId = eventLoop.subscribe(
    event::EventType::FILE_MODIFIED,
    [](const std::shared_ptr<event::Event>& evt) {
        auto fileEvt = std::dynamic_pointer_cast<event::FileEvent>(evt);
        if (fileEvt) {
            spdlog::info("File modified: {}", fileEvt->filePath);
            // Trigger delta computation
        }
    },
    "file_history_watcher"  // Optional subscriber ID
);
```

### 3. Subscribing to All Events (Catch-All)

```cpp
// Subscribe to all events for logging/monitoring
auto loggerId = eventLoop.subscribeAll(
    [](const std::shared_ptr<event::Event>& evt) {
        spdlog::debug("Event type: {} from: {}", 
                     static_cast<int>(evt->type), 
                     evt->sourceId);
    },
    "system_logger"
);
```

### 4. Posting File Events

```cpp
auto fileEvent = std::make_shared<event::FileEvent>();
fileEvent->type = event::EventType::FILE_MODIFIED;
fileEvent->priority = event::EventPriority::NORMAL;
fileEvent->sourceId = "file_watcher";
fileEvent->filePath = "/home/user/documents/file.txt";
fileEvent->fileSize = 1024;
fileEvent->fileHash = "abc123...";

eventLoop.postEvent(fileEvent);
```

### 5. Unsubscribing

```cpp
eventLoop.unsubscribe("file_history_watcher");
```

### 6. Monitoring Queue

```cpp
size_t queueSize = eventLoop.getQueueSize();
spdlog::info("Events in queue: {}", queueSize);

bool running = eventLoop.isRunning();
```

### 7. Waiting for Queue to be Empty (Synchronization)

```cpp
// Useful in tests or graceful shutdown
eventLoop.waitUntilEmpty(5000);  // Wait max 5 seconds
```

## Integration with Other Subsystems

### File Watcher → EventLoop

```
LinuxWatcher/MacWatcher/WinWatcher
    ↓
    postEvent(FileEvent::FILE_CREATED)
    ↓
EventLoop (dispatches to all subscribers)
```

### EventLoop → Delta Engine

```cpp
eventLoop.subscribe(
    event::EventType::FILE_MODIFIED,
    [deltaEngine](const std::shared_ptr<event::Event>& evt) {
        deltaEngine->computeDiff(evt);  // Process delta
    },
    "delta_engine"
);
```

### EventLoop → Crypto Layer

```cpp
eventLoop.subscribe(
    event::EventType::SYNC_REQUEST,
    [cryptoLayer](const std::shared_ptr<event::Event>& evt) {
        cryptoLayer->encryptPayload(evt->payload);  // Encrypt before sending
    },
    "crypto_layer"
);
```

### EventLoop → Transport

```cpp
eventLoop.subscribe(
    event::EventType::SYNC_REQUEST,
    [transport](const std::shared_ptr<event::Event>& evt) {
        transport->sendToServer(evt->payload);  // Send to remote
    },
    "websocket_transport"
);
```

## Thread-Safety

The EventLoop is **fully thread-safe**:

- **Multiple producer threads** can safely call `postEvent()` from different threads (e.g., file watcher thread, config reload thread)
- **Subscription/Unsubscription** is thread-safe
- **Internal synchronization** uses mutexes and condition variables
- **Handler execution** happens in the EventLoop thread (no race conditions)

## Design Patterns

### 1. Priority Dispatch
Events are stored in a **priority queue**, ensuring critical events are processed first:
```
HIGH/CRITICAL → NORMAL → LOW
```

### 2. Type-Specific Subscriptions
Subscribe to **specific event types** for efficiency:
```cpp
eventLoop.subscribe(EventType::FILE_MODIFIED, handler);  // Only called for this type
```

### 3. Catch-All Subscriptions
Subscribe to **all events** for system-wide monitoring:
```cpp
eventLoop.subscribeAll(handler);  // Called for every event
```

### 4. Custom Events
You can create **custom event types** by inheriting from `Event`:
```cpp
struct MyCustomEvent : public event::Event {
    std::string customData;
    MyCustomEvent() : Event(EventType::CUSTOM) {}
};

auto evt = std::make_shared<MyCustomEvent>();
eventLoop.postEvent(evt);
```

## Performance Considerations

1. **Efficient Queue**: Uses `std::priority_queue` for O(log n) insertions
2. **Lock-Free Reading**: Event dispatching happens outside locks
3. **Condition Variables**: Efficient waiting instead of polling
4. **Single Event Thread**: Avoids race conditions in handlers
7. **Subscriber Removal**: Use unsubscribe to prevent memory leaks

## Error Handling

The EventLoop catches and logs all exceptions:

```cpp
try {
    dispatchEvent(event);
} catch (const std::exception& e) {
    spdlog::error("Exception during event dispatch: {}", e.what());
    // Continues processing next event
}
```

**Best practice**: Handlers should also implement try-catch to prevent propagation.

## Lifecycle

```
    start()
       ↓
[EventLoop Thread Running]
  ├─ Wait for events (condition variable)
  ├─ Get highest priority event
  ├─ Dispatch to subscribers
  ├─ Handle exceptions
  └─ Repeat...
       ↓
    stop()
       ↓
[Process remaining events]
[Thread joins]
[Clean shutdown]
```

## Integration with main.cpp

Once you implement other subsystems, integrate like this:

```cpp
int main() {
    auto config = awd::AgentConfigLoader::loadFromFile(cfgPath);
    
    // Create EventLoop and subsystems
    event::EventLoop eventLoop;
    eventLoop.start();
    
    // Create file watcher
    auto watcher = std::make_unique<FileWatcher>(eventLoop);
    watcher->startWatching(config.watchEntries);
    
    // Create other subsystems
    auto deltaEngine = std::make_unique<DeltaEngine>(eventLoop);
    auto cryptoLayer = std::make_unique<CryptoLayer>(eventLoop);
    auto transport = std::make_unique<WebSocketTransport>(eventLoop);
    
    // All components communicate via EventLoop
    // ...
    
    // Graceful shutdown
    eventLoop.stop();
    return 0;
}
```

## Testing

Example unit test:

```cpp
TEST(EventLoopTest, BasicPublishSubscribe) {
    event::EventLoop loop;
    loop.start();
    
    int callCount = 0;
    loop.subscribe(
        event::EventType::FILE_MODIFIED,
        [&callCount](const std::shared_ptr<event::Event>&) {
            callCount++;
        }
    );
    
    auto evt = std::make_shared<event::Event>(event::EventType::FILE_MODIFIED);
    loop.postEvent(evt);
    
    loop.waitUntilEmpty();  // Wait for processing
    EXPECT_EQ(callCount, 1);
    
    loop.stop();
}
```

## See Also

- `AgentConfig.h`: Configuration structures
- `IFileWatcher.h`: File watching interface (to be implemented)
- `DeltaEngine.h`: Diff computation (to be implemented)
- `CryptoLayer.h`: Encryption (to be implemented)
- `ITransport.h`: Network transport (to be implemented)

