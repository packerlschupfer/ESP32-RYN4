# RYN4 Library Feature Request: Direct Relay State Getter

**STATUS: COMPLETED - 2025-01-19**

## Overview
The ESPlan Boiler Controller project uses the RYN4 library for relay control through a Hardware Abstraction Layer (HAL). Currently, there's no direct method to query the current state of individual relays from the RYN4 device, requiring HAL implementations to maintain their own cached state.

## Current Situation
When implementing the HAL adapter for RYN4, we discovered that while the library provides excellent control methods like `setRelayStateVerified()`, there's no corresponding getter method to retrieve the current state of a relay.

### Current Workaround
```cpp
// In RYN4RelayModule.cpp (HAL implementation)
State getState(uint8_t channel) const override {
    if (!initialized || !device || channel >= channelCount) {
        return State::UNKNOWN;
    }
    
    // For RYN4, we need to track state ourselves since the device
    // doesn't provide a direct getter. Return our cached state.
    return lastStates[channel];
}
```

## Feature Request
Add a public method to query the current state of individual relays:

### Proposed API
```cpp
// In RYN4.h
class RYN4 : public QueuedModbusDevice, public IDeviceInstance {
public:
    /**
     * @brief Get the current state of a relay
     * @param relayIndex Relay number (1-8)
     * @return RelayResult<bool> SUCCESS with true=ON, false=OFF
     */
    ryn4::RelayResult<bool> getRelayState(uint8_t relayIndex) const;
    
    /**
     * @brief Get the current state of all relays
     * @return RelayResult<std::vector<bool>> SUCCESS with vector of relay states
     */
    ryn4::RelayResult<std::vector<bool>> getAllRelayStates() const;
};
```

### Implementation Suggestion
The implementation could access the internal relay mappings that are already being maintained:

```cpp
ryn4::RelayResult<bool> RYN4::getRelayState(uint8_t relayIndex) const {
    if (relayIndex < 1 || relayIndex > getRelayCount()) {
        return ryn4::RelayResult<bool>(ryn4::RelayErrorCode::INVALID_RELAY_INDEX, false);
    }
    
    // Access internal relay mapping
    if (relayIndex <= relayMappings.size()) {
        const auto& mapping = relayMappings[relayIndex - 1];
        return ryn4::RelayResult<bool>(ryn4::RelayErrorCode::SUCCESS, mapping.isOn);
    }
    
    return ryn4::RelayResult<bool>(ryn4::RelayErrorCode::INVALID_RELAY_INDEX, false);
}
```

## Benefits
1. **Simplified HAL Implementation**: No need to maintain separate state tracking
2. **Consistency**: Single source of truth for relay states
3. **Thread Safety**: Leverage existing mutex protection in RYN4
4. **Better Debugging**: Direct access to actual device state

## Use Case Example
```cpp
// In HAL implementation
State getState(uint8_t channel) const override {
    if (!initialized || !device || channel >= channelCount) {
        return State::UNKNOWN;
    }
    
    // Direct query to RYN4 device
    auto result = device->getRelayState(channel + 1);  // RYN4 uses 1-based indexing
    
    if (result.isOk()) {
        return result.value ? State::ON : State::OFF;
    }
    
    return State::UNKNOWN;
}
```

## Additional Notes
- The library already maintains relay state internally (in `relayMappings`)
- The getter would be read-only and thread-safe
- This would align with common design patterns where setters have corresponding getters
- Could also be useful for debugging and monitoring applications

## Priority
Medium - Current workaround functions correctly, but direct access would improve code quality and maintainability.

## Implementation Status
✅ **COMPLETED** - The feature has been implemented as requested:
- Added `getRelayState(uint8_t relayIndex)` method returning `RelayResult<bool>`
- Added `getAllRelayStates()` method returning `RelayResult<std::vector<bool>>`
- Both methods are thread-safe with mutex protection
- Full error handling with appropriate error codes
- Documentation updated in README.md and QUICK_START.md
- Example code provided in `examples/RelayStateGetterExample/`
- Test functions added to verify functionality

---
*Requested by: ESPlan Boiler Controller Project*  
*Date: 2025-01-19*  
*Contact: Via GitHub issue or PR*  
*Implemented: 2025-01-19*