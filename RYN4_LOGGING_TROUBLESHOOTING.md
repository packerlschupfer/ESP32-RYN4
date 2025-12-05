# RYN4 Logging Troubleshooting Guide

## Issue Description
RYN4 logs are appearing with `[loopTask]` tag instead of `[RYN4]` tag, while MB8ART logs show correctly with `[MB8ART]` tag.

## Possible Causes and Solutions

### 1. Build Flag Propagation
The most likely issue is that `USE_CUSTOM_LOGGER` is not being seen by RYN4 during compilation.

**Check:**
```bash
# Look at the verbose build output
pio run -v | grep -A5 -B5 "RYN4"
```

**Solution:**
Ensure your platformio.ini has global build flags:
```ini
build_flags = 
    -DUSE_CUSTOM_LOGGER  ; This must be in the global build_flags
```

### 2. Include Path Issues
RYN4 might not be finding the LogInterface.h header.

**Check:**
```bash
# Verify LogInterface.h exists in your include path
find . -name "LogInterface.h" -type f
```

**Solution:**
Add to your platformio.ini:
```ini
build_flags = 
    -I include  ; Ensure your project include directory is in the path
```

### 3. Library Compilation Order
Libraries are compiled separately and might not inherit all build flags.

**Solution:**
Force recompilation with clean build:
```bash
pio run --target clean
rm -rf .pio/build
pio run
```

### 4. Direct Serial Output
Check if there's any code calling Serial.printf directly.

**Check in your code:**
```cpp
// Look for any of these in your RYN4-related code:
Serial.printf("...");
Serial.print("...");
Serial.println("...");
```

### 5. Task Name Logging
The `[loopTask]` suggests the log might be coming from the main loop or a task.

**Check:**
- Is there code in your main loop() that's logging RYN4 status?
- Are you using vTaskGetCurrentTaskHandle() or pcTaskGetName() anywhere?

## Debugging Steps

### Step 1: Verify USE_CUSTOM_LOGGER is defined
Add this to your main.cpp temporarily:
```cpp
void setup() {
    Serial.begin(115200);
    
    #ifdef USE_CUSTOM_LOGGER
        Serial.println("USE_CUSTOM_LOGGER is DEFINED");
    #else
        Serial.println("USE_CUSTOM_LOGGER is NOT defined");
    #endif
    
    // Rest of setup...
}
```

### Step 2: Check RYN4 Compilation
Create a test file `test_ryn4_logging.cpp`:
```cpp
#include <RYN4.h>

void testRYN4Logging() {
    #ifdef USE_CUSTOM_LOGGER
        Serial.println("RYN4 sees USE_CUSTOM_LOGGER");
    #else
        Serial.println("RYN4 does NOT see USE_CUSTOM_LOGGER");
    #endif
    
    // This should show with [RYN4] tag
    RYN4_LOG_I("Test log from RYN4");
}
```

### Step 3: Library Dependency Build Flags
MB8ART and RYN4 need to be compiled with the same flags. Try:

```ini
[env:your_env]
lib_ldf_mode = deep+  ; Ensure deep dependency resolution
build_flags = 
    -DUSE_CUSTOM_LOGGER
    
# Force rebuild of specific libraries
lib_deps = 
    RYN4
    MB8ART
```

### Step 4: Check Logger Registration
Ensure Logger is initialized before any library usage:
```cpp
void setup() {
    Serial.begin(115200);
    
    #ifdef USE_CUSTOM_LOGGER
    Logger& logger = Logger::getInstance();
    logger.init(4096);
    logger.setLogLevel(ESP_LOG_INFO);
    logger.enableLogging(true);
    #endif
    
    // Only now initialize RYN4
    ryn4->initialize();
}
```

## Working Example from MB8ART

MB8ART works correctly because:
1. It includes `<LogInterface.h>` when `USE_CUSTOM_LOGGER` is defined
2. It uses `LOG_WRITE` macro which maps to the custom logger
3. The MB8ART tag is properly passed to LOG_WRITE

RYN4 has identical code, so the issue must be environmental.

## Most Likely Solution

Add explicit build flags for library dependencies:

```ini
build_unflags = 
    -std=gnu++11

build_flags = 
    -std=gnu++11
    -DUSE_CUSTOM_LOGGER
    -I include
    -I .pio/libdeps/${PIOENV}/Logger/src  ; Explicit Logger path

lib_ldf_mode = deep+
lib_compat_mode = off  ; Force exact dependency matching
```

## If All Else Fails

1. Check if there's a local copy of RYN4 that's being used instead of the library
2. Verify no #undef USE_CUSTOM_LOGGER anywhere in the code
3. Look for any task creation that might be overriding the log context
4. Check if pcTaskGetName() is being used in any logging wrapper

The fact that logs show `[loopTask]` strongly suggests the logging is happening from the Arduino loop() task context, not from within RYN4 itself.