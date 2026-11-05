# Tactical FPS Current Setup

Last updated: 2026-10-14

This document is the current implementation snapshot for resuming work. The authoritative gameplay rules and long-term execution order remain in:

```text
ProblemAndSolution/Tactical_FPS_Gameplay_Design_And_Plan.md
```

The older multiplayer repair report is historical troubleshooting material. If it conflicts with this file or the gameplay plan, use the gameplay plan and the current source code.

## 1. Current Product Definition

The project is a server-authoritative 3v3 tactical round FPS built on the UE5 first-person template.

Current packaged-PC connection direction:

- Steam Lobby/Session + a player-hosted authoritative Listen Server using Steam networking/NAT-relay is the chosen multiplayer model. One player hosts and participates; renting or operating an always-on dedicated server is not required.
- The C++ Steam phase is implemented: `OnlineSubsystemSteam`, SteamSockets/relay, native Host/Find/Join/Invite/Leave/Destroy UI, a fail-closed six-player Lobby/Session state machine, provider-qualified identities, Shipping Steam admission, Session-resolved travel/reconnect, bounded failure cleanup, and terminal listen-host loss.
- The player-facing packaged flow is Host/Find/Join/Invite/Leave through Steam rather than manual IP entry. Editor/PIE deliberately uses OSS Null + IpNetDriver because one editor process cannot represent multiple independent Steam accounts; this remains a diagnostic path, not the release transport.
- A newly created room is private `INITIALIZING` until the authoritative gameplay world reaches `BeginPlay`, then becomes discoverable `LOBBY`. Match start publishes `IN_PROGRESS` and closes advertising, presence joins, and invites before the provider Session starts; shutdown publishes `CLOSING` and raises a local admission fence before destruction. Public Find/Join accepts only `LOBBY`. The low-level in-progress opening exists solely for an authenticated inactive roster member to recover the same surviving host/world; authoritative admission still rejects fresh or duplicate-active identities.
- Steam App ID `480` is development-only. A `TCAMP_FPS` product setting filters unrelated Spacewar sessions, but the project's assigned App ID must replace it before distribution. Formal Shipping Steam runtime also requires `UE_PROJECT_STEAMSHIPPINGID=<assigned id>` in `Source/Tcamp_FPS.Target.cs` or an equivalent release definition; the current successful Shipping compile does not prove Steam initialization.
- If the listen host exits, crashes, or loses its authoritative connection, the match ends and clients return to the multiplayer menu. Host migration and recovery after host loss are out of scope.

Locked rules:

- Maximum active roster: 3 players per team, 6 total.
- 1v1 and 2v2 are valid development configurations.
- At least 2 active players are required, with at least one player on each team.
- Fresh players begin with team `255` as unassigned lobby candidates; a seventh connection is non-candidate spectator-only because both 3-player rosters are the maximum supported capacity.
- The only round win condition is eliminating the entire enemy team.
- Capture points and overtime are removed.
- First team to 5 round wins wins the match.
- A match ends after at most 9 completed rounds; the higher score wins, or the match is a draw if tied.
- The world-space player body remains `/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple`. The rifle switches the owner-only first-person arms to RAR's `/Game/InfimaGames/ArtCore/Mannequin/Meshes/SK_IG_FP_Mannequin`; both rifle views now use the same `/Game/InfimaGames/RealisticAssaultRifle/Art/Weapon/Meshes/SK_RAR_AssaultRifle` gun and matching magazine, handguard, and front sight. The first-person gun mounts to `SOCKET_Weapon`, while the world-space gun uses the Manny `HandGrip_R` mount. The pistol retains its previous presentation. The former SciFi runtime presentation and Manny-to-SciFi retarget path remain disabled.
- The fixed-layout treasure rewrite is enabled in C++ (`bTreasureEnabled=true`). The chest stays on the raised central platform; two guards use placed ground-level posts inside a server-authoritative, semi-transparent combat arena. The core claim loop and native fallback chest model were previously accepted in PIE. The revised arena visuals, guard behaviour, and frame pacing still need manual multi-window PIE acceptance.

`ERoundPhase::Overtime`, `TimeAliveAdvantage`, and `OvertimeDraw` remain hidden only for serialized asset compatibility. They are not reachable gameplay states.

## 2. Runtime Phase Flow

| Phase | Duration | Movement and firing | Player damage | Purpose |
| --- | ---: | --- | --- | --- |
| `PreRound` | No timer | Enabled | Disabled | Warmup, player-selected teams, and unanimous ready gate |
| `Buy` | 10 seconds | Frozen and firing disabled | Disabled | Server-authorized equipment purchase |
| `Combat` | 105 seconds | Enabled | Enabled | Elimination combat and hunt intelligence |
| `RoundEnd` | 6 seconds | Frozen and firing disabled | Disabled | Result, economy, and next-round transition |
| `MatchEnd` | 15-second restart delay | Frozen and firing disabled | Disabled | Final result followed by restart |

The transition path is:

```text
PreRound -> Buy -> Combat -> RoundEnd -> Buy ... -> MatchEnd
```

During `PreRound`, players keep game-only mouse capture and may move, look, and shoot. Tactical weapons use server-authoritative infinite reserve ammunition in this phase: magazines still empty normally and the full reload montage still gates firing, but completing a reload does not deduct reserve ammunition. The transition into the first `Buy` respawns each participant and therefore creates the normal finite-ammunition round loadout; a reload completion also re-checks the live phase so the warmup rule cannot cross a phase boundary. Players initially have no team. Shooting `JOIN RED` or `JOIN BLUE` selects that team and marks the shooter ready in one server-authoritative operation; shooting the opposite target before the match starts switches teams if the destination has room. At least two players and both teams are required, and every eligible lobby player must have selected a team and be ready before entering `Buy`. An unselected third player therefore blocks a ready 1v1 from starting. The old controller-side ready toggle/RPC has been removed, so a client cannot bypass the shootable-target interaction.

The gameplay level contains six unique `ATacticalSpawnPoint` lobby slots within the central movement area and three numbered slots for each team along its own flag-side wall. Team selection does not move the player out of the lobby. Once the unanimous ready gate succeeds, the first `Buy` respawn places each team at its wall-side slots; later round respawns use the same team groups. One player uses the middle slot, two use the outer slots, and three use all three. Missing, duplicate, or overlapping authored markers block match start instead of silently stacking players. The marker actors can be moved in the level without changing C++ or Blueprint graphs.

`ATacticalCharacter::TakeDamage` accepts player damage only during `Combat`, which is the final server-side guard against warmup, buy-phase, or round-end damage.

## 3. Round And Match Resolution

- A single eliminated team loses the round.
- After a possible final elimination, the server waits `0.15` seconds and evaluates both teams again.
- If both teams are eliminated in that window, the result is `SimultaneousEliminationDraw`.
- If Combat reaches zero while both teams still have living players, the result is `TimeExpiredDraw`.
- Player count, remaining health, equipment, money, and treasure never break a round tie.
- A draw consumes one of the nine rounds and awards no round point.
- Reaching five wins ends the match immediately.
- After round nine, the higher score wins; equal scores produce match winner `255` (draw).
- A normal score victory/draw locked by the completed round has priority and cannot be reversed by disconnecting during the six-second `RoundEnd` presentation. If the match would otherwise continue, a qualified team with no connected controller at the end of `RoundEnd` forfeits instead of donating empty-team round wins; a connected reconnect spectator counts as present. If both qualified teams are absent, the result is `AllTeamsDisconnectedAbort` with winner `255` and no-result semantics, not a draw.

The current connection lifecycle combines the server-side participant/recovery layer with Steam Session-resolved reconnect in packaged builds. The recovery rules below apply to a non-host client only while the same authoritative host/world remains alive:

