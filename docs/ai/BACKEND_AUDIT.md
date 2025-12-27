# Backend Capabilities Audit - Phase 3 Planning

**Date**: October 1, 2025
**Purpose**: Identify which UI stubs can be wired to existing backend vs. require new implementation

---

## Executive Summary

**Audit Scope**: Video, Network, Controller subsystems
**Config System**: `VitaChiakiConfig` (vitaki-fork) - minimal, TOML-based
**Alternative System Found**: `VitaRPS5Settings` (vitarps5 code) - comprehensive but unused

**Key Finding**: vitaki-fork uses a minimal config system. Most stubbed UI features have NO backend support.

---

## 1. Config System Analysis

### Current Active System: VitaChiakiConfig

**Location**: `vita/include/config.h`
**Storage**: TOML file at `ux0:data/vita-chiaki/chiaki.toml`

**Supported Fields**:
```c
typedef struct vita_chiaki_config_t {
  int cfg_version;
  char* psn_account_id;                      // ✅ USED IN UI
  bool auto_discovery;                        // ✅ USED IN UI
  ChiakiVideoResolutionPreset resolution;    // ✅ USED IN UI (540p/360p only)
  ChiakiVideoFPSPreset fps;                  // ✅ USED IN UI (30/60)
  VitaChiakiHost* manual_hosts[MAX];
  VitaChiakiHost* registered_hosts[MAX];
  int controller_map_id;                     // ✅ USED IN UI (0-7, 25, 99, 100+)
  bool circle_btn_confirm;                   // ✅ USED IN UI
} VitaChiakiConfig;
```

**What's Missing**:
- ❌ Hardware decode toggle (always on)
- ❌ Video quality settings (aspect ratio, brightness, smoothing)
- ❌ Network settings (connection type, timeout, MTU)
- ❌ Motion controls toggle
- ❌ Touch controls toggle
- ❌ Deadzone/sensitivity settings

### Alternative System: VitaRPS5Settings (UNUSED)

**Location**: `vita/src/core/settings.h`
**Status**: Part of VitaRPS5 codebase but NOT integrated into vitaki-fork

**Full Feature Set**:
```c
typedef struct {
  // Streaming Quality
  VitaRPS5Quality quality_preset;
  uint32_t resolution_width;
  uint32_t resolution_height;
  uint32_t target_fps;
  uint32_t target_bitrate;                   // ⚠️ NOT IN VITAKI
  bool hardware_decode;                      // ⚠️ NOT IN VITAKI

  // Video
  bool hdr_support;                          // ⚠️ NOT IN VITAKI
  bool vsync_enabled;                        // ⚠️ NOT IN VITAKI

  // Network
  bool auto_connect;
  bool wake_on_lan;
  uint32_t mtu_size;                         // ⚠️ NOT IN VITAKI

  // Controller
  bool motion_controls;                      // ⚠️ NOT IN VITAKI
  bool touch_controls;                       // ⚠️ NOT IN VITAKI
  float deadzone_percent;                    // ⚠️ NOT IN VITAKI
  float sensitivity_percent;                 // ⚠️ NOT IN VITAKI
  char button_mapping[32];

  uint32_t settings_version;
  bool settings_loaded;
} VitaRPS5Settings;
```

**Comprehensive API** with setters/getters for all features.

---

## 2. Video Backend Analysis

### Chiaki Video Profile System

**Location**: `vita/src/chiaki/chiaki_session.h`

**Supported Resolutions**:
```c
CHIAKI_VIDEO_RESOLUTION_PRESET_360p = 1  // ✅ Supported
CHIAKI_VIDEO_RESOLUTION_PRESET_540p = 2  // ✅ Supported (default)
CHIAKI_VIDEO_RESOLUTION_PRESET_720p = 3  // ⚠️ May work on PS5
CHIAKI_VIDEO_RESOLUTION_PRESET_1080p = 4 // ❌ Likely too high for Vita
```

