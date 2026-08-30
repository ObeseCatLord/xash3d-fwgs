# Desktop OpenXR Lambda1VR port

This branch ports Lambda1VR's user-visible VR behavior to current Xash3D FWGS
for desktop Linux and Windows. Android is a behavioral reference, not a target.

Team Beef Lambda1VR commit `8fa5e6b` is the behavior oracle. The product base is
Xash3D FWGS commit `1442d14a`; Lambda-aware game code is being transplanted onto
FWGS `hlsdk-portable` commit `e277ffaa`. The earlier desktop prototype at
`613de3e` remains useful for comparison, but is not a production base.

## Architecture invariants

- Keep `ref_params_t`, `usercmd_t`, `cl_enginefunc_t`, and `cldll_func_t`
  byte-compatible with unmodified GoldSrc client DLLs.
- Keep generic VR networking byte-compatible with the normal Xash/GoldSrc
  protocols.
- Execute one host, network, prediction, client-frame, refdef, viewmodel-event,
  and audio update per XR frame.
- Derive two renderer-owned eye passes from one immutable per-frame snapshot.
- Do not use the client-owned `nextView` mechanism as stereo state.
- Keep exactly one owner for OpenXR handles, frame pairing, actions, and spaces.
- Treat the first default-framebuffer copy path as disposable proof code.
- Add richer game behavior only through a separate versioned client interface.

## Compatibility modes

1. **Best-effort generic VR** keeps the stock game ABI and protocol. It provides
   stereo rendering, 6DoF head tracking, controller poses/buttons,
   room-scale-to-command movement, haptics, and head-relative UI. Unmodified
   game DLLs still use their normal view direction for authoritative attacks.
2. **Lambda-aware Half-Life** uses rebuilt game DLLs and an optional versioned
   interface for tracked weapons, offhand behavior, melee, scopes, gestures,
   hands, and flashlight behavior.
3. **Negotiated co-op VR** is future Sven-specific work. Remote tracked poses or
   richer melee are not inferred from Lambda1VR's existing multiplayer support.

## Behavior parity matrix

| Area | Lambda reference | Generic VR | Lambda-aware | Status |
|---|---|---:|---:|---|
| Predicted HMD and asymmetric eye views | `TBXR_Common.c` | yes | yes | implemented; simulated-runtime proof |
| 6DoF head translation and recenter | `L1VR_SurfaceView.c` | yes | yes | coherent basis transform implemented; reference-change hardware test pending |
| Session focus, stop, restart, teardown | `TBXR_Common.c` | yes | yes | pairing/unwind implemented; restart testing pending |
| Quest/Index/Vive/WMR action bindings | `OpenXrInput.c` | yes | yes | implemented; Quest/Index hardware validation pending |
| Dominant/off-hand aim and handedness | `VrInputCommon.c` | yes | yes | implemented |
| Snap/smooth turn and locomotion direction | `VrInputDefault.c` | yes | yes | implemented |
| Room-scale movement as normal movement | `VrInputDefault.c` | yes | yes | implemented |
| Jump, crouch, ladder, use, reload | `VrInputDefault.c` | yes | yes | implemented |
| Haptics | `VrInputCommon.c` | yes | yes | implemented; hardware validation pending |
| Menu/UI pointer and head-relative HUD | `VrInputDefault.c`, `vr_renderer.cpp` | yes | yes | head-relative HUD implemented; pointer pending |
| Comfort mask | `L1VR_SurfaceView.c` | yes | yes | pending |
| Controller-relative weapon transform | `vr_helper.cpp` | no | yes | Phase 3 vertical slice |
| Weapon velocity and pitch compensation | `VrInputDefault.c` | no | yes | Phase 3 vertical slice |
| Authoritative controller-aligned hitscan/projectiles | weapon/game event paths | no | no | Phase 5; presentation pose is not shot authority |
| Physical melee and gesture use | `VrInputDefault.c` | no | yes | pending |
| Two-hand stabilization and scope | `VrInputDefault.c` | no | yes | stabilization vertical slice; scope pending |
| Hand and flashlight models | `vr_renderer.cpp` | no | yes | pending |
| Backpack quick actions | `VrInputDefault.c` | no | yes | pending |
| Save/load and map transitions | client/server VR helpers | limited | yes | pending |

