# Sol-max senior review brief: desktop Lambda1VR port through Phase 3

> This is the verified pre-review snapshot supplied to sol-max. Findings and
> implemented dispositions are recorded in `senior-review-disposition.md`.

## Goal and scale

Review the architecture and implemented Linux Phase 0-3 vertical slice for
porting Team Beef Lambda1VR behavior to current Xash3D FWGS, with later Windows,
Opposing Force, Blue Shift, Sven Co-op 3.0, and modded-campaign support.

This is a solo/open-source project. Prefer the smallest maintainable adapter;
no enterprise ceremony, parallel framework, or speculative protocol is wanted.
The review is read-only. The main agent will disposition and apply findings.

## Environment facts

| Fact | Value | Evidence label |
|---|---|---|
| Engine repo | `/home/obesecatlord/Documents/lambda1vr/xash3d-fwgs` on `pc-openxr-lambda-parity`, base `1442d14a69093780389104dcb7369aa3685945cf` | [verified: `git rev-parse`, 2026-08-30] |
| Production game repo | `/home/obesecatlord/Documents/lambda1vr/hlsdk-portable-vr` on `pc-openxr-lambda-parity`, base `e277ffaae85422cb674c8c19a2209a7fa85157db` | [verified: `git rev-parse`, 2026-08-30] |
| Behavior oracle | `/home/obesecatlord/Documents/lambda1vr/Lambda1VR`, Team Beef Android fork | [verified: local source inspection] |
| Rejected prototype | `/home/obesecatlord/Documents/lambda1vr/hlsdk-xash3d-vr` | [verified: compiled and runtime tested] |
| Platform tested | Linux i386 engine/client/server, GLX/OpenGL OpenXR | [verified: build output and `file`] |
| Runtime | Monado v25.1 simulated Qwerty HMD plus left/right controllers, null compositor | [verified: runtime device selection log] |
| Physical controllers | Quest Touch and Valve Index not physically available in this pass | [unknown: hardware behavior] |
| Supplied Sven archive | Windows `client.dll` and `hl.dll`; no Linux game libraries | [verified: archive listing] |
| Supplied They Hunger conversion | Sven maps/content overlay; no independent client/server DLLs or game manifest | [verified: archive listing] |

## Evidence: verified facts, separate from recommendations

1. [verified: source and runtime] The old Lambda game DLL changes two embedded
   GoldSrc ABIs: adds `playermove_t::angles2` and widens
   `usercmd_t::buttons`. On i386 its `playermove_t` is 325088 bytes versus
   current FWGS/portable's 325072. Its expected `PM_PointContents` offset
   324996 is current FWGS's `PM_TraceLine` slot. The reproduced first prediction
   crash is therefore a wrong-signature indirect call into `PM_CL_TraceLine`,
   ending at `PM_HullForBsp`. Current layout guards are at
   `engine/client/dll_int/cl_pmove.c:32-34`; `usercmd_t` was already pinned in
   `common/q_client.h:55`.

2. [verified: diff inspection] No VR fields were added to `ref_params_t`,
   `usercmd_t`, `cl_enginefunc_t`, `cldll_func_t`, or network messages.
   `common/ref_params.h:76-81` adds guards only. The renderer interface version
   moves to 19 and gains renderer-private callbacks at
   `engine/ref_api.h:735-742`.

3. [verified: source inspection] The renderer is the sole owner of all OpenXR
   handles, actions, spaces, swapchains, frame wait/begin/end, and composition
   in `ref/gl/gl_openxr.c`. The engine begins one XR frame in
   `engine/client/input/input.c:675`, calculates one game refdef, then performs
   two eye draws in `engine/client/cl_view.c`. Eye projection and culling are
   asymmetric. Stateful renderer effects, counters, viewmodel events, and
   extra updates are guarded from the secondary eye in `ref/gl/gl_rmain.c`.

4. [verified: source and runtime] OpenXR action profiles are explicitly
   suggested for core Oculus Touch/Quest and Valve Index at
   `ref/gl/gl_openxr.c:307-308`. They include grip/aim, trigger, squeeze,
   thumbsticks, face buttons, menu/system fallback, clicks, and haptics. The
   paths agree with the Khronos OpenXR 1.1 profile allowlists. Simulated Monado
   validated action-set/session wiring, not physical button feel.

5. [verified: source inspection] Generic controls remain engine-owned and
   append ordinary `usercmd_t` movement/buttons at
   `engine/client/vr/vr_client.c:230`. Handedness, locomotion, room-scale,
   snap/smooth turn, attack/alt attack, use, jump, crouch, reload, inventory,
   flashlight, menu, and haptics do not require an aware game DLL. Session loss
   clears input latches. The `hand` cvar is synchronized with Lambda's
   `vr_control_scheme` convention.

6. [verified: source inspection] Head/controller positions and velocities are
   translated and yaw-rotated into the recenter basis before body-yaw placement
   (`engine/client/vr/vr_client.c`, `engine/client/vr/vr_game.c`). OpenXR
   orientation is currently converted to Xash Euler pitch/yaw/roll at the
   renderer boundary.

7. [verified: source and symbol inspection] A separate fixed-width v1 optional
   API is in `public/vr_client_api.h`; the engine negotiates it only after the
   legacy `Initialize` call at `engine/client/vr/vr_game.c:61` and invokes its
   shutdown before DLL unload. Linux i386 sizes are statically pinned and
   callback calling convention is explicit `__cdecl` on Windows. Both repos'
   headers compare byte-for-byte.