- `UTacticalMatchRosterSubsystem` owns a stable server-only participant record independently of the current `PlayerController`. It stores the opaque participant ID, reserved team slot, current connection state, entered/settled round numbers, retained pawn/controller references, and the public reconnect-status deadline. `TeamAPlayers` and `TeamBPlayers` remain connected-controller views, while capacity and reconnect eligibility use the stable reservation.
- An unexpected non-host client disconnect in `Buy` or `Combat` retains a living pawn until that current round's authoritative boundary. Held movement, jumping, firing, aiming, breath hold, and interaction are stopped; velocity is cleared; and a non-replicating `ATacticalDisconnectedController` temporarily owns the preserved `PlayerState` and pawn. The same pawn remains visible, collidable, normally damageable, and keeps its live health, armor, weapon, magazine, reserve ammunition, and transform. Normal physics, moving bases, or external forces may still move it.
- A valid same-round reconnect transfers the preserved `PlayerState` before normal `PostLogin` startup and repossesses that same living pawn during `Buy` or `Combat`, including after the 30-second public countdown expires. If the pawn died or the round reached its boundary, the participant enters restricted teammate spectating. Combat never creates a replacement pawn; an eligible rostered participant who did not enter the round may spawn only during `Buy`, and normal `StartRound` remains the later-round admission boundary.
- Pawn death while offline follows the normal death/drop/elimination path. Public-status expiry only changes the roster row from `Reconnecting` to `Disconnected`; it applies no damage, destroys no pawn, clears no equipment, awards no kill, settles no economy, and changes no score. `EnteredRound`, `LastSettledRound`, and the compatibility `PendingSettlementRound` guard against duplicate round income.
- A `PreRound` disconnect retains the participant record and selected team for the same 30-second grace, but readiness is cleared. Expiry releases the PreRound roster slot. A non-Shipping development `GUEST` whose first credential was never acknowledged is released immediately because the client has not proven that it can reclaim the record; the 30-second PreRound grace applies after that admission is established, or to a UID/PIE identity. Mid-match reserved slots are not released to fresh joiners.
- The client-native reconnect layer blocks gameplay input, shows connection/attempt state, and makes five automatic attempts using delays of `0`, `1`, `2`, `4`, and `8` seconds. A duplicate-active rejection caused by the server still retiring the old connection is handled separately every 2 seconds with a 35-second hard budget and does not consume the five transport attempts. Failure leaves native `RETRY` and `LEAVE MATCH` actions available. Packaged Steam recovery first resolves the current named Session, or rejoins the immutable last joined result when necessary, and then travels through the provider-resolved address. `IpNetDriver` raw-address/token retry remains only the editor/non-Steam development baseline. Driver timeouts are 20 seconds; the separate 30-second public status window begins only when the server processes logout and is not the pawn lifetime or same-round manual-retry cutoff.
- Non-Shipping builds expose `TacticalPauseAutoReconnect`, `TacticalResumeAutoReconnect`, and the preferred atomic test command `TacticalTestDisconnect` on the exact local `ATacticalPlayerController`; the older per-GameInstance `tactical.Reconnect.AutoRetry 0|1` command remains compatible. `TacticalTestDisconnect` first disables automatic recovery for that one PIE client and then routes `NET GameNetDriver NETDISCONNECT`, eliminating numeric-value and wrong-window ambiguity. Failure immediately exposes the native manual `RETRY`/`LEAVE MATCH` actions, and manual retry deliberately bypasses the switch. These helpers have no behavior in Shipping.
- Reconnect HUD initialization is driven from retained replicated state. Client `BeginPlay` no longer replaces authoritative health or reserve ammunition with class/DataTable defaults, HUD delegates are rebound after the widgets exist, and a late-resolving `CurrentWeapon` immediately pushes its magazine and reserve snapshot. Initial/reconnect health synchronization updates only the `LifeBar`; the damage vignette is driven exclusively by a transient owner-client cue sent after new authoritative damage is actually applied. The cue is not persistent replicated state, so a valid same-pawn reconnect shows preserved health and ammunition without replaying an old hit effect.
- Reconnect admission uses a stable identity, a per-match Match ID, a server-generated per-participant 128-bit bearer credential, and protocol version `1`. The complete claim is validated in `PreLogin` and `InitNewPlayer`; partial claims, wrong match/token, version mismatch, abandoned records, and duplicate active connections are rejected. Credential rotation keeps the client-proven previous value valid until the owner receives and acknowledges the replacement, preventing a second interruption from locking out the legitimate player. A first-issue credential must also be acknowledged before team selection; the same UID-bound identity has one controlled recovery if that first RPC is lost. Once recovery starts, admission protection remains armed through automatic failure or `CancelReconnect` until success, leave, or an explicit new remote travel. While armed, owner admission is accepted only from an `NM_Client` controller for the already cached Match ID: the temporary `NM_Standalone` fallback world created after `NETDISCONNECT` can no longer overwrite the real server credential or send a false ACK.
- Shipping fails closed unless the active online provider and the participant identity are Steam. The Steam Auth packet-handler component is enabled before gameplay admission, provider-qualified identity keys prevent cross-provider collisions, and an inactive record can be reclaimed only by the same Steam identity. Editor supports explicit `TacticalPIEReconnectId`; non-Shipping local/Development builds can still use isolated `GUEST` identities plus owner-only bearers for IP-driver tests.
- A deliberate native `Leave Match` while the server connection is open calls the separate authoritative abandonment path, bypasses recovery, runs the normal uncredited death/drop path once, destroys the pawn, clears equipment/settlement, cleans up the named Session, and returns to the frontend; a two-second local fallback prevents a lost acknowledgement from trapping the client. If the client is already offline, the button can only leave locally while the unreachable host continues its retention policy. A host selecting `End Match` broadcasts `HostLost`/no-result, freezes the match, allows the reliable client notice a short delivery window, destroys its Session, and returns every reachable peer to the frontend.
- `ATacticalPlayerState` replicates a privacy-safe public connection status (`Connected`, `Reconnecting`, `Disconnected`, or `Abandoned`) and the absolute public-status-window end time without account, address, Match ID, or credential data. A native hold-Tab scoreboard derives remaining time from `GameState`'s synchronized server clock and presents both fixed 3-player team columns. A mid-match disconnect or deliberate leave keeps one row while its roster slot remains locked; a lobby leave still releases its slot. The controller handles Tab in normal game input, while `UBuyMenuUI` intercepts Tab preview/key-up events so focused purchase controls cannot consume it as UMG navigation.
- The older `DisconnectedPlayers` snapshot remains only as a degraded compatibility fallback for economy/loadout state when the preserved object graph cannot be retained. It is no longer the primary reconnect mechanism and never reconstructs exact live pawn state.
- Selecting a spectator target now starts a server-validated, owner-only HUD snapshot. The server accepts only a living active pawn on the viewer's preserved team, and only while the viewer has no pawn or a dead pawn. This also supports a valid reconnect whose `bIsActiveParticipant` remains false until the next round. Exact target ammunition is never changed to global replication.
- The native spectator panel explicitly shows `SPECTATING`, target name/team/index, exact health, exact armor, weapon tier, magazine count/capacity, and reserve ammunition. The dead player's health/ammo and armor widgets are hidden while observing and restored to their exact previous visibility on respawn.
- The observed pawn's real first-person Manny, active first-person weapon mesh, character Tick, and weapon Tick are enabled only in the observing client's world. The observer locally hides that same pawn's real third-person Manny so the camera cannot see a duplicate body. Every changed visibility, animation, and Tick state is cached and restored on target switch, target destruction, round transition, respawn, or controller teardown; cleanup never re-shows a body that died during observation.
- Client-requested engine restarts are rejected after match start, so a waiting reconnect cannot call the base restart RPC to spawn during Combat. `StartRound` is the only active-match respawn authority.
- The restart gate normally prevents `Super::PostLogin` from spawning a late-join pawn; the spectator helper also destroys any pawn supplied by an alternate path, preventing an unowned collision/damage actor from remaining in the world.
- Whole-world/PIE teardown skips reconnect preservation and elimination evaluation. This prevents stop-PIE cleanup from producing a false invariant failure or a fake round result.

## 4. Economy And Equipment

### Economy values

| Item or event | Value |
| --- | ---: |
| Starting money | 1000 |
| Maximum money | 9000 |
| Valid enemy-player kill | 200 |
| AI kill | 0 |
| Round win | 3000 |
| First consecutive loss | 1700 |
| Second consecutive loss | 2100 |
| Third and later consecutive loss | 2500 |
| Draw | 1700 |
| Treasure team reward | 500 per eligible stable-roster teammate, exactly once |

### Prices

| Equipment | Price |
| --- | ---: |
| Default pistol | 0 |
| Upgraded pistol | 500 |
| Rifle | 2500 |
| Sniper | 4000 |
| Light armor | 650 |
| Heavy armor | 1250 |

Purchase RPCs validate the current phase, active-player state, living pawn, price, balance, item configuration, and equipment component on the server. A failed weapon spawn/equip rolls back the money deduction.

Armor tuning and feedback are now defined in C++ as a safe baseline because the current armor DataTable has zeroed reduction fields:

| Armor | Durability | Body absorption | Head absorption |
| --- | ---: | ---: | ---: |
| Light | 100 | 50% | 0% |
| Heavy | 200 | 50% | 50% |

Absorbed damage consumes the same number of durability points. The local native HUD shows armor as a compact blue rounded progress bar above the health bar, with the armor type and exact `current / maximum` durability inside the bar. It is hidden when the player has no armor. An actual armor absorption triggers the blue shield sphere for approximately 0.3 seconds on server and clients. Light armor intentionally does not flash for a head hit because it provides no head protection.

Implemented equipment behavior:

- Magazine ammo, reserve ammo, reload, and replicated ammo HUD updates exist.
- A survivor saves weapon tiers and remaining armor durability for the next round.
- A dead player loses non-default saved weapons and armor.
- Death can drop the most valuable non-default weapon, at most one weapon in the current implementation.
- The default pistol is restored when the player has no valid sidearm.
- Picked-up weapons are saved only if the collector survives.
- An unexpected `Buy` or `Combat` disconnect does not clear equipment while the retained pawn remains alive in the current round, regardless of public-status-window expiry. Equipment is cleared only if that pawn actually dies, or if initial preservation fails and the participant must use the pawnless compatibility fallback. The compatibility snapshot stores only canonical economy/loadout state, not the retained pawn's exact live ammunition or transform.
- Weapon replacement now publishes and broadcasts the new `CurrentWeapon` before the old actor is retired. The old actor is stopped, hidden, detached, removed from the replicated inventory, and kept alive for a one-second replication grace period before destruction. Clients also track destruction of the current weapon and normalize a stale/pending-kill reference to `nullptr` before notifying Blueprint listeners.
- During legitimate client replication gaps, the character uses the project's unarmed third-person AnimBP and clears its weapon-specific first-person AnimBP. This prevents `ABP_TP_Rifle` and `UI_Tactical` from repeatedly dereferencing a null or pending-kill weapon.

Snapshot and next-round persistence store weapon tier and armor durability, not remaining magazine/reserve ammunition. A pawn retained through its current round keeps its actual live ammunition because that actor is not reconstructed; newly spawned round weapons still begin with configured ammunition. This is a design detail to confirm during balance work.

## 5. Actual Map And Asset Topology

Current startup and PIE map:

```text
/Game/FirstPerson/Lvl_FirstPerson
```

The map's World Settings select:

```text
/Game/Variant_Shooter/Blueprints/BP_TacticalGameMode
```

Existing Blueprint assets are still used as subclasses and configuration containers:

```text
/Game/Variant_Shooter/Blueprints/BP_TacticalGameMode
/Game/Variant_Shooter/Blueprints/BP_TacticalPlayerController
/Game/Variant_Shooter/Blueprints/BP_TacticalCharacter
/Game/Variant_Shooter/UI/UI_Tactical
/Game/Variant_Shooter/UI/UI_BuyMenu
```

The active player presentation asset is:

```text
/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple
```

The former SciFi presentation and retarget assets remain in the project but are not used by the current runtime player-presentation path:

```text
/Game/SciFITrooper_Man_03/SkeletalMesh/SK_SciFITrooper_Man_03
/Game/Variant_Shooter/Anims/Retarget/IK_TcampManny
/Game/Variant_Shooter/Anims/Retarget/IK_SciFiTrooper
/Game/Variant_Shooter/Anims/Retarget/RTG_TcampManny_To_SciFiTrooper
```

`CharacterMesh0` remains `SKM_Manny_Simple` for the third-person body. The default first-person mesh is Manny, but the active rifle changes it to RAR's `SK_IG_FP_Mannequin` so it can use the imported rifle arm animations. There is no active duplicate presentation body or SciFi retarget bridge. The third-person body continues to own world animation, collision/headshots, and death/ragdoll behavior. The owner-only first-person component uses weapon-specific animation graphs and montages; its camera is a sibling under a stable eye-height pivot at runtime, not an animated head-bone child.

The rifle's first-person gun mounts to RAR's `SOCKET_Weapon` and the world gun attaches to the third-person Manny `HandGrip_R`. These are separate render components but now reference the same RAR gun asset and matching modular parts. The RAR mesh has a `GripPoint_002` support-hand socket for the existing third-person rifle IK and a `Muzzle` socket shared by the two viewpoints. The world gun also runs the RAR gun-body animation graph and receives its fire/reload montages; the world character remains on the existing Manny third-person rifle animation. The pistol still uses Manny and `ABP_FP_Pistol` for its first-person presentation.

The pistol AnimBP starts with `CopyPoseFromMesh`, followed by `Ctrl_HandAdjusment_Pistol` and the `Arms` montage slot. After the eye-pivot reparenting, there is no skeletal parent above the first-person mesh, so C++ explicitly binds the CopyPose source to the character's world Manny mesh, orders its animation tick after that source, and refreshes the otherwise owner-hidden source bones while pistol CopyPose is active. The pistol Control Rig's left-arm IK is enabled and its support-hand/shoulder defaults are tuned for a visible two-handed hold without a near-camera sleeve spike. The montage slot follows the rig, allowing reload animations to release the support hand. The active RAR rifle keeps its separate imported first-person animation path.

The first-person mesh evaluates continuously only for the locally controlled pawn or the one pawn currently being observed. Ordinary remote proxies and dedicated/listen-server copies keep the owner-only first-person path throttled. `PawnClientRestart` restores the current weapon's first-person mesh, AnimBP, and authored weapon mount when reconnect returns a retained pawn to local control, so the reclaimed player does not appear with a reference-pose body or missing gun.

Spectating reuses the target's real presentation components instead of manufacturing a second visual body: locally hide the target's third-person Manny, expose and animate its weapon-specific first-person mesh and weapon, and restore all cached state when observation ends. This preserves the target's first-person animation language while preventing the observer from seeing both the first- and third-person bodies at once.

At BeginPlay, C++ captures the authored head-camera eye position and reparents both camera and owner-only arms beneath a capsule-mounted eye pivot without changing their initial world placement. The active RAR rifle has no pitch-driven arm animation, so the pivot follows local controller pitch or replicated remote view pitch when spectated; the gun follows its arm socket. The legacy pistol AnimBP already drives pitch with its Control Rig, so its pivot stays level to avoid double correction. The camera remains at the pivot origin, preventing the eye from orbiting the arms root during vertical look. C++ handles this migration; no Blueprint hierarchy edit is required.