## Phase gates

### Phase 1

- Optional Waf feature-off and feature-on Linux builds pass.
- An unmodified standard Half-Life client DLL renders one map through Monado.
- Counters demonstrate one stateful game callback sequence and two asymmetric
  renderer passes per XR frame.
- Both swapchain images are paired and submitted, and shutdown is clean.

### Phase 2

- Generic controls, room-scale movement, haptics, lifecycle, menu, and HUD match
  the parity matrix without changing public game structs or network commands.

### Phase 3

- A size/version-tagged optional client interface carries Lambda-only state.
- A current `hlsdk-portable` Half-Life client proves the first vertical slice:
  tracked dominant-hand weapon pose, frame-retained off-hand state, two-hand
  stabilization, pitch compensation, and weapon haptics.
- Physical melee, scopes, gestures, rendered hand/flashlight models, backpack
  actions, and save/transition parity remain later behavior slices. They are
  not Phase 3 completion criteria.

## Architecture decision: transplant, do not revive the old client

The old Lambda client and server can be made to compile on Linux, but loading
them into current Xash crashes during the first prediction frame in the
player-movement trace path. That is demonstrated incompatibility at the legacy
game ABI boundary, not evidence that the engine or renderer should be forked.

The minimal production design is therefore:

1. Preserve current Xash and GoldSrc public structs and networking.
2. Preserve current `hlsdk-portable` as the game-code implementation.
3. Transplant Lambda behavior in bounded slices through the optional VR client
   interface, leaving ordinary client DLLs unaffected.
4. Keep the old Android and desktop ports as behavioral and mathematical
   references only.

This avoids maintaining a second player-movement implementation, a second host
loop, or a parallel OpenXR state machine.

## Campaign and co-op compatibility roadmap

- **Half-Life base campaign:** Phase 3 uses `hlsdk-portable` master and is the
  first Linux-native Lambda-aware target.
- **Blue Shift and Opposing Force:** build the existing `bshift` and `opfor`
  branches as separate client/server artifacts. Each consumes the same optional
  VR client API and receives weapon-specific behavior adapters without changing
  the engine ABI.
- **Sven Co-op 3.0:** the supplied archive includes Windows `client.dll` and
  `hl.dll` binaries, but no Linux game libraries. On Linux it can initially use
  generic engine VR only if a compatible native game-code build is obtained.
  On Windows, unmodified binaries should retain generic VR; Lambda-aware weapon
  behavior requires a source-compatible Sven build or a narrowly scoped,
  binary-compatible adapter. Remote tracked poses and authoritative physical
  melee require an explicitly negotiated protocol extension and are later work.
  The RPG/rocket launcher is a mandatory 6DoF acceptance case: both its visual
  muzzle and server-authoritative rocket trajectory must follow the dominant
  controller rather than the HMD/body view.
- **They Hunger Multiplayer Map Conversion:** the supplied archive contains
  Sven maps and content, not its own game DLLs or game manifest. Treat it as an
  overlay acceptance suite after Sven compatibility, not as another engine or
  VR implementation. The `hlsdk-portable` `theyhunger` branch remains useful
  for the original single-player campaign target.

Acceptance should progress from one stock map, to representative weapon and
transition tests, to complete base campaigns, then to Sven-hosted conversions.
Content compatibility is measured by playable maps and transitions, not merely
by loading an archive or negotiating the optional API.

Reopen the architecture if a phase creates a second host loop, duplicates the
OpenXR state machine, changes the public game ABI/protocol, clones a renderer,
or repeatedly expands the provisional presentation bridge.
