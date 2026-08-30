# Desktop OpenXR continuation plan

## Goal and boundaries

Deliver Lambda1VR-equivalent behavior on desktop Linux and Windows while
preserving current Xash3D FWGS, GoldSrc game ABI, networking, and game-code
behavior. Android is the behavioral oracle, not a build target.

The architecture-planning postmortem's central lesson is enforced here:
isolate demonstrated incompatibility at its narrowest boundary. OpenXR and
generic controls belong in the renderer/engine adapter; game-specific weapon
behavior belongs in current game code; existing movement, prediction, game
rules, maps, and protocols remain in place unless a concrete incompatibility
is reproduced.

## Baseline decision

| Component | Production base | Reuse decision |
|---|---|---|
| Engine and renderer | Xash3D FWGS `1442d14a` | Continue incrementally |
| OpenXR behavior | Team Beef Lambda1VR `8fa5e6b` | Behavioral oracle only |
| Half-Life game code | FWGS hlsdk-portable `e277ffaa` | Continue incrementally |
| Old Lambda desktop/HLSDK fork | prototype `613de3e` and descendants | Reference only |

The old client is not a viable production base. Its i386 `playermove_t` is 16
bytes larger than current FWGS because it inserts `angles2` and widens
`usercmd_t::buttons`. That shifts `PM_PointContents` onto the engine's
`PM_TraceLine` slot and causes the reproduced first-prediction crash. The
production response is a current-HLSDK behavior transplant, not a parallel
engine, movement system, or renderer.

## Completed foundation through Phase 3

### Phase 0 — compatibility boundaries

- Pin the public `ref_params_t`, `usercmd_t`, and i386 `playermove_t` layouts.
- Keep OpenXR types out of public game interfaces and network messages.
- Define generic and Lambda-aware compatibility modes.

Exit evidence: feature-off build passes; incompatible old game DLL failure is
explained at exact callback offsets; current hlsdk-portable client/server run.

### Phase 1 — stereo and 6DoF

- Renderer owns the OpenXR instance, session, actions, spaces, swapchains, and
  frame pairing.
- Engine performs one simulation/refdef/audio update and two asymmetric eye
  draws from one frame snapshot.
- Apply 6DoF head pose, scaled IPD, and asymmetric projection/frustum.
- Submit paired projection images before the desktop swap.

Exit evidence: Linux OpenXR build and tests pass; simulated Monado submits the
first stereo frame on `c1a0` without prediction or rendering failure.

### Phase 2 — generic controls and HUD

- Add grip/aim poses, sticks, buttons, squeeze/trigger, and haptics.
- Provide explicit Quest Touch and Valve Index profiles, plus simple, Vive,
  and WMR fallbacks.
- Map dominant/off-hand controls, locomotion, room scale, snap/smooth turn,
  attack, use, jump, crouch, reload, inventory, menu, and flashlight onto
  ordinary engine input and `usercmd_t` fields.
- Submit a head-relative HUD quad without changing the client DLL ABI.

Exit evidence: current unmodified hlsdk-portable runs in generic VR mode under
simulated Monado and submits stereo plus HUD layers. Quest and Index mappings
are compile/runtime-negotiation ready; real-device action and haptic acceptance
is still required.

### Phase 3 — first Lambda-aware game-code slice

- Negotiate a fixed-width, size/version-tagged optional client API without
  extending `cldll_func_t`, `ref_params_t`, or the network protocol.
- Feed a cached frame containing head, dominant/off-hand grip and aim poses,
  velocities, buttons, and haptics into current hlsdk-portable.
- Preserve orientation as an orthonormal basis through recenter and body
  composition; convert to Euler angles only at legacy GoldSrc presentation
  boundaries.
- Apply dominant-hand viewmodel pose, weapon/crowbar/throwable pitch policy,
  support-hand stabilization, and common weapon-fire haptics.
- Keep off-hand state for later rendering without creating a second input or
  game-state owner.

Exit evidence: 32-bit CMake and Waf client/server builds pass, the optional
symbol exports, API headers match byte-for-byte, and simulated Monado runs
`c1a0` with API negotiation, active tracked frames, stereo, and HUD submission.
Feature-off and OpenXR engine builds each pass 15/15 tests. A clean unmodified
client also completes the generic runtime proof.

Phase 3 is a vertical proof, not full Lambda parity. Physical melee, scopes,
rendered hands, flashlight models, backpack gestures, UI pointing, comfort
masking, authoritative controller-directed hitscan/projectile aim, and all
per-weapon tuning remain future work.

## Next implementation phases

### Phase 4 — Linux interaction acceptance and presentation completion

1. Add a controller-ray UI pointer and verify menu ownership/focus transitions.
2. Port Lambda's comfort mask and expose equivalent cvars.
3. Add rendered off-hand/hand and flashlight presentation without moving
   weapon authority or input policy out of existing owners.
4. Run the controller matrix on physical Quest Touch and Valve Index hardware:
   grip pose, aim pose, both sticks, trigger, squeeze, face buttons, menu,
   stick click, handedness, and per-hand haptics.
