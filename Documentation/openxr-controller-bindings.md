# Desktop OpenXR controller bindings

The action layer exposes the same logical controls for either dominant hand.
`vr_control_scheme` values below 10 use the right hand for the weapon; values
10 or greater use the left. As in Lambda1VR, the engine keeps the legacy
`hand` cvar synchronized (`0` right, `1` left) for aware game DLLs.

| Logical input | Quest Touch path | Valve Index path | Game behavior |
|---|---|---|---|
| Grip pose | `grip/pose` | `grip/pose` | tracked hand grip |
| Aim pose | `aim/pose` | `aim/pose` | weapon/off-hand aim |
| Trigger | `trigger/value` | `trigger/value` | dominant attack; off-hand run |
| Squeeze | `squeeze/value` | `squeeze/value` | dominant alt-attack modifier; tap reload; support stabilization |
| Stick | `thumbstick` | `thumbstick` | off-hand locomotion; dominant turn/inventory |
| Stick click | `thumbstick/click` | `thumbstick/click` | dominant use |
| Primary face | X/A | A | dominant crouch; off-hand flashlight |
| Secondary face | Y/B | B | dominant jump; off-hand menu fallback |
| Menu | left `menu/click` | `system/click` when exposed | menu/Escape; pressing both stick clicks is a runtime-independent fallback |
| Haptic | `output/haptic` | `output/haptic` | per-hand weapon and action feedback |

Index system clicks may be reserved by the runtime. Pressing both stick clicks
therefore opens the menu without taking either A/B gameplay action away from
Index or Quest mappings.

The Khronos simple-controller fallback binds `select/click` to the float
trigger action through OpenXR's boolean-to-float conversion, so simulated
Monado and minimal controllers can still fire.

## Hardware acceptance

For both Quest Touch and Valve Index, test every row with right- and left-hand
weapon modes. Verify active-state loss releases menu/reload/turn latches,
haptics affect the requested hand, and no system-reserved button is the only
way to reach an in-game action. Simulated Monado validates action/session
wiring, but does not replace this physical-device gate.
