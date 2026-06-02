# EventLoop Implementation - Quick Summary

## What is the EventLoop?

The EventLoop is the **heart** of the Anywheredoor agent. It's like a postal system:

```
📨 Incoming Events (Mail)
    ↓
📦 Priority Sorting (by priority)
    ↓
🔄 Main Event Loop (sorted by priority)
    ↓
👥 Dispatch to Subscribers (all interested handlers)
    ↓
✅ Process complete, wait for next event
```

## The 5 Key Concepts

### 1. **Events** - Things that happen
- File created/modified/deleted
- Sync request received
- Transport became ready
- Etc.

```cpp
auto event = std::make_shared<event::FileEvent>();
event->type = EventType::FILE_MODIFIED;
event->filePath = "/path/to/file.txt";
```

### 2. **Priority Queue** - Importance ordering
Events are processed by priority:
- **CRITICAL** → (processed first)
- **HIGH**
- **NORMAL**
- **LOW** → (processed last)

```cpp
// This event will jump the queue
event->priority = event::EventPriority::CRITICAL;
```

### 3. **Subscribe** - Register interest in events
Components register their interest in specific events:

```cpp
// "I want to know when files change"
eventLoop.subscribe(
    event::EventType::FILE_MODIFIED,
    [](const std::shared_ptr<event::Event>& evt) {
        // Handle the event
    }
);
```

### 4. **Post Event** - Send an event to the queue
When something happens, post it:

```cpp
eventLoop.postEvent(myEvent);  // Thread-safe!
```

### 5. **Dispatch** - Call all interested handlers
The EventLoop automatically calls all subscribed handlers:

```
Event Posted and Waiting
    ↓
EventLoop picks event from queue (highest priority first)
    ↓
Find all subscribers interested in this event type
    ↓
Call handler #1 → Call handler #2 → Call handler #3...
    ↓
Done! Pick next event
```

## How Subsystems Communicate

### Before EventLoop (❌ Tight coupling)
```
FileWatcher ──→ DeltaEngine ──→ CryptoLayer ──→ Transport
              (direct calls)
```
Problem: Each component depends directly on others → Hard to change

### With EventLoop (✅ Loose coupling)
```
FileWatcher
    ├─→ postEvent(FileModified) ──┐
                                  ↓
DeltaEngine                    EventLoop
    └─→ subscribe(FileModified) ──┘
    
CryptoLayer
    └─→ subscribe(SyncRequest) ──┐
                                  ↓
Transport                      EventLoop
    └─→ subscribe(SyncComplete) ──┘
```
Benefit: Components are independent → Easy to add/remove/test

## Code Structure

```
EventLoop.h
├─ EventType enum          ← What kind of events exist
├─ EventPriority enum      ← How important
├─ Event struct            ← Base event with type, priority, timestamp
├─ FileEvent struct        ← for file changes
├─ SyncEvent struct        ← for sync operations
├─ TransportEvent struct   ← for network events
├─ EventHandler type       ← Function signature for handlers
├─ EventSubscriber struct  ← Subscriber info
├─ EventLoop class         ← The main event dispatcher
│   ├─ start()             ← Start background thread
│   ├─ stop()              ← Graceful shutdown
│   ├─ postEvent()         ← Add event to queue
│   ├─ subscribe()         ← Register for specific event type
│   ├─ subscribeAll()      ← Register for all events
│   ├─ unsubscribe()       ← Unregister
│   └─ (private methods)   ← Internal implementation

EventLoop.cpp
├─ Constructor/Destructor
├─ start() implementation
│   └─ Spawns background thread → eventLoopThread()
├─ stop() implementation
│   └─ Graceful shutdown: wait for pending events
├─ postEvent() implementation
│   └─ Thread-safe queue insertion
├─ subscribe() implementation
│   └─ Register handler in map
├─ eventLoopThread() - THE MAIN LOOP
│   ├─ Wait for event (or timeout)
│   ├─ Pop highest priority event
│   ├─ Call dispatchEvent()
│   └─ Repeat until shutdown
└─ dispatchEvent() implementation
    └─ Find all interested subscribers and call their handlers
```

## Simple Usage Flow