5. Add repeatable lifecycle tests for focus loss, STOPPING to READY, runtime
   shutdown, map changes, save/load, death, and recenter.

Gate: one complete `c1a0` session on each controller family, including menu,
movement, combat, save/load, and a map transition, with no duplicated game
callbacks or stuck actions.

### Phase 5 — Lambda gameplay parity

Port behavior in end-to-end slices, using the Lambda matrix as the acceptance
list:

1. Per-weapon offsets, pitch, velocity, recoil, muzzle/event alignment, and
   two-hand stabilization. Make the dominant controller the authoritative aim
   source for aware game code; do not confuse viewmodel alignment with shot
   direction.
2. Scoped weapon rendering and eye selection.
3. Physical crowbar/melee and gesture-use behavior while preserving server
   authority and ordinary damage paths.
4. Backpack quick actions, hand models, flashlight attachment, death/respawn,
   save/load, and level-transition state restoration.
5. Full base Half-Life campaign regression, with representative saves at
   chapter boundaries and scripted transitions.

Gate: user-visible behavior matches Lambda1VR for every matrix row; counters or
API negotiation alone do not close a row.

### Phase 6 — Windows backend and expansion campaigns

1. Add WGL graphics binding to the existing renderer-owned OpenXR state
   machine. Do not clone the Linux backend; isolate only window/context setup.
2. Build and smoke-test 32-bit Windows engine and game DLLs with the same
   optional API and explicit `__cdecl` callbacks.
3. Rebase the bounded game adapter onto hlsdk-portable's `bshift` and `opfor`
   branches, preserving each branch's existing game rules and weapons.
4. Run complete Blue Shift and Opposing Force campaign acceptance suites.
5. Optionally adapt the `theyhunger` branch for the original single-player
   campaign; keep this separate from the Sven multiplayer conversion.

Gate: Linux and Windows share behavior and ABI tests; each expansion completes
its campaign with weapon, save/load, and transition parity.

### Phase 7 — Sven Co-op 3.0 and modded campaigns

1. Inventory the supplied Sven exports, protocol expectations, filesystem
   layout, and engine-extension dependencies on Windows. The supplied archive
   has Windows game binaries but no Linux game libraries, so Linux cannot be
   the binary acceptance platform for that archive.
2. First prove unmodified Sven binaries in generic VR: stereo, 6DoF, controls,
   HUD, local haptics, base campaign maps, connect/disconnect, and transitions.
3. Pursue Lambda-aware Sven weapons only through an available compatible
   source build or a narrowly proven binary adapter. Do not patch public engine
   structs to fit the old Lambda fork.
4. Keep ordinary Sven networking authoritative. Add remote head/hand poses or
   physical-melee data only as an optional, version-negotiated extension with
   clean fallback for non-VR peers.
5. Validate the Half-Life base campaign in Sven, then Opposing Force and Blue
   Shift content where supported, then the supplied They Hunger Multiplayer
   Map Conversion. The conversion is a Sven content overlay, so its acceptance
   depends on Sven rather than a separate VR implementation.
6. Treat Sven's RPG as a blocking 6DoF regression: fire while the controller is
   displaced and aimed away from the HMD, and verify muzzle origin, rocket
   spawn origin, server trajectory, guidance behavior, collision, and what a
   second client observes all agree. A viewmodel-only pass is a failure.

Gate: two real clients complete representative combat and transitions, survive
reconnect/map changes, and remain compatible when one peer lacks VR extensions.

### Sven RPG investigation note

The current Phase 3 adapter moves only the client viewmodel. Normal HL server
RPG code derives launch vectors, source offsets, rocket angles, and laser traces
from `m_pPlayer->pev->v_angle` (`hlsdk-portable-vr/dlls/rpg.cpp`). Lambda1VR
solves this with an aware client/server path: its client sends tracked poses,
the server stores them on `CBasePlayer`, and its RPG consumes controller weapon
position and angles. Therefore a visually aligned Phase 3 RPG is expected to
fire along the legacy body/HMD view and is not a valid pass.

The supplied Sven 3.0 archive has binaries but no source, so its exact RPG path
is not yet proven. First test the ordinary `usercmd_t::viewangles` compatibility
route for opaque DLLs without coupling camera/body orientation to the hand. It
can convey direction but not a 6DoF muzzle origin. Full Lambda parity requires
a capability-aware Sven/game-DLL path or a narrowly demonstrated binary
adapter; do not add an unconditional pose command to ordinary networking.

## Reopen and simplification triggers

Pause implementation and revisit the design if a phase:

- adds a second OpenXR lifecycle, host loop, movement predictor, game-rule
  owner, or protocol for state that already has an owner;
- modifies a public GoldSrc struct or ordinary network command;
- exceeds its estimated file/module boundary or repeatedly fixes interactions
  among newly introduced layers;
- closes rows using mocks, counters, or packet emission without playable
  end-to-end behavior;
- leaves temporary bridges or diagnostics with no deletion/convergence plan.

Every review must ask what can be reused or deleted, not only whether newly
added code is locally correct.