On 2026-10-10, Development Editor and Development Game builds succeeded, the full `Tcamp.Tactical` automation group passed `19/19`, and two-player PIE probes confirmed rifle arms/gun pitch tracking at `0° / ±45°`, stable camera position, the separate pistol pitch path, and remote view-target pitch reconstruction.

On 2026-10-11, the pistol CopyPose source/tick repair compiled successfully, and the pistol Control Rig left IK and support-hand/shoulder offsets were saved. Fresh-process rendered probes confirmed a visible support hand at idle, `+35° / -35°` pitch, movement, fire and reload; palm position returns to the pistol support socket after each montage, while reload visibly releases the hand. The wrist intentionally retains some rotational difference from the socket to avoid the severe close-camera sleeve deformation seen with full alignment. The full `Tcamp.Tactical` automation group passed `24/24` (`Saved/Automation/PistolLeftHandFull20261011/index.json`). A human-operated two-player PIE pass, especially spectator/reconnect presentation and subjective wrist appearance, remains the final visual acceptance step.

The previous `UTacticalPlayerRetargetAnimInstance`, IK rigs, retargeter, SciFi components, and hand-alignment helpers are retained as inactive implementation history so a future final character can be evaluated or migrated without losing the work. They are not instantiated as the current player's visible presentation and are not valid attachment targets for live player weapons.

Model-level red/blue/neutral material tinting is temporarily disabled because the SciFi material instances are no longer the visible player body. Team selection, replicated team ownership, friendly/enemy rules, the top-left HUD team label, roster columns, and spectator team text are unchanged. A later art pass must add a deliberate Manny-compatible team-color treatment before model tint can again be used as a gameplay readability guarantee.

Relevant data assets include:

```text
/Game/Variant_Shooter/Blueprints/Pickups/Data/DT_TacticalWeaponData
/Game/Variant_Shooter/Blueprints/Pickups/Data/DT_ArmorData
```

The current core-flow work does not require the user to edit Blueprint graphs manually. C++ supplies fallback input contexts, ready targets, missing team starts, the ready-status panel, the spectator-status panel, the hold-Tab roster/status panel, damage gating, and default-sidearm restoration. The project is not Blueprint-free: weapon mappings and part of the HUD presentation still use the existing assets above.

The implemented treasure version keeps the macro map skeleton fixed and uses the single chest already placed on the central platform. It no longer selects among three C++ candidate sites. No per-round layout-variation manager is implemented yet. A later increment may add manually reviewed local-layout presets; arbitrary procedural/PCG competitive geometry is not planned.

## 6. C++ Ownership

- `UTacticalGameInstance`: client network/travel-failure filtering, sanitized remote-address cache, one bounded `0/1/2/4/8` retry loop, native reconnect overlay/input block, owner-only admission cache, manual retry, and deliberate leave.
- `UTacticalMatchRosterSubsystem`: server-generated Match ID and server-only stable participant records, reconnect credential generation/rotation/validation, reserved team-slot counting, connection state, retained pawn/proxy ownership, and entered/settled-round metadata.
- `TacticalNetworkAdmission`: protocol version `1`, complete-claim classification, recovery-source/match isolation, and constant-time Match ID/credential comparison used by server admission and tests.
- `ATacticalGameMode`: authoritative player-selected team rosters, stable-slot capacity checks, ready gate, phase flow, validated authored lobby/team respawns, elimination window, round/match/whole-team-disconnect-forfeit resolution, connected/offline economy settlement, `PreLogin` admission, 30-second public-status servicing, current-round body retention, rollback-safe `PlayerState`/pawn takeover, same-pawn repossession or spectator fallback, explicit abandonment, hunt scheduling, and the fixed chest's 75/70-second announcement/activation timeline.
- `ATacticalGameState`: replicated phase/end time, round, score, round result, reason-aware match result, one-shot match-result serial, shared hunt state, and one atomic late-join-safe treasure snapshot with server timestamps.
- `ATacticalPlayerState`: money, loss streak, team, ready/active flags, saved armor/weapon tiers, compatibility pending settlement, replicated privacy-safe public connection status, and the absolute public-status-window end time. The legacy property name remains for compatibility.
- `ATacticalPlayerController`: per-frame local input/control rotation, independently throttled HUD/roster refresh, mouse-capture policy, input-context fallback, buy UI, purchase RPCs, phase-driven gameplay-HUD visibility, local combat/phase/armor/treasure UI, the native hold-Tab roster/status panel plus creation/binding/visibility diagnostics, round/sequence-fenced per-team hunt delivery, teammate-only spectating, server-validated owner-only observer snapshots, reversible local exposure of the observed pawn's first-person Manny, suppression/restoration of its real third-person Manny while observing, owner-only reconnect admission RPC, server leave RPC, and spectator diagnostics. Confirmed player hits arrive only through the owning controller's unreliable client RPC after the authority reports positive applied damage.
- `UBuyMenuUI` / `UTacticalShopItemIconUI`: the existing `UI_BuyMenu` Blueprint keeps its six authored buttons, but native C++ lays them out as four equally sized shop columns: upgraded pistol, rifle, sniper, then light/heavy armor stacked. Native handlers replace the old button delegates, including the previously unbound heavy-armor button; the existing server purchase RPCs still decide whether a transaction is valid. Names, prices, images, combat stats, live funds, purchase status, and the 10-second countdown are displayed without editing the Blueprint graph. The three shop weapon rows in `DT_TacticalWeaponData` now reference transparent, offline-rendered images of the actual pistol, assembled RAR rifle, and runtime sniper meshes under `/Game/Variant_Shooter/UI/ShopIcons`; their PNG sources are in `SourceArt/ShopIcons`, and `scripts/capture_shop_weapons.py` plus `scripts/compose_shop_icons.py` regenerate them. Missing `Icon` values still fall back to lightweight vector illustrations (or the older authored RAR HUD icon for the rifle). Armor percentages display the effective native fallback when both currently authored reduction fields are zero.
- `ATacticalDisconnectedController`: transient, non-replicating server proxy that keeps the preserved `PlayerState` and live pawn inside the normal damage/death pipeline through the current round, including after the public status window expires, without generating player input.
- `ATacticalCharacter`: phase freeze, Combat-only damage, armor processing, equipment persistence, death drop, authoritative treasure interaction/firing lock and cancellation, continuous movement-input handling, remote teammate camera-pitch reconstruction from the replicated aim pitch, direct visible Manny first-/third-person presentation, direct per-view `HandGrip_R` weapon attachment, reconnect-safe restoration of the first-person AnimBP, smooth owner-view ADS FOV and arm/gun sight alignment, and an owner-only first-person fire-montage entry point.
- `UTacticalPlayerRetargetAnimInstance`: retained but inactive Manny-to-SciFi implementation history. The current player path does not instantiate it, run its retarget/FABRIK/finger corrections, or attach a live player weapon to a SciFi presentation component.
- `AShooterAIController`: generic NPCs retain StateTree behaviour, while treasure guards use one dedicated server-only combat brain with stable target selection, close-range retreat hysteresis, safe firing-position selection, request-ID-owned navigation, independent planning/movement throttles, exclusive leash return, and lifecycle cleanup.
- `AShooterNPC` / `AShooterWeapon`: guard torso aiming uses only the weapon's authoritative spread layer; guard reloads consume real reserve ammunition and remain fire-locked for at least the NPC-specific visible reload animation.
- `UArmorComponent`: server-authoritative armor absorption, replicated durability state, and multicast shield-hit feedback.
- `UArmorStatusUI`: native exact-value armor readout that requires no Blueprint event wiring.
- `UWeaponInventoryComponent` / `AShooterWeapon`: authoritative firing, switching, reload, magazine/reserve ammo, replicated weapon state, current-weapon lifetime tracking, and safe old-weapon retirement ordering. Owner-local fire-montage prediction is keyed by weapon/burst/shot and deduplicated against the authoritative multicast; start/stop/reload requests validate the expected current weapon, while server damage and hit confirmation remain unchanged.
- `AShooterWeapon` / `ATacticalBulletTracer`: one unreliable server shot-result multicast carries bounded cosmetic endpoints and the existing impact list; each local viewer chooses its visible gun muzzle and spawns a short-lived non-replicated streak using the standalone `M_TacticalTracer` material. The shot-ID frequency and `tc.Tracer.Enabled` toggle affect presentation only.
- `ATacticalReadyTarget`: team-colored, shootable `JOIN RED` / `JOIN BLUE` interaction that requests one atomic team-selection-and-ready operation from GameMode.
- `UReadyLobbyUI`: native persistent team badge plus PreRound team-selection, roster-count, ready, and team-switch prompts. The compact team badge is left-aligned below the health bar so it does not overlap the armor or health HUD.
- `UShooterCombatFeedbackUI`: native full-screen combat feedback. The visible reticle now uses the RAR `T_IG_ATT_Sight_Dot_03_D` transparent sight texture and server-confirmed hits use `T_IG_UI_Hitmarker`; the old four-segment crosshair and line-art hit marker remain only as missing-asset fallbacks. The textures' transparent padding is accounted for in their screen sizes. The hit marker keeps its existing `0.16s` fade and is still shown only after positive server-applied enemy damage. The widget also draws eight C++-generated directional damage masks and one omnidirectional fallback mask. A cardinal hit covers its complete corresponding edge: front maps to the full upper edge, back to the full lower edge, and left/right use the complete side edges. The edge falloff uses a power-`2.0` high-contrast base plus a soft semi-elliptical inward bulge, so the outer border is dark while the center-facing portion quickly becomes faint. A diagonal hit shifts that arc toward the source side, soft-unions two complete neighboring edges, and ramps from a visible `0.32` far-end weight to full strength at the shared source corner. The dark-red overlay peaks at `0.70` opacity; the screen center and opposite-edge centers remain transparent. The nine `128x128` masks are built once per local widget rather than evaluated per pixel every frame. Every direction owns an independent pulse, allowing crossfire from multiple sides to coexist. Each pulse uses a smooth `0.08s` fade-in, `0.10s` hold, and `0.32s` fade-out; repeated damage from the same sector restarts from the opacity already on screen instead of flashing back to transparent. The crosshair remains visible during `PreRound` so players can shoot the team/ready targets, and is hidden while spectating.
- `UTacticalUI`: the authored circular `Crosshair` widget is retained as a collapsed structural marker so C++ can still locate and isolate its ancestry during `PreRound`; the native RAR-texture reticle replaces its visible output. Score, timer, phase, avatar, health/ammo, and armor branches remain hidden during readiness, then restore normally after it. No Blueprint graph edit is required.
- During a confirmed hit, `UShooterCombatFeedbackUI` draws only the RAR hit marker, not the normal reticle. The marker replaces the reticle for its `0.16s` pulse; repeated hits restart that interval, and the reticle returns when the timer reaches zero. Spectating/hidden-HUD transitions clear the pulse and reject delayed notifications.
- `USpectatorStatusUI` / `FSpectatorHudSnapshot`: native bottom-center spectator presentation plus the pointer-free replicated target state used to populate it.
- `UReconnectStatusUI`: native full-screen connection-loss, automatic-attempt, awaiting-admission, failure, retry, and leave presentation; it requires no Blueprint widget wiring.
- `UTacticalRosterStatusUI`: native centered hold-Tab 3v3 roster with team score, round, local-player marker, life/participation state, public connection state, and server-clock-derived status-window countdown; it requires no Blueprint widget wiring.
- `UMatchResultStatusUI`: native special-result banner for disconnect forfeits and technical no-result endings. Abort/host-loss reasons suppress the legacy Blueprint draw result, and late-binding clients catch up through `MatchEndSerial`.
- `UHuntIntelStatusUI`: native Blueprint-independent top-center hunt warning and a pool of three short-lived approximate world-position markers. Markers are projected only in the owning player's viewport, fade locally, do not spawn replicated actors, and never resolve or track an enemy pawn on the client.
- `UTreasureStatusUI`: native Blueprint-independent treasure state panel, synchronized activation countdown and claim progress, plus one distance-labelled world marker that clamps to the viewport edge without querying actors.
- `ATreasureChest`: authoritative two-guard lifecycle, exclusive first-valid 2.0-second claim with idempotent retry handling, cancellation, exactly-once stable-roster reward, per-round cleanup/reset, and replicated actor-local Blueprint presentation hooks.
- `ATacticalSpawnPoint` / `ATacticalGuardSpawnPoint`: editor-movable, numbered lobby/team and guard-post markers. The level owns their transforms; server validation rejects incomplete or overlapping groups before use.
- `ATacticalGuardArena`: editor-movable/resizable replicated cage with four light translucent side faces, no visible roof/floor, and no physical player collision. Its oriented area constrains AI targets and paths; its finite side faces drive projectile, hitscan, and damage eligibility. Players may cross the sides, but shots crossing an active side face are blocked, including outside-to-outside trajectories through the cage.

