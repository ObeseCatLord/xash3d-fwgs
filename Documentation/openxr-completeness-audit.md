# Desktop Lambda1VR completeness audit

Status date: 2026-09-02

This document supersedes the implementation-status rows in
`openxr-continuation-plan.md` and `openxr-port-plan.md`. Those files preserve
the original phase plan and architecture decisions; they are no longer a list
of unimplemented features.

## Decision

Continue the current port. Do not restart it and do not revive the Android
engine fork.

The production design preserves current Xash3D FWGS, its renderer and GoldSrc
ABI, and each existing game DLL. Lambda1VR behavior is adapted at three narrow
boundaries:

1. the existing renderer owns OpenXR objects, frame pairing and compositor
   submission;
2. engine VR policy maps tracked input onto normal Xash input and a fixed-size,
   versioned user-command sidecar;
3. rebuilt game DLLs consume the optional pose API and sidecar for behavior
   that cannot be expressed by the legacy ABI.

The standalone Team Beef base repository adds no newer gameplay implementation
than the vendored Lambda1VR source. Its Opposing Force, They Hunger and AoMDC
trees are byte-identical to the corresponding vendored trees. AoMDC remains
architecture evidence only and is not a product target.

## Source-completeness result

Independent Terra closure audits and the retained oracle-to-desktop matrix
compared the desktop implementation to the Team Beef sources. Their
source-proven findings were fixed before this status was recorded. No known
Lambda1VR gameplay feature remains stubbed or deferred in the Linux source
path. This is a source-traceability result; runtime parity remains incomplete.

See `lambda1vr-parity-matrix.md` for the behavior-family mapping,
`openxr-acceptance-evidence.md` for commit-stamped evidence, and
`senior-review-disposition.md` for the final sol-max review decisions.

| Surface | Source status | Runtime status |
|---|---|---|
| OpenXR stereo, asymmetric FOV, 6DoF head pose and head-relative UI | implemented | simulated-runtime acceptance passed |
| Quest Touch and Valve Index actions, handedness and squeeze edges | implemented | physical-controller acceptance last |
| locomotion, room scale, wall pushback, crouch, turn, ladder and recenter | implemented | gameplay acceptance required |
| menu ray, use gestures, backpack actions, flashlight/hand presentation | implemented | gameplay acceptance required |
| comfort mask, scope and two-hand stabilization | implemented | gameplay acceptance required |
| finite and sustained per-hand haptics, including overlap policy | implemented | physical-controller acceptance last |
| weapon mirroring and weapon back-face controls | implemented and archived | presentation acceptance required |
| fixed-size VR client API and negotiated user-command sidecar | implemented with legacy fallback | modern two-client acceptance passed; protocol-48 server required for live fallback acceptance |
| base Half-Life weapons, events, melee, haptics, save/reset and RPG aim | implemented | representative campaign startup passed; physical RPG acceptance required |
| Blue Shift variant | implemented from the shared base SDK | campaign acceptance requires game data |
| Opposing Force weapons and behavior | implemented in the existing Gearbox DLL | campaign acceptance requires game data |
| standalone They Hunger weapons and behavior | implemented in its existing DLL | standalone campaign acceptance requires game data |
| Sven/SevenKewp VR behavior and They Hunger compatibility classes | implemented in SevenKewp | modern two-client, representative base-campaign and 58-map They Hunger conversion startup passed |

The They Hunger multiplayer archive contains 58 maps and 127 distinct entity
classnames. Its map-required custom entities are represented by SevenKewp.
`weapon_hornetgun` is deliberately not aliased to a They Hunger weapon: it is
unused by the supplied conversion and would collide with Sven's real
hornetgun.

## Verified automated evidence

- Fresh 32-bit Linux OpenXR and feature-off engine builds pass the engine test
  suite.
- OpenXR SDL2 and SDL3 builds compile; both accept a validated `-display`
  index before creating the desktop mirror.
- Base, Blue Shift, Opposing Force, standalone They Hunger and SevenKewp
  client/server targets build as i386 ELF artifacts.
- Every rebuilt client exports `HUD_GetVRClientAPI`; copied public API and
  sidecar headers match byte-for-byte.
- Engine/renderer ELF relocation checks pass.
- The package tests stage the exact 58-map They Hunger multiplayer overlay
  without modifying either input.
- Simulated Monado accepted the Linux GLX OpenXR session, stereo submission,
  head-relative UI submission and VR client API for base Half-Life, Sven and
  They Hunger. Every one of the 58 supplied They Hunger multiplayer maps
  reached those checkpoints without a resource warning.
- The supplied Sven 3 `c1a0`, `c2a3` and `c4a1` maps pass the same checkpoints
  without resource warnings. `c2a3` exposed a historical non-text padding byte
  after its final BSP entity; the shared loader now normalizes only padding
  after a structurally complete final entity, with malformed inter-entity data
  retained for the existing parsers to reject.
- A dedicated Sven server accepted one VR and one non-VR client concurrently
  over current protocol 49. The VR client submitted OpenXR stereo/UI frames;
  the peer created no OpenXR session and neither connection reported a
  malformed VR sidecar.
- The user-command sidecar unit test round-trips every signed pose field and
  verifies flags, unknown-version consumption and unknown-size consumption.
- A forced protocol-48 client correctly selected GoldSrc mode and was rejected
  by the protocol-49-only Xash server before sign-on. The client-DLL
  `updatevr` fallback is source-covered, but a live fallback test needs an
  actual protocol-48 server rather than weakening the production server.
- The post-review strict Sven smoke at clean engine/game heads additionally
  required VR client API v2 initialization, valid tracking for both simulated
  controllers, complete server-consumer readiness, and a valid sidecar pose
  delivered to the game DLL. It passed with zero resource warnings; artifact
  hashes are retained in `openxr-acceptance-evidence.md`.

Builds, symbols, packet records and map inventories are necessary evidence,
but they do not by themselves prove user-observable gameplay.

## Remaining acceptance plan

1. **Representative campaign starts:** Blue Shift, Opposing Force and
   standalone They Hunger. Missing legally owned expansion data is an input
   prerequisite, not a reason to replace game code.
2. **Behavior scenarios:** divergent HMD/controller aim, left-handed mode,
   physical melee, use/backpack/flashlight, scopes, two-hand stabilization,
   save/load, death/respawn, map transition and sustained-haptic stop/restart.
3. **Sven multiplayer:** run the legacy fallback against a real protocol-48
   server. On hardware, verify RPG muzzle origin, launch direction, guidance,
   collision and the second client's observation all agree with the dominant
   controller pose. The authoritative launch/guidance math and full sidecar
   serialization are source- and unit-verified.
4. **Campaign startup sweep:** only after representative behavior passes, run
   the manifests to find map-specific resource/entity failures. This is why an
   all-map loop exists; it is not a substitute for playing every map.
5. **Physical Linux hardware:** Quest Touch and Valve Index control/haptic
   acceptance.
6. **Windows last:** build, package and repeat the runtime/controller matrix.

Any failure must be reduced to the narrowest existing owner. Reopen the
architecture only if evidence shows that an adapter cannot preserve the
required behavior; untested behavior alone is not such evidence.