**Supported FPS**:
```c
CHIAKI_VIDEO_FPS_PRESET_30 = 30  // ✅ Supported
CHIAKI_VIDEO_FPS_PRESET_60 = 60  // ✅ Supported
```

**Video Profile Structure**:
```c
typedef struct {
  unsigned int width;
  unsigned int height;
  unsigned int max_fps;
  unsigned int bitrate;        // ⚠️ Hardcoded, not configurable
  ChiakiCodec codec;
} ChiakiConnectVideoProfile;
```

### Vita Video Decoder

**Location**: `vita/src/video.c`

**Capabilities**:
- ✅ Hardware H.264 decoding via SceAvcdec
- ✅ Aspect ratio handling (letterbox/pillarbox automatic)
- ❌ NO brightness control
- ❌ NO video smoothing/filtering
- ❌ NO HDR support (Vita hardware limitation)
- ❌ NO vsync toggle (always enabled)

**Scaling Logic**:
- Automatically handles aspect ratio mismatches
- Has stub for `center_region_only` config (line 140) but disabled
- No user-configurable video quality settings

---

## 3. Network Backend Analysis

### Network Subsystem

**Location**: `vita/src/network/`

**Components**:
- `takion.c` - Chiaki's RUDP protocol implementation
- `session_init.c` - Connection establishment
- `session_keepalive.c` - Connection maintenance
- `network_manager.c` - High-level network coordination

### Network Configuration

**Current State**:
- ❌ NO MTU configuration (uses system default)
- ❌ NO timeout configuration (hardcoded in Chiaki)
- ❌ NO connection type selector (auto-negotiated)
- ⚠️ Wake-on-LAN: Exists (`network/wake.c`) but not in config

**Latency Tracking**:
- Session has internal latency stats
- NOT exposed to UI layer
- Would require adding getter to session manager

---

## 4. Controller/Input Backend Analysis

### Controller Mapping System

**Location**: `vita/src/controller.c`

**Comprehensive Mapping System**:
```c
void init_controller_map(VitakiCtrlMapInfo* vcmi, VitakiControllerMapId controller_map_id)
```

**Supported Schemes** (fully implemented):
- ✅ Map 0-7: Various touchscreen/rear touch combinations
- ✅ Map 25: No touchpad mode
- ✅ Map 99: Vitaki custom (rear touch L2/R2)
- ✅ Map 100+: L2/L3 R2/R3 swaps (101, 102, 103, etc.)

**Each scheme defines**:
- L2/R2 sources (touch regions or physical buttons)
- L3/R3 sources (touch regions)
- Touchpad source (front touch center/any)

### Motion Controls

**Vita Capabilities**:
- ✅ Hardware: 3-axis gyroscope + 3-axis accelerometer
- ✅ API: `sceMotionGetState()` available
- ❌ Backend: NOT integrated into Chiaki session
- ❌ Would require significant work to add

### Touch Controls

**Current State**:
- ✅ Touch used for button mapping (L2/R2/L3/R3/Touchpad)
- ✅ Comprehensive touch region detection
- ❌ NO "touchpad as buttons" mode (not in Chiaki protocol)
- ⚠️ Could potentially map touch regions to face buttons

---

## 5. Feature-by-Feature Capability Matrix

### Settings Screen

| Feature | Backend Support | Implementation Effort | Notes |
|---------|----------------|----------------------|-------|
| **Streaming Quality Tab** |
| Resolution (720p/1080p) | ⚠️ Partial | Low | 720p may work, 1080p risky |
| FPS (30/60) | ✅ Full | DONE | Already wired |
| Hardware Decode | ❌ None | N/A | Always on, can't disable |
| Auto Discovery | ✅ Full | DONE | Already wired |
| **Video Settings Tab** |
| Aspect Ratio | ❌ None | High | Would need custom scaling code |
| Brightness | ❌ None | Medium-High | GPU post-processing needed |
| Video Smoothing | ❌ None | High | Would need filtering pipeline |
| **Network Settings Tab** |
| Connection Type | ❌ None | N/A | Auto-negotiated by Chiaki |
| Network Timeout | ❌ None | High | Hardcoded in Chiaki core |
| MTU Size | ❌ None | Medium-High | Would need Takion modification |