## 7. Hunt Intelligence And Treasure Status

The hunt-intelligence server path and native C++ presentation are implemented:

- 30 to 16 seconds remaining: pulse every 8 seconds with approximately 300-600 cm error.
- 15 to 0 seconds remaining: pulse every 4 seconds with approximately 100-250 cm error.
- Only living enemy locations are sent to each team.
- State is cleared outside Combat and on round transitions.
- `UHuntIntelStatusUI` clearly distinguishes the 8-second normal scan from the 4-second escalated scan, replaces each pulse instead of accumulating markers, projects the server-obfuscated coordinates into screen space, clamps off-screen information to the viewport edge, and expires normal/escalated markers after 3.0/2.25 seconds.
- Every pulse carries its authoritative round number and a per-round sequence. The owning client rejects old, duplicate, wrong-round, out-of-phase, and out-of-window pulses, so an RPC delayed across a phase or round cannot redraw stale information.
- Living players receive their own team's data. A dead or pawnless player receives coordinates only while the authority has accepted a living teammate as that controller's spectator target; an unassigned player or a spectator with no valid target receives none.
- The active/escalated warning is non-sensitive shared phase-timing information derivable from the public Combat clock; only enemy coordinates use the strict living-player/validated-spectator authorization gate.
- A player death sends a reliable sequence fence and clears all current anonymous marker samples. Phase changes, round end, match end, spectator-target changes, possession restoration, and controller teardown also clear the appropriate marker or full warning state.
- Gameplay widgets now share the per-player screen layer with deterministic Z-order, and the controller idempotently binds/rebinds GameState UI delegates if replication or world replacement makes GameState available after controller `BeginPlay`.

The former `UTacticalUI` Blueprint events remain as optional compatibility hooks, but the complete default presentation no longer depends on Blueprint wiring. UE 5.7 Development Editor compiles and links with the native presentation; broader multi-window timing, edge-case, and balance coverage remains in the full acceptance matrix.

The fixed-layout treasure rewrite is implemented and enabled in C++:

- The macro map skeleton remains fixed. The level has one authored chest on the raised central platform; authority uses that single site each round rather than randomly moving the chest.
- At 75 seconds remaining in `Combat`, both teams receive the same site announcement. At 70 seconds remaining, the selected chest attempts to activate with exactly two guards; incomplete guard creation fails closed and does not expose an unguarded active chest.
- The chest remains guarded until both tracked guards die. AI guard kills grant no direct money.
- Each guard is created at one of two authored `ATacticalGuardSpawnPoint` markers on the ground around the platform, projected onto the local NavMesh. Activation fails closed if either marker is missing, outside the arena, on the raised platform, too close to the other guard, unavailable to navigation, collision-blocked, or unable to produce a complete pair. The projected post is that guard's return destination.
- At activation the placed `ATacticalGuardArena` becomes visible and active, then closes after claim or at the round boundary. Its light translucent barrier has four side faces and no roof or floor; the visible panels have no physical collision, so players can walk through them. Guard selection and path points remain constrained to the authored area. The server applies the four finite side faces as a logical bullet/damage boundary for hitscan, penetration, projectiles, explosions, and final player/guard damage, including outside-to-outside trajectories through the cage. Shots that do not cross a side face remain valid. The level instance is sized to enclose the central AI NavMesh and both ground guard markers.
- A dedicated C++ guard brain replaces the generic roam/sniper StateTree for treasure guards. It maintains brief occlusion memory, uses full close-range awareness, backs away below 450 cm, leaves retreat after 650 cm, stops to fire when it has a valid shot, and otherwise searches for a safe position without routing through the player.
- Candidate planning and actual `MoveTo` requests have separate 1.25-second limits. Cheaply scored candidates are path-checked best-first in rotating batches of at most six paths per planning pass, so an unreachable top group cannot permanently starve lower-ranked routes and a single decision cannot spike the frame. Routes that leave the guard area or pass too close to the player are rejected. Active requests are matched by ID, and return-home finishes only after physical arrival at the owning guard's post. This removes the previous high-frequency invalid/aborted navigation loop.
- Claiming is server-authoritative: the first valid claim request receives exclusive ownership. The existing `IA_Interact` mapping is bound to `E`; a claimant must continuously hold it for 2.0 seconds and cannot fire while claiming. Repeated start requests from that claimant preserve the original timer and firing lock; competing requests are rejected. Releasing interaction, leaving range, dying, losing active-round eligibility, or phase/state invalidation cancels the claim; nonlethal damage alone deliberately does not reset it.
- A completed claim awards 500 money exactly once to each eligible member of the claimant team's stable roster who entered that round. Eligibility includes a dead teammate and a temporarily offline but non-abandoned participant; each balance remains capped at 9000. The claimant additionally heals by at most 40 without exceeding maximum health.
- Treasure grants no AI-kill money, armor repair, overshield, speed buff, damage buff, or round point, and it never changes round resolution.
- Chest, guards, claim state, interaction offers, timers, and replicated public state are cleared or reset at every round boundary and teardown path.
- `UTreasureStatusUI` presents `Announced`, `Guarded`, `Unlocked`, `Claiming`, and `Claimed` in native C++, using synchronized server time for the activation countdown and 2.0-second progress. It shows the claimant's name/team and one distance-labelled world marker clamped at screen edges without client actor searches. Remote clients also receive the chest Blueprint's optional unlock, claim-start/cancel, loot, and close presentation events.
- The placed `BP_TreasureChest` has no authored Static Mesh. When `Chest Mesh` is empty, native components build the accepted visible brown-and-gold placeholder from the project's rounded prototype mesh, including a lid that opens after both guards die. Assigning a future art asset to `Chest Mesh` automatically suppresses the placeholder, so no Blueprint edit is required for the current build and no code removal is required for the later art pass.

`bTreasureEnabled` is `true`. The core claim loop and fallback model appearance were previously accepted. The new fixed-site, guard-post, and cage rules pass a two-local-player runtime smoke and C++ rules tests; full rendered multi-window visual, handling, and frame-pacing acceptance remains open. Optional future curated local-layout presets are unimplemented.

## 8. Runtime Diagnostics And Automated Rules Tests

Tactical flow events use the searchable log category:

```text
LogTacticalFlow
```

The log records only state boundaries, not per-frame Tick output. Important events include:

```text
ServerInitialized
PlayerJoined / PlayerLeft / PlayerReconnected
ConnectionAdmissionRejected / ReconnectAdmissionIssued
DisconnectGraceStarted / DisconnectStatusWindowExpired / DisconnectGraceCommitFailed
RosterParticipantStatePrepared / RosterParticipantReclaimed
RosterParticipantReconnectRolledBack / RosterParticipantReconnectFailed
ReconnectEligibleNextRound / LobbyReconnectGraceExpired / ParticipantAbandoned
DisconnectStateSaved / DisconnectStateNotSaved / ReconnectIdentityUnavailable
SpectatorPolicyApplied
SpectatorTargetSelected
SpectatorHudTargetAccepted / SpectatorTargetRejected
TeamSelected / TeamSelectionRejected / ReadyGateEvaluated
MatchStarted / RoundStarted / PhaseChanged
PurchaseResult
PlayerKilled / EliminationResolutionScheduled / EliminationResolutionResolved
EconomySettlement / EquipmentClearedOnDeath / PlayerRespawned
RoundEnded / DisconnectMatchTermination / MatchEnded
TreasureSiteSelected / TreasureSiteAnnounced / TreasureActivated / TreasureActivationFailed
TreasureGuardKilled / TreasureClaimStarted / TreasureClaimCancelled / TreasureClaimCompleted
TreasureEconomyAward / TreasureRoundDisabled
ClientPhaseObserved / ClientRoundResult / ClientMatchResult
```

Client recovery events use `LogTacticalReconnect`, including `ReconnectStarted`, `ReconnectScheduled`, `ReconnectAttempt`, `ReconnectTransportConnected`, `ReconnectSucceeded`, `ReconnectStopped`, `ReconnectManualRetryRequested`, and `ReconnectLeaveMatchRequested`. Online room operations use `LogTacticalSession`. Project-authored logs omit Match IDs and credential values. Packaged Steam reconnect uses the Session-resolved address and same authenticated Steam identity without appending the bearer claim to its travel URL; only the non-Steam diagnostic path retains the raw-IP URL-option proof.

Client observation events include the PIE world name and net mode, and repeated synchronization of the same phase is not logged as another transition. Competitive results use `Winner=RED`, `Winner=BLUE`, or `Winner=DRAW`; technical abort/host-loss results use `Winner=NO_RESULT`. Purchase events use stable `MoneyBefore` and `MoneyAfter` fields for both success and failure.

Development builds also validate non-fatal invariants at important state boundaries: team capacity and uniqueness, roster/PlayerState agreement, the limited PreRound unassigned-candidate state, ready/team validity, money/loss-streak bounds, phase timers, removed-overtime protection, round synchronization, score bounds, and elimination-window phase ownership.

Pure rule tests are located at:

```text
Source/Tcamp_FPS/Private/Tests/TacticalRulesTests.cpp
Source/Tcamp_FPS/Private/Tests/ShooterAIRulesTests.cpp
Source/Tcamp_FPS/Private/Tests/PlayerPresentationTests.cpp
Source/Tcamp_FPS/Private/Tests/TacticalTracerTests.cpp
Source/Tcamp_FPS/Private/Tests/TacticalBodyHitFxTests.cpp
```

They cover the two-player and full-3v3 start gates, unanimous readiness, an unselected player blocking start, per-team capacity, elimination and timeout edges, qualified empty-team elimination, match resolution, current-round body retention after status expiry, same-round reclaim, forfeit/abort distinction, normal-score-result priority, disconnect/teardown settlement gates, the active-match restart lock, teammate-only spectator information gates, stable participant records and reserved slots, deferred-cleanup participant-generation isolation, reconnect credential rotation/validation, reconnect-claim shape and protocol compatibility, absolute status-deadline countdown conversion, shared win/loss/draw economy transforms, locked economy constants, treasure candidate/timeline/claim/reward eligibility rules, guard retreat hysteresis, independent path-planning and move-request throttles, movement bounds, retreat direction, return-request ownership, mouse-input configuration, top-left HUD geometry, the Manny world-body configuration, weapon-specific first-person overrides, matching RAR rifle assets in both views, required rifle grip/muzzle sockets, render/visibility policy, direct weapon-attachment selection, absence of an active SciFi runtime presentation, and any retained replicated team-state notify. These are pure/in-process rules tests; they do not prove RPC replication, a real network break and reconnect, two-client possession transfer/rollback, phase timing, real NavMesh traversal, guard animation/presentation, exact hand/finger contact in a rendered pose, muzzle alignment, or rendered UI behavior.

