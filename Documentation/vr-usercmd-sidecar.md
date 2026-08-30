# VR usercmd sidecar v1

`NET_EXT_VR_USERCMD` is negotiated through the existing current-protocol
connect `ext` bitmask. A client writes sidecars only after the server echoes
that bit, so an older server never sees extra bytes; a newer server does not
parse them for an older client. GoldSrc protocol packets are unchanged.

The extension is placed after every delta-compressed command in `clc_move` and
before that move's checksum. This is the narrowest packet-local placement: it
keeps the payload under the existing checksum and lets the exact backup/new
command order define the sample order. It avoids adding a `clc_*` command,
changing `usercmd_t`, or using a separate reliable stream (which cannot replay
the pose that belongs to a recovered command).

Version 1 writes an envelope byte pair (`version`, `bytes-per-sample`) followed
by one fixed 35-byte record for each command, oldest through newest. A record
contains flags, two controller ladder angles, and the 15-value weapon/offhand
pose. Positions and velocity are relative to the player eye at 1/8 unit
precision; angles use 1/128 degree precision. Unknown versions are skipped by
the advertised record size. Truncated records drop the client before movement.

The engine keeps a sample next to each client command-ring entry and retains
the last accepted sample for dropped-command replay. PM receives it only via
optional begin/end exports around one dispatch; the base game DLL clears that
transient state immediately afterwards. Missing exports, missing capability,
invalid/stale records, or `vr_controller_ladders 0` use normal view angles.
The server's optional pose export updates the owning player's authoritative
pose before `PlayerPreThink`/`ItemPostFrame`; `updatevr` remains a fallback
while no fresh usercmd pose is available.
