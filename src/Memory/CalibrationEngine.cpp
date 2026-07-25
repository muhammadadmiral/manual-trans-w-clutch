// =============================================================================
// CalibrationEngine.cpp
// Interactive calibration state machine.
//
// ── How it works (high level) ─────────────────────────────────────────────────
//  1. Engine OFF  → find all floats near 0.0 in 0x600-0x1800  (RPM decays to 0)
//  2. Engine ON   → keep only those that moved off 0.0 at idle (idle RPM)
//  3. Rev (hold W)→ keep only those that rose ≥30 % above their idle snapshot
//       → surviving 1-6 candidates are most likely RPM fields
//
//  For each RPM candidate:
//     • Clutch = RPM + 12   (hardcoded GTA V struct relationship)
//     • Run SearchGearLayout() to find Gear / NextGear / TopGear / GearRatios
//     • Validate with AreOffsetsSane()
//     • Stop on first candidate that passes
//
// ── SearchGearLayout — two-pass robust search ─────────────────────────────────
//  Pass 1 (pointer-based):
//     Walk the entire CVehicle scan window looking for a pointer that points
//     to readable memory whose first two floats look like gear ratios
//     (finite, |r| in [0.05, 25]).  For each such pointer, test ≥12 possible
//     byte-cluster positions relative to the pointer (not just -8) to locate
//     the NextGear / Gear / TopGear uint8 cluster.
//     This handles both the legacy GTA V layout AND the restructured layout
//     in GTA V Enhanced.
//
//  Pass 2 (direct byte scan):
//     If Pass 1 finds nothing, directly scan for the NextGear/Gear/TopGear
//     pattern in the struct (no pointer involvement).  Uses tighter range for
//     TopGear (2–10) and requires the alignment-padding byte at cluster+1
//     to be 0.  Also requires proximity ≤ 0x800 bytes from the RPM candidate
//     to filter false positives.
// =============================================================================
#include "CalibrationEngine.h"
#include "AOBScanner.h"
#include "../Core/ModLogger.h"
#include "../Vehicle/VehicleData.h"

#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ScriptHookV raw pointer retrieval
#include "../../sdk/inc/main.h"