## 9. Verified And Not Yet Verified

RAR combat-feedback texture update on 2026-10-10:

- Development Editor and Development Game compile and link. The complete `Tcamp.Tactical` automation group passes `19/19` in `Saved/Logs/RARCombatFeedbackFullFinal2_20261010.log`, including the texture-load and feedback-style test.
- `Saved/Screenshots/WindowsEditor/ScreenShot00002.png` is a rendered PIE/Slate capture of the new centered sight reticle in `PreRound`; the capture command exits normally in `Saved/Logs/RARCombatFeedbackUIVisualFinal20261010.log`. The RAR hit-marker texture itself was inspected, but live two-player shooter-only hit/no-hit timing remains manual acceptance.
- Hitscan tracers are now implemented as purely cosmetic local moving streaks. The existing unreliable `MulticastHitscanResult` sends a bounded server-authored endpoint and per-weapon shot ID with its ordered impacts; it still does not change damage or hit-marker confirmation. Owner/ordinary observer/first-person spectator choose their respective visible gun muzzle, while hidden or stale weapons skip the effect. `ATacticalBulletTracer` uses the new independent additive/unlit `/Game/Tactical/FX/M_TacticalTracer` material, moves a short segment for `0.045–0.12s`, then destroys itself; it neither collides nor replicates. Miss visuals cap at `20000` cm while gameplay range stays `100000` cm. `tc.Tracer.Enabled 0|1` provides a local performance toggle. The planned Niagara Beam was deferred because a reliable asset-authoring path was not available; the migrated RAR glow instance has a missing parent and is not used. Development Editor/Game builds and `21/21` tactical automation tests pass in `Saved/Logs/TracerAutomationFinal20261010.log`; Windows map cooking completes with zero errors in `Saved/Logs/TracerCook20261010.log` and includes the new material. Rendered owner/remote/2v1 spectator acceptance, six-player frame comparison, and packaged network confirmation remain open.
- Enemy body-hit feedback is triggered only after server point-damage resolution on `ATacticalCharacter` or `AShooterNPC`. True HP loss produces a short red-orange local impact; armor-only absorption produces a blue-white impact; mixed HP/armor now produces a red center plus a wider blue-white halo, alongside the separate full-body shield flash. Headshots are slightly brighter/larger. One unreliable victim multicast carries effect flags and hit point/normal/bone; nearby viewers spawn a `0.16–0.20s` local, bone-following, non-replicated, non-colliding, shadowless actor using `/Game/Tactical/FX/M_TacticalBodyHit`. The victim's first-person view keeps its directional HUD cue. Warmup, zero damage, self hit, and same-team point contact show no *enemy* impact; damage/friendly-fire rules are unchanged. `tc.BodyHitFx.Enabled 0|1` isolates local cost.
- The old full-body armor flash now rejects empty Blueprint `ShieldSphere` candidates and uses its native Sphere + `M_Shield` fallback, attached/registered at runtime and scaled as an oval. This fixes the case where the Blueprint's named component had neither a mesh nor a material, causing the multicast to reveal nothing. Two-window rendered PIE, six-player frame pacing, and packaged network appearance still require acceptance.
- The shield/mixed-hit follow-up builds in Development Editor and Game. All `24/24` tactical automation tests pass in `Saved/Logs/BodyHitShieldFixAutomation20261010.log`; the full Windows game-map cook finishes with `0` errors in `Saved/Logs/BodyHitShieldFixCook20261010.log`. These mechanical results do not yet verify on-screen size, color, or two-player replication timing.
- The hit-marker replacement follow-up compiles in Development Editor and Development Game, and the `Tcamp.Tactical` group passes `19/19` in `Saved/Logs/HitmarkerReplacementFullFinal20261010.log`. `CombatFeedbackStyle` now tests the reticle/marker replacement and spectator-hidden predicates. Live two-player hit timing remains a visual acceptance check.

ADS, ballistic-origin, and fire-response native implementation on 2026-10-12:

- The RAR first-person native graph now drives aim-in/out, aimed idle/walk, aimed fire, and aimed reload from the imported matching-skeleton assets. Current pistol and rifle tiers have ordinary ADS fallback; smooth world FOV and first-person arms/gun visual alignment operate without moving the camera. The ordinary reticle hides only once sight alignment converges. The owning character now drives the aim blend instead of relying on whether weapon Tick was enabled during BeginPlay, and client restart re-enables the retained pawn's viewmodel Tick. The RAR aimed reload length is included in both authoritative and local presentation fire locks. The pistol remains Manny with its current Control Rig; a new finger/wrist pose and precise rendered sight quality have not been accepted.
- Player hitscan and projectiles now use one stable, server-computed logical muzzle from validated view location/rotation plus a configurable offset, rather than the listen host's FP socket versus a remote pawn's TP socket. NPC holder overrides remain. Eye-to-muzzle and muzzle-to-spawn geometry checks stop near-cover bypass; projectiles additionally test their actual collision-sphere volume and do not spawn if blocked. The blocked shot still spends ammo; no synthetic projectile impact FX is generated in this first pass. View-specific visual muzzles and server-result cosmetic tracers are unchanged.
- The owning autonomous client can play its FP arms/gun fire montage immediately. A weapon/burst/shot identity suppresses both late authoritative duplicates and local timers that would otherwise follow an early server echo. Start/stop/reload RPCs validate the expected weapon, rejected starts cancel active predicted bursts, and switch/reload/freeze/death/teardown cancel local firing work. Switch/select input is edge-triggered, switch requests cannot repeat while confirmation is pending, and an empty reserve outside warmup no longer creates a false local reload lock. The server still controls ammo, cadence, recoil to camera control rotation, hit markers, and damage. Standalone predicted sound, muzzle flash, and visual-only gun kick are **not** implemented/verified; current immediate presentation is the montage. A montage already started for a subsequently rejected shot cannot be undone perfectly.
- The final native version builds for Development Editor and Development Game. After review fixes for repeated switch requests, false local reload lock at zero reserve, and the owner/reconnect ADS Tick dependency, the complete `Tcamp.Tactical` automation group passed `25/25` in `Saved/Logs/ADSBallisticPredictionAutomationPostReview20261012.log`. The tactical map started through BeginPlay without a fatal/Blueprint runtime error in the earlier `Saved/Logs/ADSBallisticPredictionMapSmoke20261012.log`. Final Windows map cook finished with `0` errors and `2` warnings in `Saved/Logs/ADSBallisticPredictionCookPostReview20261012.log`, and the RAR aimed-animation assets are present in the cooked output; one warning is the pre-existing Paragon retarget `Rig` load, and the other is intentional Steam disabling for this test cook. These are mechanical checks only. Two-window PIE is still needed for actual hand/sight angle, host/client hit parity and near-wall cases, warmup ready targets, rapid switching/reload, latency/packet-loss deduplication, and six-player frame pacing.

ADS visual first pass on 2026-10-12: the owner-only alignment uses a virtual rear-to-front line for each weapon, not a single accessory/bone point. The pistol uses `Barrel` → `Muzzle`, with a 4.5 cm shared mesh-up offset; the RAR rifle uses `SOCKET_Scope` → `Muzzle` with unequal offsets (4.9/12.2 cm) because the rear reference bone is already about 7.3 cm higher than the muzzle. World ADS FOV zooms without enlarging the first-person weapon projection. The RAR Aim In clip blends to its steady pose over its full length rather than only the final 0.06 seconds. Gameplay muzzle, authoritative ballistics, damage, and third-person presentation are unchanged. Development Editor/Game builds and `25/25` tactical tests passed in `Saved/Logs/ADSVisualAlignmentAutomation20261012.log`; `Saved/Logs/ADSVisualAlignmentMapSmoke20261012.log` starts the gameplay map without a fatal error. This was a mechanical first pass, superseded by the overlap follow-up below. The RAR rear iron-sight accessory is available in content but is not installed by the current default-parts code.

ADS transition/overlap follow-up on 2026-10-12: releasing aim keeps the current weapon and decaying `AimBlendAlpha` in the owner visual-root solver; the same alpha now drives world FOV, pose, and root on both aim-in and aim-out. The RAR Aim Out clip's first frame did not match steady ADS, so the native graph returns through its continuous base-pose blend and briefly fades any interrupted Aim In clip. Rifle aim-out is slower. During aim-in/out, the RAR rifle skips its endpoint-authored **arms** fire montages while retaining the gun-body fire montage; `Fire_Aimed` is selected only after the alpha and actual sight alignment have converged. The rifle caches a non-recoil sight line in gun-component space, then follows the animated hand socket even while the gun montage plays; arm reload/fire montages still cannot feed recoil into the entire viewmodel root. The shipped pistol AnimBP copies the body pose and continuously aims `hand_r` through a Control Rig. Its C++ ADS now translates the whole arms root but leaves rotation to that Rig, avoiding two competing aim rotations. The Rig's `AimBoneMath` primary/secondary axes were calibrated to the registered pistol `Barrel`→`Muzzle` and gun-up directions in `hand_r` space; the previous asset was backed up at `Saved/CodexADSBackup20261012/Ctrl_HandAdjusment_Pistol.before_ads_axis.uasset`. In a fresh rendered standalone run, the settled pistol barrel/camera angle fell from about 11.4° to about 0.7° (`Saved/Logs/ProbeADSRuntime_20261012_calibrated.log`), while a rifle full-auto + aim-in run kept the sight near center after convergence (`Saved/Logs/ProbeRifleADSFireRuntime_20261012_fixed.log`). Development Editor/Game builds, `25/25` tactical tests (`Saved/Logs/ADSOverlapFinalAutomation20261012.log`), and a NullRHI map startup (`Saved/Logs/ADSOverlapFinalMapSmoke20261012.log`) passed. These are single-player/rendered and mechanical checks, not a two-window PIE acceptance of rapid reversal, networked firing, or every pistol animation. The imported RAR Aim Out asset remains loaded but deliberately bypassed until its starting pose matches steady ADS.

ADS fire-first/pitch follow-up on 2026-10-12: entering ADS while a RAR hip-fire arms montage is already playing now fades out only that arms clip in 0.05 s; the gun-body firing montage and actual shot continue. In the fire-then-aim runtime probe, maximum leftward sight drift fell from about 6.44 cm to 0.98 cm and settled to 0.02 cm (`Saved/Logs/ProbeRifleFireThenADSFinal_20261012.log`). The pistol keeps its level view pivot and calibrated Control Rig. The Rig's spring output no longer delays right-hand aim, and `ABP_FP_Pistol` feeds its visual Rig a distant camera-forward point even when its trace hits nearby geometry; authoritative firing/ballistics are unchanged. The two animation assets were backed up under `Saved/CodexADSBackup20261012` before editing. The initial yaw-only ADS root target with a `-17.5*sin(pitch)` compensation was validated only by aiming at level pitch before turning (`Saved/Logs/ProbePistolADSPitchFarTarget_20261012.log`); it was later found to fail when the player turned before aiming and is superseded below.