### Profile Screen

| Feature | Backend Support | Implementation Effort | Notes |
|---------|----------------|----------------------|-------|
| PSN ID Display | ✅ Full | DONE | Already wired |
| PSN ID Editing | ⚠️ Partial | Medium | Need text input widget |
| Connection Status | ✅ Full | DONE | Already wired |
| Console IP | ✅ Full | DONE | Already wired |
| Real-time Latency | ⚠️ Exists | Low-Medium | Need session stats getter |
| Registration Flow | ✅ Full | Low | Wire to existing registration |

### Controller Screen

| Feature | Backend Support | Implementation Effort | Notes |
|---------|----------------|----------------------|-------|
| Scheme Selection | ✅ Full | DONE | Already wired (0-7, 25, 99, 100+) |
| Map-Specific Layouts | ✅ Full | Low | Just display logic needed |
| Circle Button Confirm | ✅ Full | DONE | Already wired |
| Motion Controls | ❌ None | Very High | Need gyro → session integration |
| Touchpad as Buttons | ❌ None | Medium-High | Protocol doesn't support |

---

## 6. Implementation Priority Recommendations

### HIGH PRIORITY (Low Effort, High Value)

1. **Map-Specific Button Layouts** (Controller Screen)
   - **Effort**: 1-2 hours
   - **Backend**: Fully supported
   - **Action**: Update `draw_controller_mappings_tab()` to show different mappings per scheme

2. **Real-time Latency Display** (Profile Screen)
   - **Effort**: 1-2 hours
   - **Backend**: Data exists in session
   - **Action**: Add getter to session manager, wire to UI

3. **720p Resolution Option** (Settings Screen)
   - **Effort**: 30 minutes
   - **Backend**: Should work for PS5
   - **Action**: Add 720p option to resolution dropdown, test thoroughly

### MEDIUM PRIORITY (Medium Effort, Medium Value)

4. **PSN ID Text Input** (Profile Screen)
   - **Effort**: 2-3 hours
   - **Backend**: Setter exists
   - **Action**: Create simple on-screen keyboard widget

5. **Registration Flow Integration** (Profile Screen)
   - **Effort**: 1-2 hours
   - **Backend**: Exists but separate
   - **Action**: Wire "Register Console" button to registration screen

### LOW PRIORITY (High Effort, Low Value)

6. **Video Quality Settings** (Settings Screen)
   - **Effort**: Very High (4-8 hours)
   - **Backend**: Would need custom implementation
   - **Action**: Mark as future enhancement, disable UI controls

7. **Network Configuration** (Settings Screen)
   - **Effort**: Very High (8+ hours)
   - **Backend**: Would need Chiaki core modifications
   - **Action**: Mark as future enhancement, disable UI controls

8. **Motion Controls** (Controller Screen)
   - **Effort**: Very High (8-12 hours)
   - **Backend**: Need full gyro integration
   - **Action**: Mark as future enhancement, disable UI control

### NOT RECOMMENDED (Hardware/Protocol Limitations)

9. **Hardware Decode Toggle**
   - **Reason**: Always required for Vita performance
   - **Action**: Remove from UI entirely

10. **HDR Support**
    - **Reason**: Vita screen doesn't support HDR
    - **Action**: Remove from UI entirely

11. **Touchpad as Buttons Mode**
    - **Reason**: Chiaki protocol doesn't support
    - **Action**: Remove from UI or mark as unavailable

---

## 7. Recommended Phase 3 Implementation Plan