// ============================================================================
// Module-private state and helpers
// ============================================================================
namespace {

// ── State ─────────────────────────────────────────────────────────────────
CalibrationState      s_state          = CalibrationState::None;
std::vector<uint32_t> s_rpmCandidates; // struct-relative offsets surviving each filter
std::vector<float>    s_idleValues;    // idle RPM snapshot for each candidate
ULONGLONG             s_phaseEnterTick = 0;
std::string           s_lastError;

// ── Sanity helpers ────────────────────────────────────────────────────────
// Mirror of VehicleData::AreOffsetsSane — replicated here to avoid circular
// header dependency (CalibrationEngine.h ← OffsetResolver.h, not VehicleData.h).
static bool OffsetsAreSane(const VehicleOffsets& v) {
    constexpr uint32_t kMin = 0x100, kMax = 0x8000;
    auto inRange = [&](uint32_t x){ return x >= kMin && x < kMax; };
    auto delta   = [](uint32_t a, uint32_t b){ return a > b ? a-b : b-a; };

    if (!inRange(v.Gear) || !inRange(v.NextGear) ||
        !inRange(v.RPM)  || !inRange(v.Clutch))
        return false;
    if (delta(v.Gear, v.NextGear) == 0 || delta(v.Gear, v.NextGear) > 0x20)
        return false;
    if (delta(v.RPM, v.Clutch) == 0 || delta(v.RPM, v.Clutch) > 0x40)
        return false;
    if ((v.RPM & 3) != 0 || (v.Clutch & 3) != 0)
        return false;
    return true;
}

// ── GearRatios validation ─────────────────────────────────────────────────
// Check that a pointer-value looks like a GearRatios float array.
// Only the first two entries (Reverse + 1st) are validated — higher gears
// can be 0.0f on vehicles with fewer gears or uninitialized upper slots.
static bool IsRatiosPlausible(uintptr_t ptr) {
    // Must be a valid user-space address and readable
    if (ptr < 0x10000 || ptr > 0x7FFFFFFFFFFull)
        return false;
    
    // Float arrays must be at least 4-byte aligned. 
    if ((ptr & 3) != 0)
        return false;

    if (!AOBScanner::IsReadable(ptr, 16))
        return false;

    const float* r = reinterpret_cast<const float*>(ptr);
    
    // In GTA V, r[0] is Reverse, r[1] is First Gear, r[2] is Second Gear.
    // We check r[1] and r[2] to avoid issues with Reverse being 0.0f or undefined.
    for (int g = 1; g <= 2; ++g) {
        uint32_t u;
        std::memcpy(&u, &r[g], 4);
        uint32_t exp = (u >> 23) & 0xFF;
        if (exp == 0xFF) return false; // Reject NaN, Inf
        if (exp == 0) return false;    // Reject Denormal and exact 0.0f

        float val = r[g];
        // Forward gear ratios must be strictly positive (usually 1.0 to 5.0)
        if (val < 0.1f || val > 15.0f) return false;
    }
    
    // Verify that First gear ratio is > Second gear ratio (basic mechanics)
    if (r[1] <= r[2]) return false;

    return true;
}

// ── Gear cluster validation ───────────────────────────────────────────────
// Reads NextGear / Gear / TopGear bytes at the given struct offset and
// decides whether they look like valid gear fields during idle calibration.
//
//  base+0 = NextGear  (uint8, expect 0-2)
//  base+2 = Gear      (uint8, expect 0-1 at idle)
//  base+6 = TopGear   (uint8, expect 1-16)
static bool IsGearClusterValid(uintptr_t vehicleBase, uint32_t base,
                               uint8_t& outNext, uint8_t& outGear, uint8_t& outTop) {
    if (!AOBScanner::IsReadable(vehicleBase + base, 8)) return false;

    const uint8_t* mem = reinterpret_cast<const uint8_t*>(vehicleBase + base);
    outNext = mem[0];
    outGear = mem[2];
    outTop  = mem[6];

    return outGear <= 1 && outNext <= 2 && outTop >= 1 && outTop <= 16;
}

// ============================================================================
// SearchGearLayout
// ============================================================================
struct GearCluster {
    uint32_t nextGearOff = 0;
    uint32_t gearOff     = 0;
    uint32_t topGearOff  = 0;
    uint32_t ratiosOff   = 0;
};

static bool SearchGearLayout(uintptr_t vehicleBase,
                             uint32_t  rpmOffset,
                             uint8_t   realMaxGear,
                             GearCluster& out)
{
    const uint32_t scanLo = CalibrationEngine::kScanStart;
    const uint32_t scanHi = CalibrationEngine::kScanEnd;

    // ── Pass 1: GearRatios-pointer-based search ───────────────────────────
    LOG_DEBUG(Calib, "  [SearchGear P1] Scanning 0x%X-0x%X for GearRatios ptr", scanLo, scanHi);

    for (uint32_t ptrOff = scanLo; ptrOff + 8 <= scanHi; ptrOff += 4) {
        if (!AOBScanner::IsReadable(vehicleBase + ptrOff, 8))
            continue;

        const uintptr_t ptr =
            *reinterpret_cast<const uintptr_t*>(vehicleBase + ptrOff);

        if (!IsRatiosPlausible(ptr))
            continue;

        // This looks like a GearRatios pointer! Try a wide range of cluster-position deltas.
        // We search from -256 to +256 around the pointer to find the Gear fields.
        for (int delta = -256; delta <= 256; delta += 2) {
            const int32_t tryBase32 = static_cast<int32_t>(ptrOff) + delta;
            if (tryBase32 < static_cast<int32_t>(scanLo)) continue;
            const uint32_t tryBase = static_cast<uint32_t>(tryBase32);
            if (tryBase + 8 > scanHi) continue;

            uint8_t ng, g, tg;
            if (IsGearClusterValid(vehicleBase, tryBase, ng, g, tg)) {
                // Strict check: only accept if TopGear matches the game's actual maxGear
                if (tg != realMaxGear) continue;

                out.nextGearOff = tryBase;
                out.gearOff     = tryBase + 2;
                out.topGearOff  = tryBase + 6;
                out.ratiosOff   = ptrOff;
                LOG_DEBUG(Calib,
                    "  [P1 HIT] Ptr@0x%X RatiosPtr=0x%llX Cluster@0x%X "
                    "N=%u G=%u Top=%u delta=%d",
                    ptrOff, (unsigned long long)ptr,
                    tryBase, ng, g, tg, delta);
                return true;
            }
        }
    }

    // ── Pass 2: Fallback heuristic scan ───────────────────────────────────────
    // If the exact pointers weren't found, we scan for a byte cluster that
    // looks like NextGear, Gear, TopGear.
    LOG_DEBUG(Calib, "  [SearchGear P2] No ptr found. Direct scan 0x%X-0x%X rpmHint=0x%X", scanLo, scanHi, rpmOffset);

    // Known distances from RPM to NextGear for various GTA V builds:
    // Build 2802+: RPM 0x8C8, NextGear 0x7A0 (dist = 0x128)
    // Build 1180+: RPM 0x808, NextGear 0x7E0 (dist = 0x28)
    // Build 877+:  RPM 0x808, NextGear 0x7D0 (dist = 0x38)
    static const uint32_t kKnownDistances[] = { 0x128, 0x28, 0x38 };

    // Try known structural distances FIRST before doing a blind scan!
    for (uint32_t dist : kKnownDistances) {
        if (rpmOffset < dist) continue;
        uint32_t tryNextGear = rpmOffset - dist;
        
        if (tryNextGear >= scanLo && tryNextGear + 8 <= scanHi) {
            uint8_t ng, g, tg;
            if (IsGearClusterValid(vehicleBase, tryNextGear, ng, g, tg)) {
                // If it looks structurally valid at a known distance, we lock it in immediately.
                // We do NOT strictly check topGear == realMaxGear here because the game often
                // overrides max gear for reverse or highway cruising.
                out.nextGearOff = tryNextGear;
                out.gearOff     = tryNextGear + 2;
                out.topGearOff  = tryNextGear + 6;
                out.ratiosOff   = 0;
                
                LOG_INFO(Calib, "  [P2 KNOWN DIST HIT] Cluster@0x%X matches known offset distance 0x%X from RPM! N=%u G=%u Top=%u", 
                         tryNextGear, dist, ng, g, tg);
                return true;
            }
        }
    }

    // If known distances failed, do a conservative blind scan.
    for (uint32_t gOff = scanLo; gOff + 8 <= scanHi; gOff += 2) {
        if (!AOBScanner::IsReadable(vehicleBase + gOff, 8))
            continue;

        const uint8_t* mem     = reinterpret_cast<const uint8_t*>(vehicleBase + gOff);
        const uint8_t nextGear = mem[0];
        const uint8_t pad1     = mem[1]; // alignment gap between NextGear and Gear
        const uint8_t gear     = mem[2];
        const uint8_t topGear  = mem[6];

        if (gear > 1)                    continue; // 0=neutral, 1=first gear
        if (nextGear > 2)                continue;
        // RELAXED FILTER: TopGear just needs to be close to realMaxGear.
        // Strict match causes false positives if the game adds a hidden 6th gear!
        if (topGear < realMaxGear || topGear > realMaxGear + 2) continue; 
        if (pad1 != 0)                   continue; // strong alignment filter

        // Proximity: gear cluster must be within 0x200 bytes of RPM candidate (stricter than before)
        const uint32_t dist = gOff > rpmOffset ? gOff - rpmOffset : rpmOffset - gOff;
        if (dist > 0x200) continue;

        out.nextGearOff = gOff;
        out.gearOff     = gOff + 2;
        out.topGearOff  = gOff + 6;
        out.ratiosOff   = 0; // unknown — GetGearRatio() returns 0 gracefully
        
        LOG_DEBUG(Calib,
            "  [P2 BLIND HIT] Cluster@0x%X N=%u G=%u Top=%u dist=0x%X",
            gOff, nextGear, gear, topGear, dist);
            
        // TEMPORARY DIAGNOSTIC: Dump memory to log to find the true gear offset
        LOG_INFO(Calib, "  --- BEGIN MEMORY DUMP (0x780 - 0x8D0) ---");
        for (uint32_t dOff = 0x780; dOff < 0x8D0; dOff += 16) {
            if (AOBScanner::IsReadable(vehicleBase + dOff, 16)) {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(vehicleBase + dOff);
                LOG_INFO(Calib, "  0x%03X: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X",
                         dOff, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                         p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
            }
        }
        LOG_INFO(Calib, "  --- END MEMORY DUMP ---");
            
        return true;
    }

    LOG_DEBUG(Calib, "  [SearchGear] Both passes failed");
    return false;
}

// ── SEH-safe wrapper for SearchGearLayout ────────────────────────────────────
// MSVC C2712: __try cannot appear in a function that requires object unwinding.
// This wrapper has ONLY POD locals (GearCluster = 4x uint32_t, no destructor),
// so __try is valid here.  Never call LOG_ inside this function — std::string
// internally used by ModLogger would violate the same rule.
static bool SafeCallSearchGearLayout(uintptr_t vehicleBase, uint32_t rpmOffset, uint8_t realMaxGear,
                                     GearCluster* pOut)
{
    bool result = false;
    __try {
        result = SearchGearLayout(vehicleBase, rpmOffset, realMaxGear, *pOut);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("[CalibrationEngine] SEH exception in SearchGearLayout\n");
        return false;
    }
    return result;
}

} // anonymous namespace

// ============================================================================
// CalibrationEngine public API
// ============================================================================
namespace CalibrationEngine {

CalibrationState   GetState()          { return s_state; }
size_t             GetCandidateCount() { return s_rpmCandidates.size(); }
const std::string& GetLastError()      { return s_lastError; }

// ---------------------------------------------------------------------------
void Reset() {
    LOG_INFO(Calib, "CalibrationEngine::Reset() — clearing all state");
    s_state          = CalibrationState::WaitingForEngineOff;
    s_rpmCandidates.clear();
    s_idleValues.clear();
    s_phaseEnterTick = 0;
    s_lastError.clear();
}

// ---------------------------------------------------------------------------
bool Update(int vehicleHandle, bool isEngineOn, bool isRevving, uint8_t maxGear,
            VehicleOffsets& outOffsets)
{
    if (s_state == CalibrationState::None ||
        s_state == CalibrationState::Done ||
        s_state == CalibrationState::Failed)
        return false;

    const uintptr_t vehicleBase =
        reinterpret_cast<uintptr_t>(getScriptHandleBaseAddress(vehicleHandle));
    if (vehicleBase == 0) {
        LOG_WARN(Calib, "getScriptHandleBaseAddress returned null (handle=%d)", vehicleHandle);
        return false;
    }

    // ── State: WaitingForEngineOff ────────────────────────────────────────
    if (s_state == CalibrationState::WaitingForEngineOff) {
        if (!isEngineOn) {
            LOG_INFO(Calib, "Engine OFF → ScanningEngineOff");
            s_state = CalibrationState::ScanningEngineOff;
        }
        return false;
    }

    // ── State: ScanningEngineOff ──────────────────────────────────────────
    if (s_state == CalibrationState::ScanningEngineOff) {
        if (isEngineOn) {
            LOG_WARN(Calib, "Engine came back ON during ScanningEngineOff — restart");
            s_state          = CalibrationState::WaitingForEngineOff;
            s_phaseEnterTick = 0;
            return false;
        }

        const ULONGLONG now = GetTickCount64();
        if (s_phaseEnterTick == 0) {
            s_phaseEnterTick = now;
            LOG_DEBUG(Calib, "Settle timer armed: %llums", kEngineOffSettleMs);
            return false;
        }
        if (now - s_phaseEnterTick < kEngineOffSettleMs)
            return false;

        // ── Snapshot: near-zero floats (RPM ≈ 0 when engine is off) ──────
        s_phaseEnterTick = 0;
        s_rpmCandidates.clear();
        int scanned = 0;
        for (uint32_t off = kScanStart; off + 4 <= kScanEnd; off += 4) {
            if (!AOBScanner::IsReadable(vehicleBase + off, 4)) continue;
            ++scanned;
            float val = *reinterpret_cast<const float*>(vehicleBase + off);
            // Accept slightly negative values too — floating-point noise.
            // Also accept up to 0.90 because some vehicles idle high and we don't want to exclude them.
            if (val > -0.01f && val < 0.90f)
                s_rpmCandidates.push_back(off);
        }
        LOG_INFO(Calib,
            "Engine-OFF scan complete: scanned=%d nearZero=%zu (range 0x%X-0x%X)",
            scanned, s_rpmCandidates.size(), kScanStart, kScanEnd);

        if (s_rpmCandidates.empty()) {
            s_state = CalibrationState::Failed;
            char msg[200]{};
            sprintf_s(msg,
                "0 near-zero candidates found in 0x%X-0x%X. "
                "Widen kScanEnd in CalibrationEngine.h and rebuild.",
                kScanStart, kScanEnd);
            s_lastError = msg;
            LOG_ERROR(Calib, "%s", msg);
        } else {
            s_state = CalibrationState::WaitingForEngineOn;
        }
        return false;
    }

    // ── State: WaitingForEngineOn ─────────────────────────────────────────
    if (s_state == CalibrationState::WaitingForEngineOn) {
        if (isEngineOn) {
            LOG_INFO(Calib, "Engine ON → ScanningEngineOn");
            s_state = CalibrationState::ScanningEngineOn;
        }
        return false;
    }

    // ── State: ScanningEngineOn ───────────────────────────────────────────
    if (s_state == CalibrationState::ScanningEngineOn) {
        if (!isEngineOn) {
            LOG_WARN(Calib, "Engine went OFF during ScanningEngineOn — reverting");
            s_state          = CalibrationState::WaitingForEngineOn;
            s_phaseEnterTick = 0;
            return false;
        }

        const ULONGLONG now = GetTickCount64();
        if (s_phaseEnterTick == 0) {
            s_phaseEnterTick = now;
            LOG_DEBUG(Calib, "Idle settle timer armed: %llums", kIdleSettleMs);
            return false;
        }
        if (now - s_phaseEnterTick < kIdleSettleMs)
            return false;

        // ── Filter: candidates that moved off 0 at idle ───────────────────
        s_phaseEnterTick = 0;
        std::vector<uint32_t> next;
        std::vector<float>    nextIdle;
        for (uint32_t off : s_rpmCandidates) {
            if (!AOBScanner::IsReadable(vehicleBase + off, 4)) continue;
            const float val = *reinterpret_cast<const float*>(vehicleBase + off);
            // Upper bound 20000 covers both 0-1 normalized and raw-RPM builds.
            if (val > 0.005f && val < 20000.0f) {
                next.push_back(off);
                nextIdle.push_back(val);
                LOG_DEBUG(Calib, "  Idle candidate 0x%X = %.5f", off, val);
            }
        }
        LOG_INFO(Calib, "Idle scan: %zu → %zu candidates survived",
                 s_rpmCandidates.size(), next.size());

        if (next.empty()) {
            s_state = CalibrationState::Failed;
            s_lastError =
                "No candidate moved off 0.0 at idle. "
                "Wait longer for the engine to reach idle RPM, then recalibrate.";
            LOG_ERROR(Calib, "%s", s_lastError.c_str());
        } else {
            s_rpmCandidates = std::move(next);
            s_idleValues    = std::move(nextIdle);
            s_state         = CalibrationState::WaitingForRev;
        }
        return false;
    }

    // ── State: WaitingForRev ──────────────────────────────────────────────
    if (s_state == CalibrationState::WaitingForRev) {
        if (isRevving) {
            LOG_INFO(Calib, "Rev detected (throttle>0.5) → ScanningRev");
            s_state = CalibrationState::ScanningRev;
        }
        return false;
    }

    // ── State: ScanningRev ────────────────────────────────────────────────
    if (s_state == CalibrationState::ScanningRev) {
        if (!isRevving) {
            LOG_DEBUG(Calib, "Throttle released during ScanningRev — waiting for sustained rev");
            s_state          = CalibrationState::WaitingForRev;
            s_phaseEnterTick = 0;
            return false;
        }

        const ULONGLONG now = GetTickCount64();
        if (s_phaseEnterTick == 0) {
            s_phaseEnterTick = now;
            LOG_DEBUG(Calib, "Rev settle timer armed: %llums", kRevSettleMs);
            return false;
        }
        if (now - s_phaseEnterTick < kRevSettleMs)
            return false;

        // ── Filter: candidates that rose ≥30 % above their own idle ──────
        s_phaseEnterTick = 0;
        std::vector<uint32_t> next;
        std::vector<float>    nextIdle;
        for (size_t i = 0; i < s_rpmCandidates.size(); ++i) {
            const uint32_t off     = s_rpmCandidates[i];
            const float    idleVal = s_idleValues[i];
            if (!AOBScanner::IsReadable(vehicleBase + off, 4)) continue;
            const float val = *reinterpret_cast<const float*>(vehicleBase + off);
            // Relative threshold works regardless of RPM numeric scale.
            if (val > idleVal * 1.30f && (val - idleVal) > 0.005f) {
                next.push_back(off);
                nextIdle.push_back(idleVal);
                LOG_DEBUG(Calib, "  Rev survivor 0x%X: idle=%.5f rev=%.5f (+%.1f%%)",
                          off, idleVal, val, (val / idleVal - 1.0f) * 100.0f);
            }
        }
        LOG_INFO(Calib, "Rev scan: %zu → %zu candidates survived",
                 s_rpmCandidates.size(), next.size());

        if (next.empty()) {
            s_state = CalibrationState::Failed;
            s_lastError =
                "No candidate rose >=30% above idle during rev. "
                "Hold W (throttle) harder and longer, then recalibrate from scratch.";
            LOG_ERROR(Calib, "%s", s_lastError.c_str());
            return false;
        }
        s_rpmCandidates = std::move(next);
        s_idleValues    = std::move(nextIdle);

        // ── Gear layout search ────────────────────────────────────────────
        // Try each RPM candidate.  Clutch is assumed at RPM+12 (known GTA V
        // struct relationship).  SearchGearLayout finds the gear bytes via a
        // two-pass robust search (see file header comment).
        LOG_INFO(Calib, "Starting gear layout search for %zu RPM candidates...",
                 s_rpmCandidates.size());

        bool         succeeded = false;
        VehicleOffsets best{};

        for (uint32_t rpmOff : s_rpmCandidates) {
            LOG_DEBUG(Calib, "  Testing RPM=0x%X CLT=0x%X", rpmOff, rpmOff + 12);

            GearCluster cluster{};
            bool found = SafeCallSearchGearLayout(vehicleBase, rpmOff, maxGear, &cluster);
            if (!found) {
                LOG_DEBUG(Calib, "  RPM=0x%X: no layout found (SEH or search failed)", rpmOff);
                continue;
            }

            // Initialize from existing resolved offsets so we don't zero out fields
            // that weren't scanned here (like LightsBroken or DriveForce).
            VehicleOffsets cand = VehicleData::GetResolvedOffsets();
            cand.RPM       = rpmOff;
            
            // In older builds (e.g. 1.0.1180), Clutch was RPM + 12.
            // In newer builds (e.g. 3095), Clutch is RPM + 4.
            // Since fClutch mimics fRPM in neutral, the true Clutch offset MUST be in s_rpmCandidates!
            cand.Clutch = rpmOff + 12; // fallback
            for (uint32_t c : s_rpmCandidates) {
                if (c == rpmOff + 4) {
                    cand.Clutch = rpmOff + 4;
                    break;
                } else if (c == rpmOff + 12) {
                    cand.Clutch = rpmOff + 12;
                    break;
                }
            }

            cand.Gear      = cluster.gearOff;
            cand.NextGear  = cluster.nextGearOff;
            cand.TopGear   = cluster.topGearOff;
            cand.GearRatios = cluster.ratiosOff;

            if (!OffsetsAreSane(cand)) {
                LOG_DEBUG(Calib,
                    "  RPM=0x%X: sanity check failed "
                    "(G=0x%X N=0x%X RPM=0x%X CLT=0x%X)",
                    rpmOff, cand.Gear, cand.NextGear, cand.RPM, cand.Clutch);
                continue;
            }

            LOG_INFO(Calib,
                "  ACCEPTED: RPM=0x%X CLT=0x%X G=0x%X N=0x%X TG=0x%X Ratios=0x%X",
                cand.RPM, cand.Clutch, cand.Gear, cand.NextGear,
                cand.TopGear, cand.GearRatios);

            best      = cand;
            succeeded = true;
            break;
        }

        if (succeeded) {
            s_state    = CalibrationState::Done;
            outOffsets = best;
            LOG_INFO(Calib, "=== Calibration COMPLETE ===");
            return true;
        }

        // All candidates failed.
        s_state     = CalibrationState::Failed;
        s_lastError =
            "RPM/Clutch field located but Gear/NextGear/TopGear layout "
            "was not recognised around ANY of the RPM candidates. "
            "The struct may have shifted in this GTA V build. "
            "Try enabling DebugOverlay=1 and checking manual-trans.log "
            "for [SearchGear] output, then recalibrate.";
        LOG_ERROR(Calib,
            "Calibration FAILED — %zu RPM candidates tried, none passed gear search.",
            s_rpmCandidates.size());
        return false;
    }

    return false;
}

} // namespace CalibrationEngine