Pistol ADS input-order correction on 2026-10-12: the owner C++ solver now samples the pistol's non-montage rear/front line in arms-local space every frame, including after ADS begins, and places the virtual rear sight at the current camera-forward target. The old yaw-only target and fixed pitch compensation were removed. In fresh rendered standalone runs, both "pitch then ADS" and "ADS then pitch" converged within about 0.02 cm of the same sight point at ±35°, ±60°, and the reachable up/down extremes; the barrel-to-camera angle was about 0.11° (`Saved/Logs/PostPatchPistol_*.log`). The previous pitch-before-ADS +35° error was about 35 cm and could hide the whole gun. Continuous pitch without firing had about 1.4–1.8 cm transient sight movement, settling rapidly. Fire montages still freeze the root to avoid chasing recoil, so rapidly pitching while firing can briefly move the sight by roughly 11–14 cm before it recovers; recoil/aim-pose separation is a distinct follow-up. Development Editor/Game builds and all `25/25` tactical automation tests passed (`Saved/Logs/PistolADSOrderFixAutomation_20261012.log`). Two-window PIE and subjective hand/skin quality remain to verify.

Combat-feedback mechanical verification was refreshed on 2026-09-21:

- UE 5.7 Development Editor, Development Game, and Shipping Game compile and link with the native hollow crosshair, controller-owned confirmed-hit and victim-direction RPCs, true hitscan/projectile travel directions, authored-reticle suppression, eight complete-edge directional masks, the omnidirectional fallback, and the shared smooth time envelope.
- The complete `Tcamp.Tactical` group passes `17/17`, with `0` warnings, `0` failures, and `0` skipped tests at `Saved/Automation/DamageFeedbackArcContrastFull/index.json`. The focused `Tcamp.Tactical.UI.CombatFeedbackStyle` report is `Saved/Automation/DamageFeedbackArcContrastFinal/index.json`; it verifies the stronger edge-to-center contrast, semi-elliptical inward profile, full-width front coverage, diagonal along-edge intensity bias, equal four-side gradients at multiple depths, transparent generated center texels, all eight camera-relative directions, opposite-edge rejection, fade timing, repeated-hit continuity, and RPC ownership/flags.
- A fresh Tactical GameMode NullRHI smoke exits normally with no fatal error, assertion, Blueprint runtime error, or `Accessed None` in the checked path. Its refreshed log is `Saved/Logs/DamageFeedbackArcContrastSmoke.log`. The existing Murdock retarget-package, missing FootFX animation-modifier, and no-NavMesh warnings are unrelated pre-existing asset/map warnings.
- A 1280x720 rendered UI capture at `Saved/Screenshots/WindowsEditor/ScreenShot00000.png` confirms that the authored circle is suppressed and the outlined four-arm native reticle is centered and visible. It does not exercise the transient hit/damage animations.
- This is mechanical evidence, not rendered acceptance. A two-window PIE pass must still confirm crosshair scale at the chosen viewport/DPI, ready-target aiming during `PreRound`, shooter-only hit markers for both hitscan and projectile weapons, no marker on walls or zero applied damage, the perceived half-arc shape and stronger outer-edge contrast in all eight directions, simultaneous crossfire readability at the `0.70` peak opacity, smooth fade-in/fade-out, repeated-hit refresh without flashing, and suppression after lethal damage, spectating, round end, and reconnect restoration.

Current Manny direct-presentation state as of the 2026-09-21 rollback:

- Both player render paths again use `SKM_Manny_Simple`, and each live weapon mesh attaches to its own Manny `HandGrip_R`. The SciFi presentation components and runtime retarget/hand-correction path are inactive but retained in source/assets.
- The rollback removes model-level red/blue tinting for now; HUD, roster, spectator team text, replicated team assignment, and gameplay team rules remain active.
- UE 5.7 Development Editor and Development Game compile and link successfully. Shipping was not rebuilt for this rollback.
- The focused `Tcamp.Tactical.PlayerPresentation.Configuration` test passes at `Saved/Automation/MannyRestoreFocused/index.json`. The final complete `Tcamp.Tactical` run passes `17/17`, with `0` warnings, `0` failures, and `0` skipped tests at `Saved/Automation/MannyRestoreFinal/index.json`.
- The NullRHI smoke at `Saved/Logs/MannyRestoreSmoke.log` and rendered smoke at `Saved/Logs/MannyRestoreRenderedSmoke.log` contain no fatal error, assertion, Blueprint runtime error, or `Accessed None`. Both confirm `Parent="First Person Mesh" Socket="HandGrip_R" Mesh=Manny`, with no SciFi `PlayerPresentation` or first-person `GripIK` initialization.
- `Saved/Screenshots/WindowsEditor/HighresScreenshot00012.png` shows the restored Manny first-person hands and pistol. This is one rendered snapshot, not acceptance of every weapon and animation state.
- Two-window rendered PIE, real reconnect restoration, and full-3v3 frame-time measurements remain pending.

Historical SciFi first- and third-person presentation verification from earlier on 2026-09-21 (superseded by the Manny rollback above):

- UE 5.7 Development Editor, Development Game, and Shipping Game all compile and link the post-retarget `LocalToComponentSpace -> FABRIK -> ComponentToLocalSpace` left-hand constraint.
- The final focused `Tcamp.Tactical.PlayerPresentation.Configuration` test passes at `Saved/Automation/FirstPersonLeftHandIKFinal/index.json`. In addition to the existing presentation and attachment policies, it verifies the SciFi `upperarm_l`/`hand_l` hierarchy and that the native helper preserves the source left hand's complete transform relative to `HandGrip_R` while stripping scale. The complete `Tcamp.Tactical` group passes `17/17`, with `0` warnings, `0` failures, and `0` tests skipped at `Saved/Automation/FirstPersonLeftHandIKFull/index.json`.
- A rendered offscreen Tactical GameMode smoke reaches the live animation node and records `Event=FirstPersonLeftHandGripIKReady ... Root=upperarm_l Tip=hand_l Effector=HandGrip_R` in `Saved/Logs/FirstPersonLeftHandIKRenderedSmoke.log`, in addition to successful first-person presentation initialization and weapon attachment. This proves activation and non-zero source-derived effector data in a rendered path; it does not prove visually correct contact.
- This evidence describes only the retired SciFi runtime path and must not be used to accept the current Manny presentation. The retained reports are still useful when comparing a future replacement character, but current acceptance must confirm Manny pistol/rifle grip, muzzle direction, locomotion, jumping, ADS, firing, reload lockout, switching, spectating, reconnect restoration, headshot alignment, and full-3v3 performance directly.

Treasure guard AI rewrite mechanical verification was completed on 2026-09-18:

- UE 5.7 Development Editor, Development Game, and Shipping Game all compile and link after the dedicated guard brain, collision-safe distinct guard posts, authoritative reload-duration bridge, and bounded candidate-path search changes.
- The complete `Tcamp.Tactical` automation group passes `15/15`, with `0` warnings, `0` failures, and `0` tests skipped. The final post-review report is `Saved/Automation/TreasureGuardAIRewriteFinal3/index.json`, with the log at `Saved/Logs/TreasureGuardAIRewriteFinal3Tests.log`.
- This is mechanical evidence only. Close-range retreat, stop-and-fire cadence, obstacle repositioning, two-guard spacing, visible reload timing, accuracy, and frame pacing still require a fresh multiplayer PIE run in the gameplay map.

Treasure rewrite mechanical verification was refreshed on 2026-09-10:

- UE 5.7 Development Editor, Development Game, and Shipping Game all compile and link with the enabled fixed-layout treasure timeline, strict two-guard activation, server-authoritative claim/firing lock, stable-roster reward path, replicated snapshot, native status/world-marker UI, and fallback chest model. The project Blueprint compile pass excluding the known unrelated `/Game/Variant_Horror` assets completes with `0` compiler errors and `0` compiler warnings; its log is `Saved/Logs/ChestVisualFallbackBlueprintCompile.log`.
- The rebuilt Editor module passes the complete `Tcamp.Tactical` automation group `12/12`, with `0` test warnings and `0` failures after the native fallback visual and replicated open-state presentation were added. The final report is `Saved/Automation/ChestVisualFallbackRelease/index.json`, with the test log at `Saved/Logs/ChestVisualFallbackReleaseTests.log`.
- An editor asset/level inspection confirms that the live map actor inherits all five fallback mesh components and that each resolves to `/Game/LevelPrototyping/Meshes/SM_ChamferCube` while the authored `Chest Mesh` remains empty. The user has manually confirmed spawn, guards, guard elimination, claiming, and the fallback appearance. Broader contention/reward/balance cases remain part of the full multiplayer matrix.

Steam Listen Server implementation verification was added on 2026-09-10:

- UE 5.7 Development Editor, Development Game, and Shipping Game compile and link with `OnlineSubsystemSteam`, SteamSockets, the native multiplayer menu, ordered `INITIALIZING`/`LOBBY`/`IN_PROGRESS`/`CLOSING` lifecycle, generation-safe Session owner, Session-resolved reconnect, Steam admission gate, and host-loss flow.
- The rebuilt Editor module passes the complete `Tcamp.Tactical` automation group `12/12` with exit code `0`.
- The final uncooked `-nosteam` Listen Server smoke (`Saved/Logs/SteamPhaseFinalListenSmoke.log`) proves the editor fallback path: SteamSockets reports unavailable, GameNetDriver selects IpNetDriver, listens on port `7777`, loads `BP_TacticalGameMode_C`, admits the local participant, and reaches `PreRound` without a fatal error, assertion, or Blueprint runtime error.
- The final uncooked frontend smoke (`Saved/Logs/SteamPhaseFinalFrontendSmoke2.log`) loads `/Engine/Maps/Entry` using `TacticalMenuGameMode` without the old null-HUD spawn warning. Final Steam proof still requires packaged builds, two physical PCs, and two Steam accounts.

Reconnect HUD, feedback separation, and controlled-offline-test pass verified on 2026-09-08:

- UE 5.7 Development Editor, Development Game, and Shipping Game all compile and link after the authority-only health/ammunition initialization, silent health synchronization, post-widget HUD rebind, late-weapon ammo snapshot, and player-controller-scoped development disconnect helper. The development helper bodies are disabled in Shipping.
- The rebuilt Editor module passes the complete `Tcamp.Tactical` automation group `11/11`, with `0` test warnings and `0` failures. The machine-readable report is `Saved/Automation/ReconnectFeedbackFinal2/index.json`, with the log at `Saved/Logs/ReconnectFeedbackFinal2Tests.log`.
- A fresh uncooked NullRHI Listen Server smoke reaches authoritative `PreRound`, routes `TacticalPauseAutoReconnect` through the local tactical controller, records `Enabled=0` on the correct GameInstance, creates the native gameplay/roster UI, and tears down cleanly. `Saved/Logs/ReconnectFeedbackFinal2Smoke.log` contains no fatal error, assertion, ensure, Blueprint runtime error, `Accessed None`, pending-kill report, tactical error, or network warning in the checked path. The atomic disconnect command additionally rejects non-client worlds so it cannot disconnect every peer when mistakenly entered on the listen host. This proves command routing/startup only; the two-player retained-value, feedback, and delayed-manual-retry behavior still requires the PIE sequence below.

Current disconnect-body/match-result baseline verified on 2026-09-07:

- UE 5.7 Development Editor, Development Game, and Shipping Game all compile and link after the retained-body, reason-aware match-end, GUEST preservation, generation/ownership-safe takeover, repeated-allocation proxyless rollback, and idempotent settlement changes. A Shipping build does not prove Steam authentication or connectivity.
- The rebuilt Editor module passes the complete `Tcamp.Tactical` automation group `11/11`, with `0` test warnings and `0` failures. The current disconnect rules cover status-expiry retention, post-expiry same-pawn reclaim, forfeit versus abort, recovery cancellation, and score-result priority. The final machine-readable report is `Saved/Automation/DisconnectBodyRetentionFinal7/index.json`, with the log at `Saved/Logs/DisconnectBodyRetentionFinal7Tests.log`.
- A fresh uncooked NullRHI Listen Server smoke reaches authoritative `PreRound`, creates the native roster UI, binds Tab, and tears down with `WorldTearingDown=1`. `Saved/Logs/DisconnectBodyRetentionFinal7Smoke.log` contains no fatal error, assertion, ensure, Blueprint runtime error, `Accessed None`, pending-kill report, tactical error, or `LogNet`/`LogNetPlayerMovement` warning in the checked path. This is startup/teardown evidence, not a real disconnect test.
- A fresh uncooked NullRHI Listen Server smoke starts and tears down cleanly. `Saved/Logs/ReconnectRosterStatusSmoke2.log` contains no fatal error, assertion, ensure, Blueprint runtime error, `Accessed None`, pending-kill report, tactical warning/error, or network warning in the checked path.
- A real-time uncooked dedicated-server plus client smoke on the IP driver admits a non-Shipping development guest, creates the lobby participant, sends the owner-only admission material, receives its ACK, and starts the bounded PreRound grace after the client is interrupted. `Saved/Logs/ReconnectRootFinal6ServerLive.log` and `Saved/Logs/ReconnectRootFinal6Client.log` contain no fatal error, assertion, Blueprint runtime error, `Accessed None`, tactical warning/error, or `LogNet`/`LogNetPlayerMovement` warning in the checked path. This is diagnostic transport evidence only; a dedicated server is not the selected product deployment model and this run proves nothing about Steam.

Previously verified baseline:

- UE 5.7 UHT, the complete Development Game target, and the normal Development Editor target build and link after the spectator-HUD and weapon-lifetime changes. After the editor process closed, the temporary hot-reload artifacts were removed and the normal `UnrealEditor-Tcamp_FPS.dll` was rebuilt successfully.
- The pre-reconnect `Tcamp.Tactical` automation baseline passed with zero test warnings and zero failures in unattended `UnrealEditor-Cmd` with `NullRHI` on 2026-09-05. Its historical report is in `Saved/Automation/SpectatorHudFinal`; it must not be treated as verification of the newly added reconnect code or its expanded tests.
- A fresh uncooked Listen Server smoke run using the final spectator-HUD build (`Saved/Logs/SpectatorHudFinalSmoke.log`) starts the authoritative PreRound flow and exits through world teardown with zero Blueprint runtime errors, `Accessed None`, pending-kill reports, tactical ensures, or fatal errors.
- A fresh six-second uncooked Listen Server smoke run using the spectator-pitch build (`Saved/Logs/SpectatorPitchFixEditorSmoke.log`) exits with `WorldTearingDown=1` and no tactical ensure, fatal error, reconnect snapshot, elimination scheduling, or fabricated round result.
- A six-second uncooked Editor game-mode Listen Server smoke run confirms the host now joins as `Team=NONE`, both rosters remain empty, the native selection UI is visible, and the ready gate reports that the host has not selected a team.
- Team selection and readiness can only be granted by the authoritative red/blue wall targets; a fresh player is never automatically assigned by connection order.
- Shooting a target selects that team and marks the player ready in one server operation. Shooting the other target before match start switches teams while preserving readiness; a full destination team rejects the switch without removing the player from the current team.
- The two-player PIE self-selection path was accepted on 2026-09-04: players can begin unassigned, choose teams themselves, enter the match, and transition through phases normally.
- Two-player Listen Server PIE connects both players.
- Both players can continuously move in `PreRound`; smooth mouse look was accepted in the 2026-09-04 PIE retest.
- Listen-server and client firing are server-authoritative.
- Both players receive a default weapon with the existing asset configuration.
- `PreRound` shots do not damage players.
- A real two-player log confirms match start, a configured 10-second Buy phase, an authorized light-armor purchase, transition to Combat, armor absorption, durability reduction, and the owner armor HUD.
- The top-left armor/health/team-badge rectangles have a passing automated non-overlap regression test.
- A 2026-09-05 three-player PIE log proves the reported spectator had exactly one living same-team target after death and contains no phase, roster, assertion, or fatal error. UE 5.7 source inspection isolated the vertical-look failure to remote camera calculation rather than teammate selection.
- The spectator camera now keeps the target camera's existing location, yaw, roll, and FOV while supplying pitch from authoritative `ControlRotation` on a listen server or replicated `RemoteViewPitch16` on a client. Both Development Game and Development Editor module builds link successfully; real multi-window camera motion still requires PIE visual acceptance.
- The C++ spectator presentation now has the complete server snapshot, native HUD, and reversible first-person weapon path. The information-gate rules and build/startup behavior are automated; visible weapon placement and live target-value updates still require the multi-window PIE acceptance pass.
- The previous approximately 5 FPS failure was UE editor background throttling, not gameplay load.
- Disabling editor background throttling and restarting restored approximately 100 FPS in the reported PIE test.

Implemented in the current source and mechanically verified; real gameplay reconnect scenarios still need PIE/network acceptance:

- Stable participant records and reserved 3v3 team slots independent of connection-lifetime controllers.
- A 30-second public reconnect-status window plus current-round living-pawn retention, server-only disconnect proxy, preserved `PlayerState`, and same-pawn repossession during the same Buy/Combat round even after the countdown expires.
- Proxy allocation failure is fail-safe rather than elimination: the authoritative unpossessed pawn and its Pawn/PlayerState association stay in the roster, server alive counting includes that retained pawn, and only a later real damage/death event can eliminate it and credit an attacker. A replacement connection that fails before takeover, followed by another proxy-allocation failure, rolls the same ParticipantId generation back to this proxyless state; a living body is not cleared, scored, or removed, while a genuinely dead body still refreshes Combat victory after the safe state commit. PreRound proxyless rollback retains a serviceable public deadline so an abandoned lobby slot still expires.
- Real offline pawn death or the round boundary transitions to restricted spectating; public-status expiry alone does not. Buy-only legal late round entry, entered/settled-round bookkeeping, and compatibility snapshot fallback remain in place.
- Match ID, participant credential, protocol-version, duplicate-connection, claim-shape, and recovery-source admission checks. Rotation uses a previous-token overlap plus owner ACK; the credential is never publicly replicated and project-authored diagnostics omit its value. A recovery-time `NM_Standalone` fallback admission is rejected before it can overwrite or acknowledge the cached remote-server credential.
- A native client reconnect overlay with owner-scoped input locking, `0/1/2/4/8` automatic attempts, a separate stale-connection retry budget, owner-admission plus pawn/spectator confirmation, manual retry, and deliberate leave.
- Replicated privacy-safe connection status and an absolute server status-window deadline without exposing identity or credential material.
- A Blueprint-independent hold-Tab scoreboard with fixed red/blue 3-player columns, score/round information, local-player highlighting, alive/waiting/eliminated status, persistent reconnect/disconnect/left rows, and server-clock-derived countdown text. Its six row widgets are built once and refreshed at 0.2-second presentation intervals only while visible. Normal controller input and the focused Buy-menu preview/key-up path both drive the same visibility function, with searchable creation/binding/visibility logs.
- A reason-aware whole-team disconnect terminal rule at the `RoundEnd` continuation boundary. A normal score result already locked by the completed round wins first. Otherwise, one absent qualified team forfeits without an extra round point/economy settlement; two absent qualified teams end as `AllTeamsDisconnectedAbort`, winner `255`, no result, not a draw. This prevents the previous `10s Buy -> 0.15s Combat -> 6s RoundEnd` empty-roster loop without allowing a RoundEnd disconnect to reverse an earned win.
- Same-pawn takeover is rollback-safe, generation-bound, and ownership-checked: the pending transaction snapshots identity plus `ParticipantId`, phase/round decision inputs, preserved state, proxy, retained pawn, and any bootstrap pawn. Every re-entrant possession, PlayerState-association, roster-registration, actor-destruction, and Blueprint-extensible callback boundary revalidates the exact generation and expected actor graph. The old proxy/body is retired only after the new controller proves possession/spectator state, and `EnteredRound` is committed only after successful completion. A failed completion destroys the new controller so Logout restores only that exact participant generation and cannot erase a same-identity replacement. Development `GUEST` retention uses the already-resolved roster record, so removing its controller-local identity override cannot orphan the pawn during Logout.
- Round economy is applied idempotently before `RoundEnd` is published. Candidate state is copied through weak references and settlement markers are committed before money delegates can re-enter gameplay code; a living survivor who disconnects during `RoundEnd` retains equipment for the next legal round rather than being treated as a missing-body death.
- The new native match-result banner distinguishes `TeamDisconnectForfeit` from `AllTeamsDisconnectedAbort`; technical no-result endings do not call the legacy Blueprint draw hook. `MatchEndSerial` catches up late match-result binding, while replicated `RoundEndSerial` catches up and deduplicates the ordinary round-result banner for a client reconnecting during `RoundEnd`. A connected inactive controller with no pawn is labelled `WAITING`, not `DEAD`.
- Service and round-boundary cleanup batch identity together with `ParticipantId`, revalidate the generation after every earlier callback, copy weak actor state, and commit registry changes before user-extensible armor, possession, or actor-destruction callbacks. Re-entry therefore cannot invalidate a `TMap` reference or make stale work mutate a same-identity replacement.

Not yet accepted end to end:

- Combat elimination through the 0.15-second resolution window.
- Six-second RoundEnd and next-round respawn.
- Win/loss/draw economy and survivor/dead equipment persistence across a real network round.
- Timeout draw, simultaneous-elimination draw, five-win match end, and ninth-round draw.
- Two-player assigned-team PIE shutdown after the teardown guard change.
- 2v2, 3v3, seventh-player overflow, real network interruption, retained-pawn same-process reconnect before/after status expiry, offline pawn death, next-round re-entry, deliberate leave, duplicate connection, bad Match ID/token/protocol, and fresh join-in-progress scenarios.
- Packaged Steam Listen Server validation with two Steam accounts and two physical PCs on a LAN and across different networks, including session failure/cleanup and terminal host-loss behavior. The current Listen Server PIE harness does not prove this path.
- Real multi-window visual acceptance of the hold-Tab roster: host/client rendering, reconnect countdown progression, early reconnect clearing, expiry to disconnected, and a mid-match deliberate leave retaining its `Left` row.
- Real 1v1 status-expiry/Combat-timeout acceptance of the whole-team-forfeit path, including an unchanged living body after expiry, exactly one current-round draw settlement, one `DisconnectMatchTermination Reason=TeamDisconnectForfeit`, unchanged round score, and no automatically awarded later rounds. Also verify that a normal final-score result outranks any disconnect during RoundEnd.
- Teammate-only spectator presentation in real PIE: host/client weapon and animation visibility, live health/armor/ammo changes, target switching, HUD cleanup on respawn, vertical/horizontal look, and the explicit no-living-teammate fallback. The fallback intentionally keeps movement/look locked and shows a status panel; an authored safe camera is still optional future polish.
- Real multi-window visual/balance acceptance of the implemented hunt marker/HUD behavior, including both timing tiers, team privacy, spectator delivery, death cleanup, off-screen clamping, and no cross-round residue.
- Real two-window visual acceptance of the new enemy body-hit effect for HP-only, armor-only, mixed, headshot, NPC, warmup, death/ragdoll, fast movement, 2v1 spectator, and reconnect, followed by six-player cosmetic on/off profiling.
- Expanded treasure coverage in real multi-window PIE: retest the rewritten guard retreat/position/fire/reload/return behaviour and frame pacing, then cover contention, every firing-lock/cancellation route, nonlethal-hit behavior, stable-roster exactly-once reward including dead/offline members, round reset, marker readability, and balance sampling. The core spawn/guard-elimination/claim/model loop remains accepted.