8. [verified: source, two build systems, symbol inspection] Current
   hlsdk-portable exports `HUD_GetVRClientAPI` at
   `cl_dll/vr_client.cpp:194`, caches frames, applies dominant-hand viewmodel
   pose and support-hand stabilization at `cl_dll/vr_client.cpp:136`, tracks
   the weapon at `cl_dll/hl/hl_weapons.cpp:671`, and emits common local-fire
   haptics at `cl_dll/ev_hldm.cpp:44`.

9. [verified: commands completed 2026-08-30] Engine feature-off and feature-on
   Linux debug i386 builds each passed 15/15 Waf tests. Modified hlsdk-portable
   passed CMake and Waf 32-bit client/server builds; `client.so` and `hl.so` are
   i386 and the optional export is visible.

10. [verified: end-to-end runtime] Modified portable client/server loaded
    `c1a0`, negotiated the optional API (proved by its active-frame callback),
    connected, submitted stereo and a head-relative UI layer, and remained
    alive until timeout. A clean local clone of unmodified current
    hlsdk-portable also loaded `c1a0`, connected, submitted stereo/UI, and
    remained alive until timeout, proving generic VR does not depend on the
    optional API. A separate older local DLL reproduced the known PM ABI crash
    and is not evidence against current portable.

11. [verified: documents] `Documentation/openxr-port-plan.md`,
    `Documentation/openxr-controller-bindings.md`, and
    `Documentation/openxr-continuation-plan.md` distinguish completed Phase 3
    proof from later Lambda parity and lay out Linux hardware, Windows, campaign,
    and Sven gates.

## Current architecture recommendation (opinion)

Continue this implementation, not the old fork and not a restart. Keep one
renderer-owned OpenXR state machine, one engine-owned generic input adapter,
and one small optional game-client interface for behavior that truly needs
game code. Port Lambda behavior onto each current hlsdk-portable campaign
branch in vertical slices. Keep existing game/server authority and ordinary
protocols unless a reproduced incompatibility demands a narrow adapter.

## Open decisions

### D1 — accept the three-boundary architecture

Current lean: accept renderer OpenXR + engine generic policy + optional game
presentation API. Rejected: revive the old HLSDK, because its public movement
ABI is concretely incompatible; replace more engine/game systems, because no
adjacent incompatibility was demonstrated.

### D2 — accept the provisional default-framebuffer copy compositor

Current lean: acceptable through Phase 3 because it proves behavior with low
renderer disruption; replace with direct per-eye render targets only when
profiling or correctness demonstrates need. Rejected: immediate renderer-wide
FBO rewrite, because it expands the proof slice before hardware acceptance.

### D3 — retain Euler poses in v1 or revise before Windows freezes the ABI

Current lean: keep v1 Euler because GoldSrc viewmodels and Lambda use Euler,
but this is the last cheap point to identify a quaternion/gimbal or transform
composition problem. Rejected: exposing raw OpenXR structs, because that leaks
platform/runtime ABI into game DLLs.

### D4 — generic controls stay in the engine

Current lean: yes, so unmodified game clients work and there is one command
owner. Game code owns only weapon-specific pose, models, haptics, melee, scope,
and gestures. Rejected: duplicate Lambda input handling in hlsdk-portable,
because it would create two command owners.

### D5 — define Phase 3 as a vertical proof, not Lambda completion

Current lean: close Phase 3 now after tracked weapon pose, stabilization, and
haptics. Physical melee, scope, hand/flashlight rendering, backpack actions,
comfort mask, pointer UI, save/transition restoration, and full campaign tests
remain later gates. Rejected: claim parity from API/counter proof.

### D6 — campaign reuse and Sven strategy

Current lean: port the same bounded adapter to current `bshift`/`opfor`
branches; first run Sven's supplied Windows binaries in generic VR; require
compatible Sven source or a proven narrow binary adapter for aware weapons;
add remote poses/melee only as negotiated extensions. Rejected: preemptive Sven
protocol/game-rule rewrite.

### D7 — Windows sequencing

Current lean: complete Linux physical Quest/Index acceptance and remaining
presentation fundamentals, then add WGL setup to the same renderer state
machine. Rejected: a separate Windows OpenXR implementation.

## Suspected overlap

- D1 and D4 are the same ownership question viewed from architecture and input;
  merge them if that produces a clearer recommendation.
- D3 and D6 both affect how long the v1 optional ABI can remain stable.
- D2 may hide side-effect problems that are actually D1 problems; challenge
  whether the two-eye renderer guards are complete.

## Requested review output

Verify the load-bearing claims against both repositories before critiquing.
Return at most 2,500 words with:

1. prioritized findings (`P0`-`P3`) with concrete file/line evidence;
2. a re-ranked disposition for D1-D7, merging or deleting decisions as useful;
3. bugs or will-bite-later failure modes, especially frame/session pairing,
   renderer side effects, recenter transforms, input latches, ABI/calling
   convention, Quest/Index paths, and current-HLSDK viewmodel application;
4. missing end-to-end tests required before Phase 4;
5. genuinely human decisions, if any, stated as crisp questions;
6. specific simplifications or code that should be deleted.

## Depth budget and not-list

Spend depth on architectural necessity and correctness across boundaries. Do
not re-review formatting, licensing, Android buildability, Windows code that
does not yet exist, full Phase 4-7 feature implementation, unrelated upstream
Xash/hlsdk code, or raw archival content beyond the stated Sven/They Hunger
facts. Do not edit files. If evidence is missing, label it rather than widening
scope indefinitely.
