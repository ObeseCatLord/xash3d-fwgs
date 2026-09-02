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

Two independent Terra closure audits compared the desktop implementation to
the Team Beef sources. Their source-proven findings were fixed before this
status was recorded. No known Lambda1VR gameplay feature remains stubbed or
deferred in the Linux source path.

| Surface | Source status | Runtime status |
|---|---|---|
| OpenXR stereo, asymmetric FOV, 6DoF head pose and head-relative UI | implemented | simulated-runtime acceptance required |
| Quest Touch and Valve Index actions, handedness and squeeze edges | implemented | physical-controller acceptance last |
| locomotion, room scale, wall pushback, crouch, turn, ladder and recenter | implemented | gameplay acceptance required |
| menu ray, use gestures, backpack actions, flashlight/hand presentation | implemented | gameplay acceptance required |
| comfort mask, scope and two-hand stabilization | implemented | gameplay acceptance required |
| finite and sustained per-hand haptics, including overlap policy | implemented | physical-controller acceptance last |
| weapon mirroring and weapon back-face controls | implemented and archived | presentation acceptance required |
| fixed-size VR client API and negotiated user-command sidecar | implemented with legacy fallback | two-client acceptance required |
| base Half-Life weapons, events, melee, haptics, save/reset and RPG aim | implemented | campaign and RPG acceptance required |
| Blue Shift variant | implemented from the shared base SDK | campaign acceptance requires game data |
| Opposing Force weapons and behavior | implemented in the existing Gearbox DLL | campaign acceptance requires game data |
| standalone They Hunger weapons and behavior | implemented in its existing DLL | campaign acceptance requires game data |
| Sven/SevenKewp VR behavior and They Hunger compatibility classes | implemented in SevenKewp | multiplayer/campaign acceptance required |

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

Builds, symbols, packet records and map inventories are necessary evidence,
but they do not by themselves prove user-observable gameplay.

## Remaining acceptance plan

1. **Simulated Monado, Linux:** stage a fresh install and prove OpenXR session,
   stereo, UI, game DLL negotiation, clean shutdown/restart and desktop mirror
   placement on the non-primary monitor.
2. **Representative campaign starts:** base Half-Life, Blue Shift, Opposing
   Force, standalone They Hunger, Sven's base-campaign conversion and the They
   Hunger multiplayer conversion. Missing legally owned expansion data is an
   input prerequisite, not a reason to replace game code.
3. **Behavior scenarios:** divergent HMD/controller aim, left-handed mode,
   physical melee, use/backpack/flashlight, scopes, two-hand stabilization,
   save/load, death/respawn, map transition and sustained-haptic stop/restart.
4. **Sven multiplayer:** run two clients with negotiated and legacy fallback
   paths. The RPG is blocking: muzzle origin, launch direction, guidance,
   collision and the second client's observation must all agree with the
   dominant controller pose.
5. **Campaign startup sweep:** only after representative behavior passes, run
   the manifests to find map-specific resource/entity failures. This is why an
   all-map loop exists; it is not a substitute for playing every map.
6. **Physical Linux hardware:** Quest Touch and Valve Index control/haptic
   acceptance.
7. **Windows last:** build, package and repeat the runtime/controller matrix.

Any failure must be reduced to the narrowest existing owner. Reopen the
architecture only if evidence shows that an adapter cannot preserve the
required behavior; untested behavior alone is not such evidence.