Deliberately out of scope for the selected first Steam phase:

- Automatic match continuity after terminating and restarting a remote-client process. A later design may allow it to join a still-surviving Steam Session as a new connection, but the current memory-only admission cache does not promise this.
- Listen-host process restart, host migration, or any match recovery after the listen host/authoritative world is lost. Host loss ends the match.
- Rented or always-on dedicated-server deployment and backend-persisted match recovery.
- Seamless travel is not enabled; current teardown/restart validation assumes the configured hard-travel world replacement. If seamless travel is introduced later, disconnected/abandoned `PlayerState` tombstones and proxy-owned state must be explicitly excluded or rebuilt against the destination world's new roster registry to prevent ghost rows.

## 10. Development PIE Regression Pass

Use these PIE settings:

```text
Number of Players = 2
Net Mode = Play As Listen Server
Run Under One Process = true
Run Dedicated Server = false
```

The warmup, selection, Buy, purchase, Combat entry, armor absorption, and HUD value portions have already produced baseline runtime evidence. The reconnect implementation now changes the disconnect expectations, so run this sequence again after a clean rebuild:

1. Start a normal 1v1, eliminate one team, and wait without stopping PIE.
2. Confirm `EliminationResolutionScheduled -> EliminationResolutionResolved -> RoundEnded`, followed by exactly 6 seconds of `RoundEnd` and round 2 `Buy`.
3. Compare winner/loser money and survivor/dead equipment after both players respawn.
4. During `Combat`, first reduce the non-host client's health, magazine, and reserve ammunition to recognizable non-full values. Focus that PIE window, open its in-game console, and run the single command `TacticalTestDisconnect`. It atomically pauses automatic recovery only for that local player and disconnects its `GameNetDriver`. Confirm the log sequence contains `ReconnectAutomaticRetryTestSetting Enabled=0 ... NetMode=Client`, `ReconnectTestDisconnectRequested`, `ClientNetworkFailure ... AutoRetryEnabled=0`, and `ReconnectAutomaticRetryPausedForTesting`, with no `ReconnectAttempt` before a manual action. On a remaining connected player, hold Tab and confirm `DisconnectGraceStarted`, one retained roster row, a monotonic `Reconnecting 30s ... 1s` display, a reserved team slot, and an input-disabled but still visible/damageable pawn with unchanged health, armor, weapon, magazine, and reserve ammunition. Verify Tab both during Combat and after a Buy-menu button has focus; the logs should contain `RosterInputBound` and paired `RosterVisibilityChanged` events. Its initial velocity must stop, while normal physics or external forces may still move it.
5. Click the disconnected client's native `RETRY` action once before 30 seconds and, in a separate interruption, once after the countdown has expired but while the same round and pawn are still alive. Manual retry must work while automatic retry remains paused. Confirm the fallback world logs `ReconnectAdmissionRejected Reason=NonRemoteFallbackWorld`, then the real server receives the original Match ID/token/protocol claim, rotates the credential, transfers the preserved `PlayerState`, repossesses exactly the same pawn without spawning or duplicating equipment, and changes the retained Tab row back to a connected life state. Before taking damage or firing, confirm the reconnected HUD already shows the pre-disconnect health, magazine, and reserve values and does not play the red damage overlay. A failed completion must log rollback and leave the original proxy/body graph intact rather than killing it. There must be no `TACTICAL_RECONNECT_ADMISSION_REJECTED` for a valid recovery. Run `TacticalResumeAutoReconnect` after the controlled tests to restore normal automatic behavior for that client.
6. Run two separate offline-body outcomes. First, kill the retained pawn and confirm the normal damage/death/equipment/drop path, elimination result, and settlement occur exactly once; a later reconnect enters restricted teammate spectating until the legal next-round boundary. Second, do not shoot the body: wait past 30 seconds and confirm `DisconnectStatusWindowExpired BodyRetained=1`, the Tab row changes to `Disconnected`, health/armor/weapon/ammunition and round score remain unchanged, and the body stays damageable until the round ends. In 1v1, let Combat reach zero: the round must be `TimeExpiredDraw`, then the still-absent team must produce one `DisconnectMatchTermination Reason=TeamDisconnectForfeit` at the end of `RoundEnd` without another round point or empty round. If both qualified teams are absent, expect `AllTeamsDisconnectedAbort`/`NO_RESULT`, not a draw. Also verify a rostered player absent at round start can enter during `Buy` but never receives a replacement pawn during `Combat`. In a 2v2 variant, disconnect a living survivor during `RoundEnd` while its teammate remains connected, reconnect in the next `Buy`, and verify that survivor equipment was retained. Reconnect another client during `RoundEnd` and verify it receives exactly one round-result banner and shows `WAITING`, never a false `DEAD` state.
7. Use the native `Leave Match` action and confirm `ParticipantAbandoned`, no grace proxy, no enemy kill credit, no automatic reconnect, a return to the menu for that client, and one persistent `Left` row on the remaining player's Tab roster until the active match/world ends. In `PreRound`, deliberate leave must instead release the lobby slot and remove the row.
8. Exercise partial claims, bad Match ID/token/protocol, credential rotation, duplicate active login, and a fresh join during an active full roster. The immediately previous credential must work only while the replacement remains unacknowledged; after owner ACK it is retired, and that retired value or any older value must be rejected. No invalid claim may steal a reserved slot or receive another participant's state.
9. End PIE normally with both teams assigned. Confirm teardown logs `WorldTearingDown=1` and does not create a grace record, elimination scheduling, settlement, or round result after teardown begins.

After the 1v1 loop passes, repeat with 2v2, 3v3, a seventh connection, repeated disconnects, every phase boundary, and server/client timing races. A separate 105-second no-kill round verifies `TimeExpiredDraw`. PIE intentionally proves the Null/IpNetDriver gameplay baseline; the implemented Steam Lobby/Session path requires a separate two-account/two-PC packaged acceptance matrix.

That Steam matrix must keep two outcomes separate:

1. If an ordinary client loses transport while the same listen host and authoritative world survive, re-resolve/rejoin the active Steam Session and use the current-round retained-body recovery flow; 30 seconds remains the initial public/automatic status window, not the living pawn's same-round lifetime.
2. If the listen host or authoritative world is lost, end the match, clean up Lobby/Session state, return clients to the multiplayer menu, and never label the result as ordinary reconnect or host migration.

## 11. Performance Notes

The editor performance override is stored in:

```text
Config/DefaultEditorSettings.ini
```

`bThrottleCPUWhenNotForeground=False` prevents UE 5.7 from reducing an unfocused editor to approximately 3 FPS. This setting requires a full editor restart after first being added.

PIE rendering defaults are reduced for development in `DefaultEngine.ini` and `DefaultGameUserSettings.ini`. Two-player, same-process PIE still renders and ticks two worlds and is less representative than Standalone or a packaged build.

The earlier mouse-only camera stutter had a separate code cause: the entire local PlayerController was accidentally limited to a `0.1`-second tick. UE processes player input and control rotation inside that tick, so mouse look updated at roughly 10 Hz while the character's cached WASD input was resubmitted every frame. The PlayerController now ticks every frame; expensive discrete HUD work remains limited by its own `0.2`-second accumulator.

Mouse smoothing is disabled and `WindowsApplication.UseWorkerThreadForRawInput=1` moves high-polling-rate raw mouse messages off the game thread. The current physical display runs at 2560x1440 and 180 Hz while VSync remains disabled. If a clean horizontal tear line remains after the 10 Hz input bug is gone, compare a single Standalone client and VRR/VSync separately; do not add camera interpolation or multiply mouse deltas by frame time.

For multi-window PIE, `FInputModeGameOnly` is configured not to consume the mouse-down event used to recapture a viewport. This lets the first left-click after switching back to the listen-server window reach `IA_Shoot`.

The initial listen-server pawn is possessed before component `BeginPlay`. Its health was therefore still zero when the phase code first called `SetFrozen(false)`, causing `IsDead()` to close `WeaponInventory::bCanFire`; the later health initialization did not reopen it. Tactical characters now cache pre-`BeginPlay` freeze state and apply all movement/firing gates only after health initialization, fixing the host-only firing failure without a PIE-specific input bypass.

`UReadyLobbyUI` is a native-only widget. Building its root in `NativeConstruct` was too late because UE 5.7 had already cached an empty `SSpacer` during `RebuildWidget`. The widget tree is now created in `NativeOnInitialized`, before Slate rebuild, and the overlay is added to the local player screen at Z-order 100.

The previous PIE log also contained repeated `ABP_TP_Rifle` null references and pending-kill weapon references during spawn, reconnect, and purchase replacement. They came from separate actor/component replication channels: a weapon actor could be destroyed before the pawn's inventory update reached a client. Current-weapon lifetime tracking, an unarmed animation fallback, and the one-second retirement grace period close those invalid-reference windows without making target ammo globally visible.

## 12. Next Execution Order

1. In two-window PIE, accept pistol and RAR rifle ADS at level/vertical aim, while moving/jumping, during aim-in/out, fire, reload and switch. Check sight angle, Manny pistol wrist/left-hand contact, reticle reappearance, remote pitch, spectator and same-pawn reconnect state. Calibrate the pistol's bespoke finger/wrist pose if whole-rig alignment is not visually sufficient.
2. Compare host/client hits from the single logical muzzle at walls, thin cover, extreme pitch, NPCs, warmup ready flags, and at least one projectile. Confirm visual tracer parallax does not change server hit/damage and a blocked projectile never emerges beyond cover.
3. Under simulated RTT/loss, hold auto fire and rapidly tap semi-auto through reload, switching, death, freeze and reconnect. Confirm owner montage plays once, server rejects mismatched weapons, no stale timer survives, and hit markers/ammo remain authoritative.
4. Repeat with up to six players while recording `stat fps`, `stat unit`, and `stat anim`. The direct Manny rollback removes the extra SciFi mesh and runtime-retarget evaluation from current players, but frame pacing must still be measured rather than assumed.
5. Produce a Windows Development package using App ID `480` and run it from two different Steam accounts on two physical PCs.
6. Validate Host -> Find/Join and Host -> Invite/Accept on the LAN, then repeat across different networks without IP entry or port forwarding; verify SteamSockets/relay in both logs.
7. Validate six-player capacity plus a seventh attempt, version/product/protocol mismatch, fresh join after match start, join/invite failure cleanup, voluntary client leave, host End Match, host process exit, and host network loss.
8. Interrupt only a non-host client while the host/world survives and confirm Session-resolved reconnect restores its retained pawn or correct teammate spectating state. Host loss must remain terminal and no-result.
9. Before distribution, replace App ID `480` with the project's assigned Steam App ID and define `UE_PROJECT_STEAMSHIPPINGID=<assigned id>` in `Source/Tcamp_FPS.Target.cs` (or the equivalent release build configuration), then rerun the packaged matrix against the real Steamworks application configuration. Shipping compilation by itself is not runtime Steam proof.