```cpp
// 1. Create
event::EventLoop eventLoop;

// 2. Start (spawns background thread)
eventLoop.start();

// 3. Subscribe (register interest)
eventLoop.subscribe(
    EventType::FILE_MODIFIED,
    [](auto evt) { std::cout << "File changed!"; }
);

// 4. Post events (from any thread - thread-safe)
auto evt = std::make_shared<FileEvent>();
evt->type = EventType::FILE_MODIFIED;
evt->filePath = "/file.txt";
eventLoop.postEvent(evt);  // ← Event goes to queue

// The background thread:
// ✓ Gets the event from queue
// ✓ Finds all interested subscribers
// ✓ Calls their handlers
// ✓ Waits for next event

// 5. Stop (graceful shutdown)
eventLoop.stop();  // ← Wait for pending events, join thread
```

## Thread Safety Explained

The EventLoop is **thread-safe** for posting events:

```cpp
// Thread 1: File Watcher
void onFileChanged() {
    eventLoop.postEvent(fileEvent);  // ✅ Thread-safe
}

// Thread 2: Config Reloader
void onConfigChanged() {
    eventLoop.postEvent(configEvent);  // ✅ Thread-safe
}

// Background Thread: Event Dispatcher
// Processes events one by one without any race conditions
```

Why safe? Because:
- Queue is protected by mutex
- Only the background thread reads from queue
- No two threads access the same data without synchronization

## Integration Pattern

```cpp
int main() {
    // Load config
    auto config = AgentConfigLoader::loadFromFile("config.toml");
    
    // Create EventLoop
    event::EventLoop eventLoop;
    eventLoop.start();
    
    // Create subsystems
    auto fileWatcher = new FileWatcher(eventLoop);
    auto deltaEngine = new DeltaEngine(eventLoop);
    auto transport = new Transport(eventLoop);
    
    // File watcher posts: FILE_MODIFIED
    //                   ↓
    // DeltaEngine hears it: computes diff
    //                   ↓
    // DeltaEngine posts: SYNC_REQUEST
    //                   ↓
    // Transport hears it: encrypts & sends
    //                   ↓
    // Server responds: SYNC_COMPLETE
    //                   ↓
    // All subscribers process completion
    
    eventLoop.stop();
    return 0;
}
```

## Memory Management

EventLoop uses **shared_ptr** for events:

```cpp
// You don't need to worry about cleanup
auto event = std::make_shared<FileEvent>();
eventLoop.postEvent(event);
// When event is processed and no subscribers hold it
// → Automatically deleted (RAII)
```

## Performance

- **O(log n)** to post event (priority queue insertion)
- **O(1)** to dispatch event (no searching, just iterate subscribers)
- **Lock-free dispatching** (handlers called outside locks)
- **No polling** (condition variables for efficient waiting)

## Error Handling

EventLoop catches all exceptions:

```cpp
try {
    // Call handler
    handler(event);
} catch (const std::exception& e) {
    spdlog::error("Handler failed: {}", e.what());
    // Continue processing next event
}
```

## Common Patterns

### Pattern 1: Fire & Forget
```cpp
eventLoop.postEvent(event);  // I don't care when it's processed
```

### Pattern 2: Multiple Handlers for One Event
```cpp
eventLoop.subscribe(FileModified, handler1);
eventLoop.subscribe(FileModified, handler2);
// Both will be called when file is modified
```

### Pattern 3: System-Wide Monitoring
```cpp
eventLoop.subscribeAll([](auto evt) {
    logger.log("Event: {}", evt->type);
});
```

### Pattern 4: Synchronization (wait for queue)
```cpp
eventLoop.postEvent(criticalEvent);
eventLoop.waitUntilEmpty(5000);  // Wait max 5 seconds for processing
```

## Next Steps

1. **Implement IFileWatcher** - Reports FILE_CREATED/MODIFIED/DELETED events
2. **Implement DeltaEngine** - Subscribes to FILE_MODIFIED, posts SYNC_REQUEST
3. **Implement CryptoLayer** - Subscribes to SYNC_REQUEST, encrypts payload
4. **Implement ITransport** - Subscribes to encrypted sync, posts SYNC_COMPLETE
5. **Wire together in main.cpp** - Create EventLoop, start subsystems, profit!

## Key Takeaways

✅ **EventLoop** = Central message broker
✅ **Events** = Messages between components  
✅ **Priority Queue** = Importance-based ordering
✅ **Subscribe** = "Please tell me when this happens"
✅ **Post Event** = "This thing happened"
✅ **Thread-safe** = Can post from any thread
✅ **Non-blocking** = Processing happens in background

**Result**: Loosely coupled, highly maintainable, easy to test subsystems!

