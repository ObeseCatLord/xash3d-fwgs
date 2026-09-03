# Desktop Lambda1VR acceptance evidence

Status date: 2026-09-02

This is the compact, sanitized record for the current end-of-coding Linux
checkpoint. It does not claim physical-controller, campaign-playthrough, or
Windows acceptance.

## Source heads

| Repository | Commit |
|---|---|
| Xash3D FWGS | `8072a190` |
| base Half-Life SDK | `ccff5db9` |
| Blue Shift SDK | `c61372aa` |
| Opposing Force SDK | `861b4cc2` |
| standalone They Hunger SDK | `db11ca05` |
| SevenKewp | `e23c3d42` |
| desktop package/scripts | `c02abf7` |

Every executable/runtime file used by the integrated smoke matched these
committed contents. The Xash documentation-only commit containing this record
necessarily follows the executable Xash head shown above.

## Build and automated checks

- Clean 32-bit Linux OpenXR configure/build at Xash head `8072a190`:
  `./waf configure -o <BUILD> --enable-openxr --enable-tests`, followed by
  `./waf build --alltests`. Result: 15/15 test targets passed; the engine
  harness contained 7,852 passing assertions.
- Base, Blue Shift, Opposing Force, standalone They Hunger, and SevenKewp
  Linux game-DLL builds passed after the explicit sidecar ABI change.
- All six copies of `vr_usercmd_sidecar.h` matched byte-for-byte. Every Linux
  server DLL exported undecorated `VR_BeginPMUsercmdSidecar`,
  `VR_EndPMUsercmdSidecar`, and `VR_UpdateUsercmdVRPose` symbols.
- A focused i686 MinGW fixture compiled the shared typedefs and exported all
  three callbacks as cdecl, undecorated names. Full Windows builds remain the
  explicitly deferred Windows-last acceptance step.
- Package checks passed: interface parity, 58-map They Hunger overlay staging,
  and the strict smoke-harness self-test. The self-test proves a launcher
  outside the staged root, a missing staged artifact, a missing pose-delivery
  marker, or an unapproved resource warning fails the run.

## Commit-stamped integrated smoke

The test used the supplied Sven 3 archive, the current SevenKewp artifacts, a
fresh current-engine stage, simulated Monado Qwerty controllers, and SDL
display index 1. The sanitized command shape was:

```sh
scripts/linux-openxr-smoke.sh \
  --root <STAGE> --game sevenkewp --map c1a0 \
  --runtime <OPENXR_RUNTIME> --seconds 12 \
  --bugcomp gsmrf --display 1 --ipc-ignore-version \
  --set sv_precache_bspmodels=0 --keep-logs <EVIDENCE>
```

Result: PASS using manifest format v2, intentional timeout status 124, zero
resource warnings. The launcher and all five mandatory staged loadable
artifacts were hashed. Required markers all appeared:

- OpenXR initialized;
- game started and local client connected;
- VR client API v2 initialized;
- first frame with valid aim and grip poses for both simulated controllers;
- complete server game-DLL sidecar consumer ready;
- first valid sidecar pose delivered to the game callback;
- first stereo frame and first head-relative UI layer submitted.

Artifact hashes from the generated manifest:

| Artifact | SHA-256 |
|---|---|
| launcher | `29a411d7cee80870591e44995ecbd62fea6b1ef5f69a0ba0ffc79cd5e5695ab0` |
| `libxash.so` | `50dd23700d67836932b40e1bfcc84ee037aa341f5c8f0481bcd893abd8d5a834` |
| `libref_gl.so` | `0d37c591c5ee359fc9ef771d5b7b20f4e2d70220268e9322d240d216e1e5f42b` |
| `filesystem_stdio.so` | `e5ed2dc0dee245abebbcb142ae54361a207b948ed5c5e8d6b0cfc8160aceb927` |
| SevenKewp client | `8d3ad1bd409b534b3a474e20e737714cb31d7578834c9151839fa3601474d1c1` |
| SevenKewp server | `5e5bfd630a847aeb59503bb0968af0032eb8a90c9e9fbe14f9db8bf226c35456` |
| OpenXR runtime manifest | `1a82bbe0b8418ad73bfdc597945f66950a9a4d140b393f7606adb11df3ae865e` |

Raw local logs and temporary stages were deleted after this summary was
recorded. Future acceptance runs can recreate sanitized logs plus
`manifest.tsv` with `--keep-logs`; they are not silently retained.

## Earlier runtime record

Earlier same-day runs, documented in `openxr-completeness-audit.md`, covered
base Half-Life startup, Sven `c1a0`/`c2a3`/`c4a1`, every one of the 58 supplied
They Hunger conversion maps, and a mixed VR/non-VR two-client protocol-49
server. Those runs predate the stricter manifest and therefore remain useful
historical evidence, not commit-stamped acceptance for the final artifact.
