# Sol-max senior-review disposition

Status date: 2026-09-02

The requested sol-max end-of-coding review passed the architecture and rejected
the initial coding freeze/full-runtime-parity claim. Its two P0 findings were
verified in source and corrected without introducing a new renderer, host loop,
game state machine, or pose protocol.

| Recommendation | Disposition | Result |
|---|---|---|
| Retain renderer-owned OpenXR and narrow engine/game adapters | Adopted | The current architecture remains. Team Beef game behavior is reused behind the desktop OpenXR and fixed ABI boundaries. |
| Fix SevenKewp's Windows callback export/calling convention | Adopted | A shared explicit dllexport/cdecl contract now covers all five game-DLL families; Seven's legacy `GiveFnptrsToDll` remains stdcall. Linux exports and an i686 MinGW fixture pass. |
| Make sidecar negotiation depend on a complete game-DLL consumer | Adopted | The server echoes `NET_EXT_VR_USERCMD` only when all three callbacks resolve. No callbacks remain a quiet legacy case; partial callbacks produce one diagnostic and disable negotiation, preserving `updatevr`. |
| Prove API, tracked input, consumer readiness, and accepted pose in smoke tests | Adopted | The harness requires all four plus stereo/UI/game markers and fails resource warnings by default. Its self-test covers missing-marker and warning failures. |
| Retain commit/artifact evidence | Adapted | A final Terra verification exposed optional artifact hashes. The corrected harness now rejects launchers outside the staged root and missing engine, renderer, filesystem, client, or server artifacts; manifest v2 always hashes all of them. It writes sanitized logs only when requested. This compact commit-stamped summary is retained while raw logs/stages are deleted afterward. |
| Add oracle-to-desktop traceability | Adopted | `lambda1vr-parity-matrix.md` maps every in-scope user-visible behavior family and separates source completion from runtime acceptance. |
| Build/test Windows before freezing code | Adapted | The source-level ABI defect is fixed and MinGW-checked now. Full Windows build/runtime remains last, as explicitly ordered by the user; the project is not labeled fully accepted before it passes. |
| Add remote controller/hand rendering now | Rejected | Team Beef did not provide remote IK. Authoritative controller-pose gameplay is in scope; model-attachment remote beam art is reference parity, while remote IK is a separate future feature. |
| Weaken protocol 49 to self-test protocol 48 | Rejected | The `updatevr` fallback remains. Live fallback acceptance requires a real protocol-48 environment; production protocol checks are not weakened to manufacture a test. |
| Declare full Lambda1VR parity now | Rejected | Source traceability passes, but physical Quest/Index behavior, expansion campaigns, live fallback, observed multiplayer RPG behavior, and Windows are still explicit acceptance gates. |

The review therefore changed the implementation and acceptance rails while
confirming the original continuation decision: continue the adapter port; do
not restart or revive the Android engine fork.