### Step 1: Quick Wins (2-3 hours)
1. Implement map-specific button layouts
2. Add real-time latency display
3. Test 720p resolution option

### Step 2: Text Input & Registration (2-3 hours)
4. Create simple text input widget
5. Wire PSN ID editing
6. Connect registration flow

### Step 3: UI Cleanup (1 hour)
7. Disable/remove unsupported features:
   - Hardware Decode toggle → Remove entirely
   - Video Settings tab → Disable all controls, add "Coming Soon" text
   - Network Settings tab → Disable all controls, add "Coming Soon" text
   - Motion Controls → Mark as "Requires Backend Enhancement"
   - Touchpad as Buttons → Remove or mark unavailable

### Step 4: Documentation (1 hour)
8. Update Phase 3 docs with implementation status
9. Create feature support matrix for users
10. Mark remaining features as Phase 4 (backend enhancement)

---

## 8. Critical Decisions Required

### Decision 1: VitaRPS5Settings Integration
**Question**: Should we migrate from VitaChiakiConfig to VitaRPS5Settings?

**Pros**:
- Comprehensive settings structure already exists
- Better organized with validation
- Full API for getters/setters
- Future-proof for enhancements

**Cons**:
- Significant refactoring required
- May break existing config files
- VitaRPS5 code not tested with vitaki backend

**Recommendation**: **NO** - Too risky for Phase 3. Keep minimal VitaChiakiConfig, extend only as needed.

### Decision 2: Unsupported Features in UI
**Question**: How to handle features with no backend support?

**Options**:
A. Remove UI controls entirely
B. Disable (gray out) with tooltip explaining
C. Keep functional but add "Not Yet Implemented" message

**Recommendation**: **Option B** - Gray out with clear explanation. Maintains visual consistency while being honest about limitations.

### Decision 3: 720p/1080p Resolution
**Question**: Should we expose higher resolutions?

**Risks**:
- May exceed Vita decoder capabilities
- Could cause crashes or poor performance
- Not thoroughly tested

**Recommendation**: Add 720p as **experimental option** with warning. Do NOT add 1080p.

---

## 9. Backend Modifications Required

**Minimal Approach** (Recommended):
1. Add latency getter to session manager
2. Extend VitaChiakiConfig with:
   ```c
   bool experimental_720p;  // User opt-in for 720p
   ```
3. No other backend changes

**Comprehensive Approach** (Not Recommended for Phase 3):
1. Full migration to VitaRPS5Settings
2. Chiaki core modifications for MTU/timeout
3. Gyroscope integration
4. Video post-processing pipeline

---

## 10. Success Criteria for Phase 3

### Must Have
- ✅ All backend-supported features fully functional
- ✅ All unsupported features clearly marked
- ✅ No crashes or regressions
- ✅ User documentation complete

### Nice to Have
- ✅ Text input for PSN ID
- ✅ Real-time latency display
- ✅ Map-specific controller layouts
- ✅ Registration flow integration

### Out of Scope
- ❌ Video quality enhancements (Phase 4)
- ❌ Network configuration (Phase 4)
- ❌ Motion controls (Phase 4)
- ❌ Custom filtering/post-processing (Phase 4)

---

## 11. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| 720p crashes Vita | Medium | High | Make experimental, add warning |
| Text input bugs | Low | Medium | Thorough testing, simple implementation |
| Config migration issues | Low | Low | Backup existing configs |
| Performance regression | Low | High | Profile before/after, maintain current defaults |

---

## Conclusion

**vitaki-fork backend is minimal by design**. Most UI stubs cannot be wired without significant backend work.

**Phase 3 should focus on**:
1. Implementing 3-5 high-value features with backend support
2. Clearly marking unsupported features
3. Creating foundation for Phase 4 enhancements

**Estimated Phase 3 Time**: 6-8 hours (down from original 10-15 hours estimate)

**Next Step**: Update PHASE3_PLAN.md based on audit findings.
