# Sol-max senior review disposition

The sol-max-mediated review accepted the continuation architecture but rejected
the initial claim that Phase 3 was complete. The implementation was reopened,
the P0/P1 findings below were corrected, and Linux build/runtime evidence was
rerun before closing the phase.

| Finding | Disposition | Result |
|---|---|---|
| Particle, tracer, and temporary-beam state could advance once per eye | Adopted | Eye 0 draws an immutable pre-update snapshot; eye 1 draws the same snapshot and performs the single frame update. Beam-follow drawing and mutation are separated. |
| An aborted host frame could leave an OpenXR frame/UI image outstanding | Adopted | A subsequent begin closes the prior frame. End/shutdown release waited images before `xrEndFrame`, unwind active UI state, and tolerate UI release failure without a second end. |
| Focus loss or action-query failure could synthesize release-edge gameplay | Adopted | Frames now distinguish focused and actions-valid state. All action queries participate in validity; invalid frames clear latches without reload, flashlight, or inventory semantics. |
| Recenter used component-wise Euler subtraction and ignored reference-space changes | Adopted | Poses remain orthonormal bases across renderer, engine, and optional client ABI. Recenter is one inverse rigid transform, Euler conversion occurs only at legacy view/viewmodel boundaries, and `XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING` resets movement history. |
| VR movement was appended after game normalization without direction bits | Adopted | Final combined movement is normalized to the client maximum and final `IN_FORWARD`, `IN_BACK`, `IN_MOVELEFT`, and `IN_MOVERIGHT` bits are regenerated for ladder/game-code behavior. |
| Two-hand stabilization could remain stale and had no Lambda upper bound | Adopted | Inactive frames clear stabilization; support-hand separation must be between 0.15 m and 0.50 m, scaled into game units. |
| Tracked weapon pose was presentation-only, not authoritative shot direction | Accepted limitation; promoted to blocking Phase 5/7 work | Documentation no longer implies viewmodel alignment proves controller-directed combat. The dominant controller is the intended authority. Sven RPG direction, muzzle origin, and guidance are explicit acceptance cases. |
| Quest Touch and Valve Index behavior was only simulated/source-validated | Accepted remaining gate | Both interaction profiles remain implemented, but per-input and haptic acceptance on physical Quest and Index hardware is required in Phase 4. |
| Add a Windows ABI smoke test immediately | Deferred | The user limited this pass to Linux builds/tests. Fixed-width fields, version/size negotiation, layout guards, and `__cdecl` remain in place; Windows WGL/ABI smoke is Phase 6. |

## Simplifications

- Removed the unused game-client off-hand cache; the versioned frame remains
  the single pose snapshot.
- Removed unconsumed renderer eye payload, predicted-display-time payload, and
  touch fields before freezing the optional API.
- Retained `GL_OpenXRIsRenderingEye` because it now names the explicit
  draw-twice/update-once effects contract rather than acting as dead plumbing.
- Kept one renderer OpenXR owner, one engine command owner, and one optional
  aware-client bridge. No second host loop, movement predictor, renderer, or
  protocol was introduced.

## Review decisions

The architecture remains incremental: current Xash3D FWGS plus current
`hlsdk-portable`, with Lambda1VR as the behavioral oracle. Generic Sven support
comes first. Lambda-aware Sven weapons require compatible source or a narrowly
demonstrated adapter. For aware clients, the dominant controller—not gaze—is
the authoritative weapon aim source.

For opaque game DLLs, normal `usercmd_t::viewangles` may provide a compatibility
path for controller-directed projectile direction, but it cannot carry a 6DoF
muzzle origin and must not accidentally rotate the HMD/body camera with the
weapon hand. That tradeoff requires an end-to-end prototype before adoption.

## Verification after corrections

- Linux i386 engine, OpenXR disabled: 15/15 Waf tests.
- Linux i386 engine, OpenXR enabled: 15/15 Waf tests.
- Current VR-aware `hlsdk-portable`: CMake and Waf builds pass; the two public
  optional-API headers match byte-for-byte.
- Simulated Monado with Qwerty HMD/controllers: `c1a0` connects, the aware
  client receives an active tracked frame, and stereo plus head-relative UI are
  submitted until clean timeout shutdown.
- The same runtime test with a clean unmodified client also connects and
  submits stereo/UI, proving the generic fallback remains intact.

Not yet proven: physical Quest/Index behavior, disruptive lifecycle/recenter
events, ladders under real controller input, authoritative controller-directed
weapon fire, Sven binaries/source compatibility, Windows, or complete campaign
playthroughs.
