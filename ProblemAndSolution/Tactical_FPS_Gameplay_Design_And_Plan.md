# Tactical FPS Gameplay Design And Execution Plan

Last updated: 2026-10-10

> This document defines the target product design and execution order. The
> currently implemented runtime is documented separately in
> `Tactical_FPS_Current_Setup.md`. Stable participant/team-slot records, the
> retained-body reconnect lifecycle, native recovery UI, and Match ID/token/
> protocol admission are implemented. The selected packaged-PC connection model
> is Steam Lobby + a player-hosted authoritative Listen Server using Steam
> networking/relay. Its C++ Host/Find/Join/Invite/Leave/Destroy flow, native menu,
> Session-resolved travel/reconnect, Steam identity admission, host-loss handling,
> and UE 5.7 SteamSockets configuration are now implemented; final acceptance
> still requires two Steam accounts on two packaged PCs. Editor/PIE deliberately
> retains OSS Null + IpNetDriver as a diagnostic path. The fixed-layout treasure
> loop and its native fallback model have passed the requested PIE acceptance.
> The optional three local-layout presets remain unimplemented.

## 1. Design Positioning

This project is a 3v3 multiplayer tactical round-based FPS built on top of the UE5 first-person template.

The intended core loop is:

1. Buy equipment during a short preparation phase.
2. Hunt and fight the enemy team during the combat phase.
3. Win the round only by eliminating the entire enemy team.
4. Receive economy rewards and carry eligible equipment into the next round.
5. Win the match by reaching the target round score or leading when the maximum round count is reached.

AI-guarded treasure is a secondary resource opportunity. It can create conflict and economy decisions, but it never determines the round winner.

## 2. Locked Design Decisions

### 2.1 Team Size

- Primary mode: 3v3.
- Maximum active players: 3 per team, 6 total.
- 1v1 and 2v2 remain valid development test configurations.
- Players beyond the six-player limit must be rejected or placed into spectator-only state.
- 5v5 support is not part of the current scope.

The match requires at least two lobby players, with at least one player on each team. Fresh players enter `PreRound` unassigned and choose their own team by shooting `JOIN RED` or `JOIN BLUE`; that same authoritative hit marks them ready. Before the match starts they may shoot the other target to switch teams, provided the destination is below the three-player cap. Players retain game-only mouse capture and may move and fire for warmup, but the server applies no player damage. Every tactical weapon has infinite reserve ammunition only during `PreRound`: the magazine, reload montage, and reload-time firing lock remain normal, while a completed reload refills the magazine without consuming the replicated finite reserve. Entering the first `Buy` recreates the round pawn/loadout with normal finite ammunition. The match starts only after every eligible lobby player has selected a team and readied up; an unselected player blocks the gate, and a single player remains in warmup indefinitely.

### 2.2 Match Structure

- First team to 5 round wins wins the match immediately.
- A match contains at most 9 completed rounds, including drawn rounds.
- If neither team has reached 5 wins after round 9, the team with more round wins wins the match.
- If the score is tied after round 9, the entire match ends in a draw.

### 2.3 Round Phases

| Phase | Purpose | Target Duration |
| --- | --- | ---: |
| PreRound | Movable/shootable no-damage warmup; shoot a red/blue wall target to select or switch team and ready up | N/A |
| Buy | Equipment purchase; active players frozen | 10s |
| Combat | Elimination combat and hunt-intel escalation | 105s |
| RoundEnd | Result, economy, and transition | 6s |
| MatchEnd | Final result and restart | 15s restart delay |

There is no capture phase, capture-point system, or overtime phase.

Once both teams qualify and the match starts, qualification is retained for the match. A qualified team with no living round pawn at a later Combat boundary is eliminated immediately; an empty connected roster is not treated as a non-participating team and does not consume the 105-second timer. A disconnected pawn retained for the current round remains a living round pawn even after its 30-second reconnect-status countdown has expired.

### 2.4 Connection Model — Steam Listen Server

- The selected packaged-PC multiplayer model is Steam Lobby + a player-hosted authoritative Listen Server. The host participates in the match and owns the authoritative UE world; no rented or always-on dedicated server is required for the current project scope.
- Steam Lobby/Session provides room creation, discovery, friend invitation, joining, and lifecycle metadata. SteamSockets/relay provides the packaged connection path and NAT traversal. Existing UE replication and server-authoritative gameplay remain responsible for movement, damage, phases, economy, and possession.
- If the listen host exits, crashes, or loses its authoritative connection, that match ends with `HostLost`/no-result semantics and all clients return to the multiplayer menu with a clear host-loss result. It is never recorded as a score draw or unilateral disconnect forfeit. Host migration is explicitly out of scope.
- Packaged Shipping admission requires a provider-qualified Steam identity, with the Steam Auth packet-handler component enabled. Non-Steam identities are rejected closed in Shipping; PIE and non-Shipping diagnostic builds retain explicit PIE/`GUEST` fallbacks.
- `UTacticalSessionSubsystem` is the sole asynchronous Session owner. It creates a presence/lobby Session capped at six players and owns Create/Find/Join/Destroy, invite acceptance, metadata updates, provider delegates, operation generations, and timeout cleanup. A newly created room starts fail-closed as `INITIALIZING`: it is not advertised, joinable, present in rich presence, or open to invites until the authoritative gameplay Listen Server world has reached `BeginPlay` and publishes `LOBBY`.
- Public discovery and normal joining accept only an exact `LOBBY` state and reject missing/unknown state, `INITIALIZING`, `IN_PROGRESS`, `CLOSING`, incompatible product/build/protocol, or a full room. At match start the host publishes `IN_PROGRESS` and disables advertising, presence joins, and invites before starting the provider Session. The low-level Session keeps join-in-progress enabled only so an authenticated, inactive member of the existing authoritative roster can reacquire a lost local named Session during same-host reconnect; the UI cannot discover it and the server rejects every fresh or duplicate-active identity.
- Create/Find/Join/Destroy and match-state transitions are bounded by watchdogs, generation fences, exact-interface delegate cleanup, finite provider retries, and convergence polling for provider `Starting`/`Ending` states. Host closure first publishes `CLOSING`, disables every admission surface, and raises a local GameMode admission fence before the named Session is destroyed.
- Development App ID `480` is isolated with the `TCAMP_FPS` product key. It must be replaced by the project's assigned Steam App ID before distribution, and a formal Shipping Steam build must also define `UE_PROJECT_STEAMSHIPPINGID=<assigned id>` in the Game target (or the equivalent release build configuration); compiling Shipping alone does not prove Steam runtime initialization.
- Host and client travel use the Steam Session-resolved connection string. A client interruption re-resolves or rejoins that same Session before using the retained-body recovery path; raw-IP reconnect remains only the editor/non-Steam diagnostic fallback. Client recovery after host loss is impossible without host migration and is not a goal.
- A participant's team slot is reserved independently of their current `PlayerController` and remains reserved for the match. Abandonment changes eligibility and penalty state but does not create a mid-match replacement slot.

Implementation references: [UE 5.7 Online Subsystem Steam](https://dev.epicgames.com/documentation/en-us/unreal-engine/online-subsystem-steam-interface-in-unreal-engine?application_version=5.7), [Steam Matchmaking and Lobbies](https://partner.steamgames.com/doc/features/multiplayer/matchmaking), and [Steam Networking](https://partner.steamgames.com/doc/features/multiplayer/networking).

## 3. Round And Match Resolution

### 3.1 Round Win Condition

Eliminating every living player on the enemy team is the only way to win a round.

Rules:

1. If Team A is fully eliminated and Team B still has a living player, Team B wins.
2. If Team B is fully eliminated and Team A still has a living player, Team A wins.
3. If both teams are eliminated within the same server resolution window, the round is a draw.
4. If Combat time expires while both teams still have living players, the round is a draw.
5. Living player count, remaining health, money, equipment, and treasure state never break a round tie.

### 3.2 Elimination Resolution Window

- After a possible team elimination, the server waits approximately 0.1-0.2 seconds before committing the result.
- The server evaluates both teams again when this short window ends.
- This window exists only to resolve near-simultaneous damage events consistently.
- It is not a visible phase and does not extend normal combat.

### 3.3 Draw Resolution

- A drawn round consumes one of the nine maximum rounds.
- Neither team gains a round-win point.
- Both teams receive the configured draw income.
- A draw does not increase or reset either team's loss streak.
- The round-end UI must distinguish `TimeExpiredDraw` from `SimultaneousEliminationDraw`.

### 3.4 Disconnect And Reconnect Rules

A non-host client network loss enters a server-authoritative recovery state only while the same listen host and authoritative world remain alive, instead of immediately deleting the player's round presence. Loss of the listen host is terminal for the match and never enters this player-reconnect path:

1. For an eligible `Buy` or `Combat` participant, the team slot remains reserved and the existing living pawn remains in the world until that current round reaches its authoritative boundary. `PreRound` retains only the participant record and selected team because there is no in-round pawn to preserve.
2. The server stops held movement, jumping, firing, aiming, breath hold, and interaction, and clears velocity. The pawn remains visible, collision-enabled, and normally damageable; normal movement physics or external forces can still move it, and disconnecting never grants invulnerability or removes it from enemy interaction.
3. A valid UID-bound reconnect claim while that pawn is alive and the same round is still in `Buy` or `Combat` rebinds the new controller to the same pawn, preserving its live health, armor, weapon, magazine, reserve ammunition, active slot, and then-current transform. The first 30 seconds remain the advertised automatic-recovery/status window; expiry ends the public countdown but is not a death or a hard same-round pawn-reclaim cutoff.
4. If that pawn dies while its owner is offline, the normal authoritative death/equipment path runs exactly once. A later reconnect enters teammate-only spectating until an eligible round boundary.
5. If the 30-second reconnect-status deadline expires while the pawn is alive, the server changes the public roster state from `Reconnecting` to `Disconnected`, clears the countdown, and keeps the pawn active, visible, collidable, damageable, and eligible for same-round reconnection. It does not apply damage, destroy the pawn, clear equipment, award a kill, settle the round, or change the round score.
6. A deliberate `Leave Match` request bypasses reconnect recovery and immediately applies the separate authoritative abandon policy only while the host connection can still carry the request, then cleans up the local Lobby/Session. If the client is already offline, it can leave locally while the host continues its public-status-window/current-round-retention policy; an out-of-band backend leave is not part of the current scope. When the listen host selects `End Match`, the authority publishes `HostLost`/no-result, reliably notifies connected clients, destroys the Session, and returns everyone to the frontend.
7. The current round always settles exactly once by its normal elimination/draw rules. A score victory/draw locked by that completed round has priority and cannot be reversed by a disconnect during the six-second `RoundEnd` presentation. Only when the match would otherwise continue does the server inspect connected rosters: a qualified team with no connected controller forfeits with reason `TeamDisconnectForfeit`. This match-only decision never calls the round-win/economy path and never adds a fabricated elimination point. A connected restricted spectator counts as present and remains eligible for the next legal round.
8. Under that same continuation-only check, if both qualified teams have no connected controller, the match ends with reason `AllTeamsDisconnectedAbort`, winner `255`, and no-result semantics. This is a technical match abort, not a round draw and not a scored match draw. A unilateral full-team disconnect is never an abort and therefore cannot be used to evade a loss.

The 30-second status timer begins when the authoritative host confirms connection loss; it is separate from the transport driver's own timeout and from the retained pawn's current-round lifetime. The client may begin retries as soon as it detects failure and may manually retry after the automatic sequence. After admission, a reconnect for the same participant must atomically supersede a stale connection or be rejected—two controllers may never own the participant or pawn at once. The current pass has no bot takeover: the disconnected controller supplies no movement or combat AI.

The current C++ admission envelope contains the stable identity, a per-match Match ID, a per-participant opaque 128-bit bearer credential, and protocol version `1`. Fresh joins carry no reconnect claim. Reconnects must provide the complete three-field claim; malformed/partial claims, wrong Match ID or credential, incompatible protocol, abandoned records, and duplicate active connections are rejected in both `PreLogin` and `InitNewPlayer`. The server sends material only to the owner. Rotation keeps the proven previous credential valid until the client caches and acknowledges the replacement; the same UID-bound identity also has one controlled recovery if its first-ever credential RPC was lost. Once a recovery begins, its admission-protection latch survives automatic failure and `CancelReconnect` until success, leave, or an explicit new remote travel. While armed, the client accepts a replacement admission only from an `NM_Client` controller for the cached Match ID. This prevents UE's temporary `NM_Standalone` post-disconnect fallback world from replacing the real remote credential or falsely acknowledging its local credential. The credential lasts until rotation or match reset, is memory-only on the client, and is not a platform session ticket.

Phase-specific admission rules:

- `PreRound`: preserve the selected team but clear readiness. The player must explicitly ready again after reconnecting. A Development `GUEST` whose first credential was never acknowledged is instead released immediately because it cannot yet prove ownership of the record.
- `Buy`: a reconnect may reclaim its retained living pawn before Combat, including after the 30-second status countdown has expired. A rostered player who was offline at round start may enter during Buy only before the Combat boundary and is then marked as having entered that round.
- `Combat`: a reconnect may reclaim only its retained living pawn from the same current round, including after the status countdown has expired. If no living retained pawn exists and the match can continue because the team still has a connected member, the player remains a restricted teammate spectator until the next eligible round.
- `RoundEnd`: close current-round pawn retention without applying an abandon penalty, clean up that round's pawn normally, and reconnect as a restricted spectator for the following Buy phase when the whole-team terminal rule does not end the match.
- `RoundEnd` continuation: first preserve any normal score result locked by the completed round. If the match is not already over, proceed to Buy only when both qualified teams still have at least one connected controller; one absent team loses the match by `TeamDisconnectForfeit` without another round point, while two absent teams produce `AllTeamsDisconnectedAbort` rather than a normal draw.
- `MatchEnd`: restore result access only; do not spawn another pawn.

Settlement and roster rules:

- Round participation is recorded independently of connection state. A participant receives a settlement exactly once only for a round it entered; remaining offline does not grant income for later absent rounds.
- A mid-match disconnect never frees one of the three team slots. A fresh join-in-progress player cannot replace that reserved participant and is rejected or placed in an explicitly supported spectator-only slot.
- A reconnect is never moved automatically to the opposite team.
- Client-requested engine respawns remain rejected after match start. Repossession of a retained pawn and GameMode-authorized round spawning are the only legal return paths.
- World teardown never creates reconnect recovery state, elimination, equipment loss, score, or economy settlement events.
- All connection-state, possession, round, and economy decisions remain server-authoritative and idempotent.

## 4. Anti-Stall Hunt Intelligence

The mode remains pure elimination, but information increases near the end of Combat so hiding is not a dominant strategy.

### 4.1 Hunt Timing

- More than 30 seconds remaining: no automatic enemy-position information.
- 30 to 16 seconds remaining: show an approximate enemy-area pulse every 8 seconds.
- 15 to 0 seconds remaining: show a shorter-interval enemy-position pulse every 4 seconds.
- Both teams receive equivalent information.
- The system reveals information only; it does not deal damage or change the win condition.

### 4.2 Information Rules

- Only living enemy players are included.
- Approximate pulses should be spatially imprecise enough to preserve clearing and aiming decisions.
- Dead players see only the same information available to the teammate they are spectating.
- All hunt markers and warnings are cleared when the round ends.
- UI must clearly warn players when hunt intelligence becomes active or increases in frequency.

### 4.3 Map And Solo-Production Scope

- The map keeps one fixed macro skeleton: team starts, main lanes, room ownership, and the routes required for fair rotation do not move between rounds.
- The intended art-production path is one coherent purchased modular environment pack, not several unrelated packs or a large custom-asset workload. The selected pack should expose separate walls, doors, barriers, crates, and cover props with usable pivots, collision, and LOD/Nanite support rather than only a single merged demonstration level.
- The implemented fixed-layout treasure pass uses exactly three C++ candidate sites. Their manual PIE acceptance must check comparable navigable travel distance, at least two practical approaches, useful claimant cover, guard navigation, and no dominant spawn-side sightline.
- A later incremental pass may add exactly three complete, manually authored local-layout presets using the same asset kit. A preset may change a door, one side route, and a small cover cluster, but it may not rearrange the entire map or generate arbitrary combinations.
- The authority selects the local-layout preset before round respawn and replicates one layout identifier. All relevant visibility, collision, guard navigation, and treasure-site compatibility must be applied before active play, so clients cannot disagree and a changing layout cannot trap a player.
- Version one may ship with only the three randomized treasure sites and no changing walls or routes. The three local-layout presets are an optional second increment after the fixed-layout treasure loop is accepted.
- Runtime PCG or procedural assembly must not control collision-bearing competitive geometry. It may later be used only for non-gameplay decoration that cannot alter routes, cover, visibility, or navigation.
- Full map restructuring, a newly constructed three-lane map, and unrestricted procedural map generation remain out of scope. Playtest data may later justify a separate map-redesign task.

## 5. Economy And Equipment

### 5.1 Initial Balance Values

| Item or Event | Initial Value |
| --- | ---: |
| Starting money | 1000 |
| Maximum money | 9000 |
| Player kill reward | 200 |
| AI kill reward | 0 |
| Round win income | 3000 |
| First loss income | 1700 |
| Second consecutive loss income | 2100 |
| Third and later consecutive loss income | 2500 |
| Draw income | 1700 |
| Team treasure reward per eligible player | 500 |

Starting money is granted once when entering a new match. It is not granted again at the beginning of every later round.

### 5.2 Initial Price Table

| Equipment | Price |
| --- | ---: |
| Default pistol | 0 |
| Upgraded pistol | 500 |
| Rifle | 2500 |
| Sniper | 4000 |
| Light armor | 650 |
| Heavy armor | 1250 |

The DataTables used by purchase UI and server purchase validation must use the same values. Hard-coded fallback prices must also match.

Initial armor balance:

| Armor | Durability | Body absorption | Head absorption |
| --- | ---: | ---: | ---: |
| Light | 100 | 50% | 0% |
| Heavy | 200 | 50% | 50% |

Absorbed damage removes the same amount from armor durability. The owner HUD uses a compact blue rounded progress bar directly above the health bar and shows the exact current and maximum durability inside it; the bar is hidden when the player has no armor. Whenever armor actually absorbs damage, a replicated blue shield shell flashes around the character for approximately 0.3 seconds. A light-armor head hit does not flash because light armor has no head protection.

### 5.3 Reward Rules

- Player kill money is awarded only for a valid enemy player kill.
- Suicide, environmental death, and team kill do not award player-kill money.
- AI guards do not award direct kill money.
- A round winner receives win income and has its loss streak reset.
- A losing team advances its loss streak and receives the corresponding income.
- A drawn round grants draw income to both teams and leaves both loss streaks unchanged.
- All money gains are clamped to the 9000 maximum.

### 5.4 Purchase Rules

- Purchases are accepted by the server only during the Buy phase.
- The player must be an active participant with a valid living tactical character.
- The server validates price, available money, item data, and slot rules.
- UI visibility is not treated as purchase authorization.
- Purchase results must give clear success or failure feedback.

### 5.5 Equipment Persistence

- A living player keeps weapon and armor state for the next round.
- Remaining armor durability is preserved for a survivor.
- A dead player loses non-default weapons and armor for the next round.
- The default pistol is restored when the player has no valid pistol.
- A valid weapon collected from the world can be carried into the next round if the collector survives.
- A non-default weapon may drop on death according to the weapon-drop rule.
- Default pistols do not create economy-value drops.
- An unexpected disconnect does not itself clear equipment while the retained pawn is alive in its current round.
- Equipment is cleared only when the retained pawn actually dies, the current-round boundary performs normal survivor cleanup, or preservation fails and the participant must enter the existing death-equivalent fallback. Status-countdown expiry alone never clears equipment or eliminates the pawn. The match-level unilateral forfeit prevents disconnect-to-save-the-match exploits without fabricating a round kill.
- Exact health, position, magazine, reserve ammunition, and transient action state belong to the retained pawn and are not reconstructed from a reconnect snapshot. Only canonical next-round economy, armor, and loadout state are persisted separately.

The exact number of weapons dropped by a dead player can remain one for the initial implementation, but all non-default saved loadout state must be cleared for that dead player.

## 6. Treasure Resource System

The fixed-layout treasure version is now enabled and mechanically implemented in C++. It remains a secondary resource opportunity and does not affect round resolution. Its multiplayer gameplay, AI, presentation, and balance still require manual PIE acceptance before the implementation is treated as accepted.

### 6.1 Per-Round Setup

- At most one treasure chest is active in a round.
- The fixed-layout implementation exposes exactly three C++ candidate positions. Authority privately selects one at round setup; the future optional local-layout system does not participate yet.
- Candidate positions must be reasonably symmetric by navigable travel time, provide at least two practical approaches, and provide enough cover for a coordinated claim without making the claimant fully safe.
- The selected site remains inactive at Combat start. At approximately 75 seconds remaining, both teams receive the same advance location announcement; at 70 seconds remaining, the chest and its two guards activate. This leaves an opening pure-PvP period and creates a predictable mid-round contest before the 30-second hunt-intelligence window.
- Activation timing is fixed rather than randomized so neither team gains information or preparation advantage from hidden timing variance.
- The chest and its guards reset for every new round.

This first version supplies the three candidates in C++ and does not rebuild or randomize the map skeleton. Manual playtesting must still validate those locations against the existing geometry and navigation.

### 6.2 Constrained Local-Layout Variation

- The first playable treasure version keeps the map layout fixed and randomizes only the active treasure site.
- After that version is accepted, one layout controller may activate one of three pre-authored local-layout presets per round. Each preset reuses the same purchased modular asset kit and changes only a small, explicit set of doors, side-route blockers, and cover groups.
- Presets are complete reviewed combinations, not independent random switches. Each one must preserve fair starts, valid primary routes, treasure access, sightline limits, collision, and guard navigation.
- Layout selection occurs before participant respawn. The server owns the selected identifier, and clients present only that authoritative selection.
- Large runtime mesh reconstruction, arbitrary modular-room assembly, and navigation-critical PCG are not part of the selected implementation.

### 6.3 Guard Rules

- The chest activates only after exactly 2 guards have been created successfully. Incomplete guard creation fails closed instead of exposing an unguarded chest.
- Guards share the chest location as their home position.
- Guards must disengage and return home when exceeding their leash radius.
- Guards do not provide direct money when killed.
- Both guards must be dead before the chest becomes claimable.
- Guard targeting must remain neutral between player teams.

### 6.4 Claim Rules

- Any living active player may claim an unlocked chest.
- Claiming requires holding the interaction input for 2.0 seconds.
- The claimant cannot fire while the interaction is active; teammates are expected to provide cover.
- Releasing the input, leaving range, dying, or losing eligibility cancels the claim.
- Nonlethal damage alone does not cancel progress. Requiring only one low-damage hit to reset a claim would let a distant defender stall the objective indefinitely; death still cancels it immediately.
- The in-progress chest state and opening cue are server-authored and clearly presented, so a contest is readable rather than a silent reward.
- The server accepts the first valid claim request as the exclusive claimant. Repeated requests from that same player are idempotent and keep the original timer and firing lock; competing requests are rejected until the active claim completes or cancels.
- A team may steal a chest after the opposing team kills the guards.

### 6.5 Reward Rules

- Every eligible stable-roster participant on the claimant's team who entered the current round receives 500 money exactly once, clamped to the 9000 maximum. Eligibility is based on authoritative round/team membership rather than presence at the claim instant, so an eliminated teammate and a temporarily offline but non-abandoned participant retain the team economy reward.
- The claimant receives up to 40 health, without exceeding maximum health.
- Treasure does not grant full healing.
- Treasure does not repair armor or grant temporary overshield, movement speed, or weapon damage in the initial version. Direct damage buffs are especially excluded because they change learned time-to-kill thresholds; any later clearly telegraphed temporary buff requires a separate balance pass.
- Treasure never grants a round point or changes round resolution.

## 7. UI And Spectator Requirements

### 7.1 Combat HUD Priority

The HUD should prioritize:

1. Remaining round time.
2. Living player count and player avatar state for both teams.
3. Team round score and current round number.
4. Hunt-intelligence warning and pulse feedback.
5. Treasure state.

The always-available aiming and combat feedback layer is native C++ rather than Blueprint-dependent:

- The former circular reticle is replaced by the transparent RAR `T_IG_ATT_Sight_Dot_03_D` sight texture in native C++; the prior four-segment reticle is only a missing-asset fallback. The sight remains visible in `PreRound`, where shooting is required for team selection/readiness, but is hidden while the local player is spectating.
- The RAR `T_IG_UI_Hitmarker` texture replaces rather than overlays the regular sight for its `0.16s` pulse; repeated confirmed hits restart that interval, and the sight returns when the pulse ends. A hit marker is shooter-only and server-confirmed. Hitscan and projectile weapons may request it only after the authoritative damage call returns `AppliedDamage > 0`; the owning `PlayerController` then delivers the presentation through an unreliable client RPC. World impacts, blocked damage, warmup fire, and other zero-damage contacts must not produce a marker. Spectator/hidden-HUD transitions clear a pulse and reject delayed notifications.
- Damage feedback uses eight C++-generated directional masks plus one omnidirectional fallback mask. The server sends the owning victim a transient world-space source direction only after positive nonlethal damage is applied; the client maps it through the current camera yaw. A cardinal hit covers its complete corresponding screen edge with a high-contrast dark-red falloff and a soft semi-elliptical shape extending inward. A diagonal hit joins two complete neighboring edges, shifts both arcs toward the source corner, and makes the source-side portions darker without hiding the far ends. The four edges use the same curve, the center and opposite-edge centers stay transparent, and different sectors keep independent pulses so crossfire can remain visible. Every pulse fades in for `0.08s`, holds for `0.10s`, and fades out for `0.32s`; repeated hits in one sector continue from its current opacity rather than flashing. Missing/vertical-only source directions use the omnidirectional fallback. Health initialization/rebind, respawn, reconnect snapshots, spectators, lethal hits, and disconnected retained bodies must remain silent.
- The previous solid-color Blueprint damage animation is retained only as a compatibility fallback when the native widget cannot be created. Native and legacy damage overlays must never be shown together.

### 7.2 Round And Match Results

Round-end UI must support:

- Team A won by elimination.
- Team B won by elimination.
- Draw by time expiration.
- Draw by simultaneous elimination.

Match-end UI must support:

- Team A or Team B wins by score.
- Match draw after the ninth round (`ScoreDraw`).
- One team wins because the opposing team fully disconnected (`TeamDisconnectForfeit`).
- Both teams disconnected and the match ended without a winner or recorded draw (`AllTeamsDisconnectedAbort`).
- The listen host was lost and the match ended without a result (`HostLost`, after Steam/session integration).

The replicated match result must contain both `MatchWinner` and `MatchEndReason`. Winner `255` is not sufficient to distinguish a legitimate score draw from a technical abort. Disconnect-forfeit and abort presentation must not reuse the round-result event or imply that another elimination round was scored.

### 7.3 Spectator Rules

- Dead players can cycle only through living teammates.
- Free-camera spectating is disabled.
- The spectator camera follows the selected teammate's full view direction, including replicated vertical aim pitch as well as horizontal rotation.
- Spectators receive no extra enemy information beyond the current teammate's available information.
- A living player cannot request spectator data. The server validates a dead/pawnless viewer, a trusted preserved team, an active living target, matching PlayerState/Pawn ownership, and same-team membership before publishing observer state.
- Exact target ammunition remains owner-only gameplay data and is relayed only inside the requesting PlayerController's validated observer snapshot; it is never made globally replicated.
- Spectator UI shows an explicit observing state, target identity/team/index, exact health, armor, current weapon, magazine/reserve ammunition, round time, and round-end reason. The dead player's own health/ammo/armor widgets are hidden until normal play resumes.
- The target's real weapon-specific first-person arms and weapon are enabled only as a reversible local presentation override for the observing client. The same target's real third-person Manny is hidden locally while observed so the first-person camera cannot see a duplicate body. Target switching, death, destruction, respawn, phase exit, and controller teardown restore all changed mesh, animation, visibility, and Tick state.

### 7.4 Reconnect Experience

- The implemented native client overlay appears when a scoped client network failure starts recovery. It blocks gameplay movement/look input through retries and while awaiting a valid `PlayerState` plus either pawn possession or restricted spectator state.
- Automatic attempts use the implemented delays `0`, `1`, `2`, `4`, and `8` seconds. Exhaustion leaves persistent native `RETRY` and `LEAVE MATCH` controls; only one retry/ticker sequence may be active.
- The overlay reports scheduled/current attempt and transport/admission progress. A physically disconnected client cannot receive a live authoritative deadline update from the unreachable host. The public 30-second value describes the server's initial reconnect-status window, not the lifetime of a living retained body; after automatic retries stop, the player may still retry manually while the same body is alive in the same Buy/Combat round.
- Network/travel failures and server admission rejection text are surfaced through the reconnect or multiplayer-menu state. Native product wording now covers Steam unavailable, incompatible/full/in-progress room, create/find/join/invite failure, Session expiry, and host loss; kick-specific policy remains future work.
- `ATacticalPlayerState` replicates privacy-safe `Connected`, `Reconnecting`, `Disconnected`, and `Abandoned` status values plus the absolute server status-window deadline. The native hold-Tab 3v3 roster derives its countdown from synchronized server time and changes an expired living body from `Reconnecting` to `Disconnected` without marking it dead. It shows alive, waiting, eliminated, reconnecting, disconnected, and left states without exposing identity, address, Match ID, or credential data. Its final multi-window visual/network acceptance remains pending.
- Successful recovery currently closes the blocking overlay after replicated gameplay/spectator state is observed. Separate polished success banners such as `Reconnected — Resuming` or `Reconnected — Spectating Until Next Round` remain optional presentation work.
- While the host connection is usable, the native `Leave Match` path suppresses automatic reconnect, asks the authority to mark the participant abandoned and resolve its pawn without enemy reward, clears cached address/admission material, destroys/leaves the local named Session, and returns to the frontend. Once already offline it can only clear local state and leave while the host continues its public-status-window/current-round-retention policy.

### 7.5 Player Character Presentation

- The world-facing body uses `/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple`. The owner/spectator first-person body is weapon-specific: the pistol retains the Manny/template path, while the rifle uses RAR's `/Game/InfimaGames/ArtCore/Mannequin/Meshes/SK_IG_FP_Mannequin`. The retired SciFi visual/retarget layer is not part of the current player path.
- The third-person Manny is the visible world body as well as the animation/gameplay authority. It owns the third-person `HandGrip_R`, world weapon mesh, authoritative remote muzzle transform, pistol/rifle/unarmed AnimBPs, fire/reload montages, remote aim pitch, damage collision, headshot checks, and death ragdoll.
- The visible first-person arms use their weapon-specific AnimBP, fire/reload montages, and mount. The pistol retains the earlier Manny/Control Rig presentation. The rifle's first-person RAR gun mounts to `SOCKET_Weapon`, and its world gun mounts to the third-person Manny's `HandGrip_R`; both views use the same RAR gun mesh and matching modular parts with a `Muzzle` socket. The camera and arms share a stable eye-height pivot; rifle arms/gun follow local or replicated view pitch there, while the pistol's existing Control Rig pitch is not applied a second time.
- The SciFi presentation components, `UTacticalPlayerRetargetAnimInstance`, `IK_TcampManny`, `IK_SciFiTrooper`, `RTG_TcampManny_To_SciFiTrooper`, and the post-retarget arm/finger/muzzle corrections are disabled for current players. Their C++ implementation and content assets are retained as inactive history for a later final-character migration; they are not instantiated as the live body and are not valid live-weapon attachment targets.
- The active first-person mesh evaluates continuously only for the locally controlled pawn or a pawn under the local spectator override. Normal remote/server copies remain visibility-throttled. When `PawnClientRestart` returns a retained pawn to local control, the game restores the correct weapon-specific first-person mesh, AnimBP, evaluation policy, visibility, and mount before normal play resumes.
- A local observer hides the selected target's actual third-person Manny, enables that target's actual weapon-specific first-person arms and gun, and runs the appropriate AnimBP locally. Exiting observation, changing targets, target death/destruction, respawn, and controller teardown restore cached state without reviving a body that a newer death state hid.
- Model-level red/blue/neutral tinting is temporarily suspended because the SciFi team materials are no longer visible. Replicated team selection, damage/friendly-enemy rules, the top-left HUD team label, hold-Tab roster columns, and spectator team text are unchanged. A later Manny-compatible material or final-character art pass must restore immediate model-level team readability.
- The current visual acceptance target is the actual mixed asset path: Manny pistol, RAR rifle first-person arms/gun, and Manny world body. Check right-hand/socket/weapon lock, plausible left-hand contact, forward muzzle direction, view-pitch follow-through, and consistent first-/third-person motion during idle, locomotion, jumping, ADS, fire, reload, and switching. Because collision and visible third-person geometry are the same Manny body, the prior hidden-Manny/visible-SciFi silhouette mismatch remains removed.

Historical Manny rollback status as of 2026-09-21 (superseded for the rifle first-person path by the RAR integration):

- The architecture has returned to direct Manny first-/third-person rendering and direct Manny socket ownership. It no longer claims the SciFi retarget/FABRIK path as current behavior.
- UE 5.7 Development Editor and Development Game compile and link successfully; Shipping was not rebuilt for this rollback.
- The focused `Tcamp.Tactical.PlayerPresentation.Configuration` test passes at `Saved/Automation/MannyRestoreFocused/index.json`. The final complete `Tcamp.Tactical` run passes `17/17`, with `0` warnings, `0` failures, and `0` skipped tests at `Saved/Automation/MannyRestoreFinal/index.json`.
- `Saved/Logs/MannyRestoreSmoke.log` and `Saved/Logs/MannyRestoreRenderedSmoke.log` contain no fatal error, assertion, Blueprint runtime error, or `Accessed None`; they confirm `Parent="First Person Mesh" Socket="HandGrip_R" Mesh=Manny` and do not initialize the retired SciFi `PlayerPresentation`/first-person `GripIK` path. `Saved/Screenshots/WindowsEditor/HighresScreenshot00012.png` shows the restored Manny hands and pistol.
- Two-window rendered PIE, real reconnect restoration, and full-3v3 frame pacing remain required before presentation is visually accepted.

Historical SciFi mechanical verification from 2026-09-19 (superseded by the 2026-09-21 Manny rollback, retained for future migration reference):

- UE 5.7 Development Editor, Development Game, and Shipping Game compile and link for the two-layer first-/third-person presentation architecture.
- The complete `Tcamp.Tactical` automation group passes `16/16` with zero warnings, zero failures, and zero skipped tests. The report is `Saved/Automation/FirstPersonPresentationFullFinal/index.json`. Coverage now includes the first-person CDO component, source attachment, owner-only/first-person render path, collision/overlap/shadow rules, head/neck availability, runtime retarget class, assets, rigs/chains/operations, hand goals/Limb IK, sockets, and replicated team appearance.
- A fresh NullRHI gameplay smoke logs `PlayerPresentationInitialized`, `PlayerFirstPersonPresentationInitialized` with `HiddenBones="neck_01,head"`, and neutral `PlayerTeamAppearanceApplied`, with no fatal error, assertion, Blueprint runtime error, `Accessed None`, or presentation fallback in the checked path. Its log is `Saved/Logs/FirstPersonSciFiPresentationFinalSmoke.log`.
- These reports describe only the retired SciFi presentation and do not accept the current Manny rollback. They remain useful evidence if a future final character reuses the bridge.

Combat-feedback verification on 2026-09-19 adds a successful Development Editor, Development Game, and Shipping Game build; a `17/17` complete `Tcamp.Tactical` automation pass with no warnings, failures, or skipped tests at `Saved/Automation/CombatFeedbackSpatialGradientFinal/index.json`; a clean Tactical GameMode NullRHI smoke at `Saved/Logs/CombatFeedbackSpatialGradientSmoke.log`; and a 1280x720 UI capture at `Saved/Screenshots/WindowsEditor/ScreenShot00000.png` confirming the centered outlined hollow reticle with the authored circle suppressed. Rendered two-window PIE remains the acceptance gate for final line thickness/scale, both weapon paths, wall/zero-damage rejection, damage-gradient readability, repeated-hit refresh, and spectator visibility transitions.

The temporal damage-feedback refinement was mechanically verified on 2026-09-20 with successful UE 5.7 Development Editor, Development Game, and Shipping Game builds. The targeted `Tcamp.Tactical.UI.CombatFeedbackStyle` test passes with no warnings or failures at `Saved/Automation/DamageFeedbackFadeFinal/index.json`, covering the `0.08s` fade-in, `0.10s` hold, `0.32s` fade-out, and continuous repeated-hit restart. Rendered PIE remains required to accept the final perceived timing and intensity.

Directional damage feedback was mechanically verified on 2026-09-20 with successful UE 5.7 Development Editor, Development Game, and Shipping Game builds. The complete `Tcamp.Tactical` group passes `17/17` with no warnings, failures, or skipped tests at `Saved/Automation/DirectionalDamageFeedbackFull/index.json`; the focused report is `Saved/Automation/DirectionalDamageFeedbackFinal/index.json`. Hitscan now records `AimDir`, projectiles record their travel direction, the victim receives only a transient owner-client cue after positive nonlethal applied damage, and local Combat/alive/own-camera gates reject delayed cues during death, spectating, round end, or reconnect catch-up. Rendered two-window PIE remains required to accept directional-mask coverage, intensity, and perceived direction under real play.

The directional damage masks received their current high-contrast arc treatment on 2026-09-21 without changing direction mapping or timing. Cardinal hits still fill the whole selected edge, but a power-`2.0` base now concentrates opacity near the outer border and a semi-elliptical layer gives the inward transition a curved profile. Diagonal arcs shift toward the source corner and use a `0.32 -> 1.0` along-edge bias. The darker blood-red overlay peaks at `0.70` opacity, with the center and opposite-edge centers transparent. Development Editor, Development Game, and Shipping Game builds pass; the focused report is `Saved/Automation/DamageFeedbackArcContrastFinal/index.json`, the complete `17/17` regression is `Saved/Automation/DamageFeedbackArcContrastFull/index.json`, and the clean gameplay smoke is `Saved/Logs/DamageFeedbackArcContrastSmoke.log`. Final perceived arc shape and intensity remain a PIE check.

Historical note — superseded by the 2026-09-21 Manny rollback: first-person SciFi weapon-grip alignment was mechanically refined through a native arm-retarget profile, a SciFi-owned visible weapon, and a post-retarget left-hand FABRIK pass. Development Editor, Development Game, and Shipping Game builds passed; `Saved/Automation/FirstPersonLeftHandIKFinal/index.json`, `Saved/Automation/FirstPersonLeftHandIKFull/index.json`, and `Saved/Logs/FirstPersonLeftHandIKRenderedSmoke.log` retain that evidence. Those results are not current-player acceptance; they are preserved only as reference if a future final character returns to the retarget architecture.

### 7.6 Cosmetic Bullet Tracers — Implemented, Rendered Acceptance Pending

The implementation keeps server-authoritative hitscan damage, spread, penetration, ammunition, and hit confirmation unchanged. A tracer is presentation only: it performs no collision query and cannot award damage or a hit marker. The RAR demo projectile and its migrated `MI_IG_Bullet_Glow` are not used; the latter has no valid parent after migration. A new standalone, cookable `/Game/Tactical/FX/M_TacticalTracer` material provides the local visual. The originally proposed Niagara Beam asset was not reliably authorable through the project's available UE 5.7 scripting APIs, so this first version uses a tiny C++ local-only moving-streak actor instead. Niagara remains an optional later art/performance replacement, not a blocker for the gameplay-independent tracer.

Implemented path:

1. The existing unreliable `AShooterWeapon::MulticastHitscanResult` carries quantized server trace start and visual endpoint, a `uint16` per-weapon shot ID, and the unchanged ordered impact list in one event. A miss ends at `20000` cm along the authoritative spread direction instead of the `100000` cm gameplay trace. A stopping hit ends at its final impact (capped to the cosmetic range); if a penetration exits into open space, the streak continues to the bounded miss endpoint. The existing `BP_OnHitscanHit` calls remain separate from tracer creation.
2. Each receiving local world chooses the visible gun's actual `Muzzle` socket: owner first-person, ordinary observer third-person, or the current restricted spectator's explicitly exposed first-person weapon. Missing/hidden sockets, stale weapon positions, backward/near-zero paths, and dedicated-server worlds skip the effect. The server trace start is retained only as shot-direction/staleness evidence; it is not blindly used as the rendered muzzle.
3. `ATacticalBulletTracer` is non-replicating, non-colliding, shadowless, and transient. It moves a maximum `280` cm illuminated cylinder segment along the selected path at a nominal `120000` cm/s, with a clamped `0.045–0.12` second travel time and automatic destruction. First-person streaks use the first-person primitive projection. The standalone additive/unlit material has tunable color, brightness, and opacity. Muzzle flash, impacts, and shooter-only hit-marker logic are unaffected.
4. There is no per-frame network update or replicated projectile/tracer. Visuals cull beyond `30000` cm from the local camera, and `TracerEveryNthShot` can thin them deterministically by server shot ID (default `1`, every shot). `tc.Tracer.Enabled 0|1` allows a local on/off performance comparison. The server-authored tracer is separate from the small-scope owner fire-presentation prediction in Section 7.10; do not emit a second owner tracer without an explicit prediction/acknowledgement pair. Pooling and Niagara are conditional follow-ups only if six-player profiling shows meaningful cost.

Acceptance criteria:

- Owner, remote player, and first-person spectator see a consistent server-authored shot/impact from the correct locally visible muzzle, allowing only expected close-range parallax between view-specific muzzles. Check vertical aim, jumping, ADS, and weapon switching. No duplicate owner tracer appears if local prediction is added.
- Hits, wall impacts, misses, penetration, and zero-damage warmup shots have sensible visual paths. A missed shot still has a bounded endpoint; a hit never draws through its stopping surface. Tracers never create false hit markers or apply extra damage.
- A missed unreliable cosmetic event, reconnect, death, round transition, or destroyed/replaced weapon cannot leave a persistent streak or stale effect.
- Two-window PIE and a six-player test verify appearance and frame pacing; a three-player 2v1 setup is required for sustained first-person teammate spectating. Packaged network testing confirms the visual does not depend on an editor-only asset or an uncooked RAR demo dependency.

Mechanical verification on 2026-10-10: Development Editor/Game builds pass and the expanded `Tcamp.Tactical` automation group passes `21/21` at `Saved/Logs/TracerAutomationFinal20261010.log`, including bounded/miss/stop/penetration path rules, multicast flags, shot-ID frequency, material loading, and non-replication. Material reloading is verified at `Saved/Logs/TracerMaterialVerify20261010.log`. Windows map cooking finishes with `0` errors at `Saved/Logs/TracerCook20261010.log` and includes `Saved/Cooked/Windows/Tcamp_FPS/Content/Tactical/FX/M_TacticalTracer.uasset`; the two cook warnings are an existing Paragon rig-class load and the intentional `-NoSteam` switch. Rendered multiplayer appearance and six-player frame-time acceptance remain pending; a passing cook or NullRHI rule test does not establish perceived width/brightness or first-person parallax.

Implementation references: [UE 5.7 Niagara Beam guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-create-a-beam-effect-in-niagara-for-unreal-engine?application_version=5.7) and [UE 5.7 RPC guidance for transient cosmetic effects](https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-procedure-calls-in-unreal-engine?application_version=5.7). A persistent pooled/batched Niagara system, as in [Epic's optimization tutorial](https://dev.epicgames.com/community/learning/tutorials/0q6O/unreal-engine-optimizing-niagara-systems-as-a-service), is an optional later response to measured automatic-fire cost, not a first-version prerequisite.

### 7.7 Enemy Body-Hit Feedback — Implemented, Rendered Acceptance Pending

Add a small world-space impact at the actual victim, separate from the shooter's confirmed HUD hit marker, the victim's directional screen vignette, and the existing full-body blue armor shield. The first pass should look like a brief local flash/spark at the contact point, not a large blood cloud or persistent laser/decal. A health hit uses a restrained warm red-orange effect; full armor absorption uses a blue-white effect plus the existing shield flash; a mixed armor/health hit uses a red-orange center and a wider blue-white armor halo while the shield still indicates absorption. A headshot with real health loss may be modestly brighter/larger. This is cosmetic only and must not alter damage, ammunition, penetration, team rules, or scoring.

Implementation order:

1. Derive feedback **after** authoritative damage resolution in `ATacticalCharacter::TakeDamage` and `AShooterNPC::TakeDamage`. Compare health before/after and, for players, armor durability before/after: health loss -> health/head effect; no health loss but armor consumption -> blue armor effect; neither -> no body effect. A `TakeDamage` return value alone is insufficient for precise HP loss on overkill, and the hitscan `FHitscanHitData.HitType == character` is only collision, not confirmed damage. Combat phase, death, warmup, and same-team contacts must not create enemy-damage visuals. This does not silently change the existing friendly-fire gameplay policy.
2. For point damage, take the server impact point, normal, and optional bone from `FPointDamageEvent::HitInfo`; validate/fallback to the victim mesh for unusual projectile/explosion hit data. Both hitscan and direct projectile damage use this path; explosion victims may each get at most one small contact effect rather than a burst per overlapping component. Preserve the current shooter-only hit-marker decision and the armor component's blue shield multicast.
3. Deliver a compact transient feedback type and contact transform through one unreliable victim multicast only after a genuine resolved effect. Each local machine spawns a non-replicated, non-colliding, shadowless, short-lived impact, following the hit bone where available; skip the victim's own first-person near-camera rendering because their directional damage HUD already handles that view. A dropped cosmetic packet, death ragdoll, reconnect, or round reset must never leave a persistent effect. An independent additive/unlit impact material avoids the missing dependencies in the migrated RAR bullet-impact and Paragon hero effects; if reliable Niagara authoring becomes available, it may replace the local visual without changing the server classification/network contract.
4. Budget for six players: cull distant/unseen contacts, auto-destroy around `0.1–0.2s`, no replicated FX actor or per-frame network traffic, and profile the effect on/off before adding pooling or persistent skeletal decals. Material flash, hit-react montage, and bone decals are optional later art passes because they require more material/animation integration and can interfere with aiming/reload readability.

Acceptance: two-player Combat hits visibly mark the enemy at the actual impact, distinguish HP from armor absorption, and preserve the existing HUD/shield behavior; PreRound and blocked/zero-damage contacts never play a red damage effect. Test both hitscan and projectile paths, head/body, a fully absorbed hit, mixed armor/HP, lethal ragdoll, fast movement, 2v1 spectator, penetration, reconnect, and six-player automatic-fire frame pacing. Do not present the effect as fully visually accepted until rendered PIE and a cooked build have been checked.

Implemented on 2026-10-10: player and guard `TakeDamage` classify true post-resolution HP/armor changes; the player path records actual armor absorbed so death cleanup cannot be mistaken for another armor hit. Friendly/self/unknown-attacker point hits produce no *enemy* body effect without changing their existing damage rules. A single unreliable victim multicast carries bit flags plus impact point/normal/bone, and local viewers create a `0.16–0.20s` non-replicating, collisionless, shadowless, bone-following flash actor. Full armor absorption is blue-white, HP loss is red-orange, and mixed hits display both a red center and a wider blue-white halo. Headshots are slightly brighter/larger. The independent `/Game/Tactical/FX/M_TacticalBodyHit` additive/unlit material supports local fade-out. `tc.BodyHitFx.Enabled 0|1` can isolate its local rendering cost; it never toggles authoritative damage or RPC delivery.

Mechanical checks: Development Editor and Game builds pass; all `23/23` `Tcamp.Tactical` automation tests pass in `Saved/Logs/BodyHitFxAutomationFinal20261010.log`, including classification, hard material reference, non-replication and unreliable multiplayer contract. Windows game-map cook passes with `0` errors in `Saved/Logs/BodyHitFxMapCook20261010.log` and includes `Saved/Cooked/Windows/Tcamp_FPS/Content/Tactical/FX/M_TacticalBodyHit.uasset`; its two warnings remain the existing Paragon rig class and intentional `-NoSteam`. This does not substitute for rendered two-window PIE, six-player profiling, or packaged multiplayer visual acceptance.

Follow-up after the first PIE attempt: ordinary light/heavy armor absorbs 50% body damage while the other 50% removes HP, so an exclusive health-color branch prevented the blue-white contact color from ever appearing on normal armored body hits. The existing shield was also invisible because the Blueprint's runtime `ShieldSphere` has no mesh/material, while `UArmorComponent::ResolveShieldVisual` selected it by name over the valid native sphere. The C++ follow-up now renders both contact colors for mixed hits and rejects empty Blueprint shield candidates, attaching/registering the native oval fallback. This changes only presentation; the 50% armor reduction and all hit/damage rules are unchanged. Rendered PIE must still confirm the perceived sizes and colors.

Follow-up mechanical verification: Development Editor and Game targets compile, all `24/24` `Tcamp.Tactical` automation tests pass in `Saved/Logs/BodyHitShieldFixAutomation20261010.log` (including the empty-Blueprint-shield fallback), and a fresh full Windows game-map cook finishes with `0` errors at `Saved/Logs/BodyHitShieldFixCook20261010.log`. This is not a rendered two-window PIE acceptance of the shield geometry, blue halo, or frame pacing.

### 7.8 Weapon ADS And Sight Alignment — Native First Pass Implemented, Rendered Acceptance Pending

Preserve the current authoritative weapon/inventory architecture. ADS is a complete first-person pose and sight alignment, not merely an instantaneous camera-FOV change. Keep camera/control rotation authoritative and stable while smoothly blending FOV and the visible arms/weapon together; sprint, reload, weapon switch, death, spectator entry, and round lockouts must leave no stuck aim state. Hip fire and ADS retain their existing gameplay spread distinction.

- Rifle: reuse the imported RAR first-person aimed idle/movement, aim-in/out, aimed fire, and aimed reload assets where compatible with the current RAR skeleton/AnimInstance. Make the native aim value actually drive that animation path; align the camera with the intended sight without breaking hip-fire grip, muzzle direction, or third-person presentation.
- Pistol: retain Manny, the currently equipped pistol mesh, and the existing left-hand Control Rig/IK path. Add a dedicated first-person ADS pose/alignment so the pistol sight reaches the screen center without twisting the wrist or detaching the support hand. Do not replace its whole skeleton or rely on an unrelated third-person pose as a finished solution.
- Verification: local host and client, standing/moving/jumping, level and vertical aim, repeated press/release, fire and reload while aimed, weapon switching, death/reconnect/spectating, and near-wall sight/muzzle alignment. Record any asset-side limitation explicitly; passing a C++ build does not prove the rendered pose.

Implementation on 2026-10-12: the RAR native first-person graph now blends its absolute `Aim_Pose`, aimed idle/walk additive, mesh-space Aim In/Out transitions, and aimed fire/reload actions. Ordinary pistol/rifle ADS is enabled for the current tiers without overwriting nonempty Blueprint zoom levels; FOV changes smoothly. The first-person arms and attached weapon translate together to their `Barrel`/`SOCKET_Scope` sight reference while the camera and authoritative firing origin stay put. The reticle hides only when the sight has converged; the confirmed hit marker remains. Aim resets on relevant weapon/phase/death paths, and the RAR aimed reload duration participates in both server and owner-local fire lockouts. A follow-up removes the weapon's BeginPlay-time local Tick dependency: the owning character drives aim blending, clears a retired weapon's blend, and re-enables its own viewmodel Tick on client restart, so reconnecting into a retained pawn can resume ADS. The pistol retains Manny and its Control Rig but currently uses whole-arm/gun alignment, **not** a newly authored wrist/finger ADS animation; the latter and precise sight-angle/grip quality remain a rendered PIE calibration item. The final native version builds for Development Editor/Game and passes `25/25` tactical automation; no rendered ADS pass is claimed.

### 7.9 One Authoritative Gameplay Muzzle — Native Implementation Complete, Multiplayer Acceptance Pending

Use the standard two-stage shot query: the server traces from its validated camera/control viewpoint to choose an aim point, then computes a single stable gameplay muzzle transform from the authoritative pawn/weapon regardless of whether that pawn is locally controlled on a listen host. The gameplay ray or projectile travels from that muzzle toward the aim point, with a short near-wall obstruction check so a camera beside cover cannot shoot through it. Hits, spread, penetration, ammo, hit confirmation, ready-target interaction, and damage remain server-owned. Use one collision policy deliberately across the camera selection and muzzle obstruction stages; avoid gameplay results that change solely because the same player is host versus remote client.

Keep the view-specific `VisualMuzzle` separate: owner first-person and observers/teammate spectators may spawn flashes and cosmetic tracers at their locally visible weapon socket, while the hit result comes only from the gameplay muzzle. Do not force a first-person visual socket to become the network gameplay origin or draw a second predicted tracer. Test host/client equivalence, close walls and corners, vertical aim, ADS, moving/jumping, pistol/rifle, NPC targets, and warmup ready flags.

Implementation on 2026-10-12: player shots use the server viewpoint plus a configurable view-relative logical muzzle offset (default `35,10,-18` cm), independent of local FP/TP mesh visibility and bone refresh; NPC holder muzzle overrides remain. Camera target selection still occurs first. Hitscan and projectile launch share eye-to-muzzle and muzzle-to-offset WorldStatic/WorldDynamic obstruction checks, while the normal long-range shot trace still includes Pawn. A detected pre-muzzle block becomes the hitscan stopping contact even if penetration ammunition remains. A projectile additionally checks its real collision-sphere sweep/overlap against geometry and is not spawned when the path/volume is blocked; the shot still costs ammunition and plays firing feedback, but this first pass does not synthesize a projectile impact effect for that blocked case. View-specific tracer sockets are unchanged. Pure shot-path and RAR/weapon-configuration automation tests pass, but real UWorld near-wall collision, host/client equivalence, ready-target interaction, and visual parallax still require PIE; pure tests are not evidence of those rendered/network behaviors.

### 7.10 Small-Scope Owner-Local Fire Presentation Prediction — First Pass Implemented, Latency Acceptance Pending

Keep the project's C++ server-authoritative combat rather than migrating the entire character/weapon stack to GAS. On valid local trigger input, the owning client may immediately play first-person fire presentation (animation, an available muzzle flash/sound, and a visual-only weapon kick). The server alone validates cadence, ammo, reload lockout, team/phase rules, spread, hits, damage, and hit markers. Do **not** locally apply damage, confirm hit markers, or make a predicted camera/control-rotation recoil alter the view the server uses for the pending shot. Defer predicted ammo HUD unless measured latency warrants the extra reconciliation work.

Give each predicted presentation a weapon-generation/shot identity or equivalent owner-echo suppression so the returning multicast cannot play the same shot twice. Non-owners continue to see only server-authored fire effects, and the existing server-result tracer remains single and cosmetic. Clear or reconcile pending presentation on rejected fire, reload, switch, death, round freeze, reconnect, and weapon replacement; no old animation/effect may leak into the new state. Use latency/packet-loss emulation and host/client tests to compare shot rhythm and confirmed effects without claiming that cosmetics make hit registration client-authoritative.

Implementation on 2026-10-12: the autonomous owner immediately plays only its FP arms/gun fire montage on a local cadence timer. A `(weapon actor, trigger-burst ID, shot index)` key suppresses the corresponding late owner multicast; early authoritative echo is remembered so a later local timer does not double-play it. Start/Stop/Reload RPCs bind the client's expected weapon and the server validates identity before acting, preventing an old input from firing/reloading a newly selected weapon. A rejected start sends an owner-only burst cancellation; switch awaits the replicated weapon change with a bounded fallback, and phase/reload/death/switch/end-play cancel active prediction. Switch/select input is edge-triggered and repeated switch requests are ignored while one is pending; a zero-reserve reload outside infinite-reserve warmup does not create a false local firing lock. The server still owns ammo, refire, recoil to control rotation, spread, damage, hit confirmation, and the existing tracer. **Confirmed first-pass predicted content is the FP fire montage only**; separate muzzle-flash/audio/visual-only kick hooks were not present in C++ and have not been claimed as implemented. A rejected shot already visually started cannot be perfectly unplayed; reconciliation cancels subsequent predicted shots, and this residual cosmetic risk needs latency testing. No GAS migration or predicted ammo HUD was made.

## 8. Architecture And Ownership

The existing event-driven, server-authoritative gameplay structure remains appropriate. The first reconnect version now separates participant/slot state from the short-lived `PlayerController`; the preserved `PlayerState` and optional live pawn are transferred through a server-only proxy until a validated connection reclaims them.

Current first-version ownership:

- `UTacticalGameInstance`: scoped client network/travel-failure detection, sanitized cached remote address, private Match ID/credential/protocol cache, one bounded `0/1/2/4/8` retry loop, native reconnect overlay/input block, manual retry, and deliberate leave flow.
- `UTacticalMatchRosterSubsystem`: server-authoritative stable participant records, Match ID generation, opaque credential generation/rotation/validation, reserved team slots, connection-state transitions, reconnect deadlines, current-round participation, and last-settled-round metadata. It is reset when a new `ATacticalGameMode` initializes.
- `TacticalNetworkAdmission`: protocol version `1`, reconnect-claim shape validation, recovery-source/match isolation, and constant-time Match ID/credential comparison shared by server, client URL construction, and rules tests.
- `ATacticalGameMode`: `PreLogin`/`InitNewPlayer` admission, transactional preserved-`PlayerState` handoff, round admission policy, stable team-capacity decisions, 30-second public reconnect-status service, current-round pawn retention, same-pawn repossession, spectator fallback, explicit abandonment, round flow, elimination/economy/whole-team-disconnect terminal resolution, hunt-intel scheduling, and fixed-layout treasure selection plus its 75/70-second timeline.
- `ATacticalGameState`: replicated phase, time, score, round number, round result, match result, shared hunt-intel state, and one atomic late-join-safe treasure snapshot with synchronized server timestamps.
- `ATacticalPlayerState`: the replicated gameplay/economy state preserved across the proxy handoff: money, loss streak, team, armor/loadout persistence, ready/active flags, compatibility pending settlement, privacy-safe public connection status, and the absolute public-status-window end time. The legacy field name remains for compatibility; the participant registry is canonical for identity, reservation, and connection lifecycle.
- `ATacticalPlayerController`: server purchase requests, owner-only admission receipt, explicit leave request, possession handoff, per-team hunt-intel delivery, local UI including native combat feedback, treasure status/world marker, and the hold-Tab roster/status presentation and diagnostics, teammate camera selection, validated owner-only observer snapshots, reversible exposure of the observed pawn's weapon-specific first-person arms, and local hide/restore ownership for its real third-person Manny. The authority routes positive applied-damage confirmation to the shooter and transient damage-source direction to the owning victim through separate owner-only unreliable client RPCs; neither uses multicast or client-side hit inference. The victim converts the world direction through its current local camera yaw before selecting a directional mask. `UBuyMenuUI` intercepts Tab preview/key-up events while a purchase control owns UI focus and forwards them to the same roster visibility function, without Blueprint changes.
- `UShooterCombatFeedbackUI`: RAR sight texture, shooter-only server-confirmed hit-marker texture that replaces the sight during its pulse, eight camera-relative high-contrast semi-elliptical edge masks, and one omnidirectional fallback mask. The previous native hollow crosshair and line marker are missing-asset fallbacks. Cardinal damage sectors cover one entire edge; diagonal sectors join two entire edges, shift the arcs, and bias intensity toward the source corner. Each sector owns its own smooth fade-in/hold/fade-out pulse, so repeated hits do not flash and different directions can coexist. It suppresses the authored circular reticle visually while leaving its hierarchy available to the existing `PreRound` branch-isolation code.
- `ATacticalDisconnectedController`: transient non-replicating server proxy that temporarily owns the preserved `PlayerState` and holds the live pawn through its current-round lifetime, including after the public status window expires. It produces no player input or takeover AI and grants no protection.
- `ATacticalCharacter`: live pawn state, authoritative damage/death, disconnect action cancellation, death equipment loss, dropped weapons, health restoration, treasure interaction/firing-lock input state, Manny world body plus weapon-specific first-person presentation (RAR arms for rifle), per-view weapon mounts, and reconnect-safe first-person mesh/AnimBP restoration.
- `AShooterWeapon` / `ATacticalBulletTracer`: one existing unreliable multicast now carries server-selected bounded visual endpoints, shot IDs, and ordered impact results. The weapon chooses each viewer's visible muzzle, while a local-only non-colliding streak actor and independent additive/unlit material own the brief cosmetic travel; neither participates in authoritative hitscan damage or hit confirmation.
- `ATacticalCharacter` / `AShooterNPC` / `ATacticalBodyHitEffect`: after authoritative point-damage resolution, the victim classifies actual HP and armor loss, sends one unreliable impact cue, and each nearby non-victim view plays a short local bone-following card. It does not use the weapon's collision-only hit type as proof of damage and does not replicate effect actors.
- `UTacticalPlayerRetargetAnimInstance`: retained inactive Manny-to-SciFi implementation history. Current players do not instantiate its retarget/FABRIK/finger-correction path; it remains available only for a future replacement-character evaluation.
- `ATreasureChest`: strict two-guard activation, guard tracking, first-valid 2.0-second hold validation, cancellation, stable-roster exactly-once rewards, and per-round reset.
- `UTreasureStatusUI`: Blueprint-independent public treasure-state presentation, synchronized activation countdown and claim progress, claimant/team text, and one distance-labelled world marker with viewport-edge clamping and no actor search.
- `AShooterAIController`: existing guard leash and neutral combat behavior.

The implemented server participant record contains:

- A stable provider-qualified identity key derived from an engine-supplied valid, non-empty `FUniqueNetIdRepl`; an opaque Participant ID; and current/previous server-side credential state plus owner-acknowledgement state. Shipping requires the Steam provider and enables the Steam Auth packet handler before gameplay admission. Development-only PIE/`GUEST` identities remain available in permitted diagnostic builds; the roster subsystem separately owns the current Match ID.
- Reserved team/slot, connection state, current controller and `PlayerState`, retained pawn, server-only disconnect proxy, retained control rotation, public status deadline, entered round, and last settled round.
- Economy and canonical next-round armor/loadout remain on the preserved `ATacticalPlayerState`; the compatibility `PendingSettlementRound` remains while `EnteredRound`/`LastSettledRound` protect the new participant lifecycle.
- No reconnect secret is replicated through `GameState` or public `PlayerState`, and project-authored logs omit credential values. Packaged Steam reconnect reuses the same authenticated Steam identity and Session-resolved travel without appending the bearer to its URL. The non-Steam IP diagnostic path still uses URL options and may therefore expose that development-only claim in UE's internal `Browse` logging.

The minimum connection-state machine is:

`ConnectedPlaying -> DisconnectedGrace -> ReconnectedPlaying`

`DisconnectedGrace -> DisconnectedBodyRetained -> ReconnectedPlaying`

`DisconnectedGrace/DisconnectedBodyRetained -> DisconnectedWaiting -> ReconnectedSpectating -> EligibleNextRound -> ConnectedPlaying`

`ConnectedPlaying -> Abandoned` after the server receives an explicit online leave request. An already-offline client's local Leave button cannot immediately cause this server transition.

Every transition must be validated on the server. Duplicate login, repeated failure callbacks, offline pawn death, status expiry during a phase transition, and round settlement racing with reconnect must all be safe to execute more than once without duplicating a pawn, item, reward, or delegate binding.

Dynamic multicast delegate rule:

- Functions bound with `AddDynamic` must be `UFUNCTION` functions.
- Bindings should be removed before rebinding to avoid duplicate callbacks.
- Replicated serial counters or explicit `OnRep` callbacks should be used for one-shot result and pulse events.
- A replicated actor scheduled for removal must be detached from public inventory/current-weapon state before destruction. Cross-actor replication is not atomic, so retired weapons receive a short hidden grace period and clients normalize destroyed tracked pointers before notifying Blueprint listeners.

## 9. Implementation Plan

### Phase 1: Team, Round, And Match Rules

Tasks:

- Change maximum team size from 5 to 3.
- Reject or spectate players beyond the six-player active limit. Active-match spectators use a locked, teammate-only camera rather than UE's freely movable default spectator camera.
- Replace connection-order auto-assignment with server-authoritative player selection through the red/blue wall targets.
- Require every eligible lobby player to select a team and be ready; permit pre-match switching without exceeding 3 players per team.
- Set Buy, Combat, and RoundEnd durations to 10, 105, and 6 seconds.
- Remove active overtime and living-player-advantage resolution paths.
- Add explicit time-expired and simultaneous-elimination draw reasons.
- Replace the visible elimination delay with a 0.1-0.2 second server resolution window.
- Set the win target to 5 and maximum completed rounds to 9.
- Add match resolution by score after round 9, including a match draw.
- Preserve a normal score result already locked by the completed round; otherwise, end the match by unilateral whole-team disconnect forfeit at the `RoundEnd` continuation boundary instead of manufacturing empty-roster rounds. Replicate an explicit match-end reason, award the connected opponent the match without another round point/economy settlement, and treat two absent teams as a technical no-result abort rather than a normal draw.
- Use the stable participant registry and retained-pawn reconnect path as the primary lifecycle. Keep the older in-memory snapshot and `PendingSettlementRound` only as degraded compatibility fallbacks while migration and validation complete.
- Keep fresh join-in-progress players out of an active team and prevent them from being mistaken for reconnecting participants.
- Reject client restart requests during an active match so reconnect waiting cannot be bypassed.

Acceptance criteria:

- No round can be won by time, health, living-player advantage, money, or treasure.
- Near-simultaneous final deaths resolve deterministically.
- Round 9 always produces a team match win or a match draw.
- A qualified team with no connected member cannot trigger a Buy/instant-elimination/RoundEnd scoring loop.
- A unilateral full-team disconnect cannot evade a match loss, cannot create an extra round point, and is distinguishable from a two-team technical abort; disconnecting during result presentation also cannot reverse a normal score result already earned by the completed round.
- A seventh connected player cannot enter an active team.

### Phase 2: Economy And Equipment Closure

Tasks:

- Apply the new money values and equipment prices.
- Grant starting money only once per match.
- Add the 9000 money cap.
- Add explicit draw income without changing loss streaks.
- Validate all purchases against the Buy phase on the server.
- Synchronize DataTable prices and hard-coded fallback prices.
- Preserve survivor weapons and armor durability.
- Clear dead-player armor and non-default saved loadout state.
- Verify picked-up weapons persist only when the collector survives.

Acceptance criteria:

- The same item price appears in UI and server validation.
- No player can purchase outside the Buy phase.
- Money never exceeds 9000.
- Win, loss, and draw economy follow the documented rules.
- Survivor and dead-player equipment states are correct after respawn.

### Phase 3: Pure-PvP And Hunt-Intel Validation — Native Presentation Implemented, Manual Acceptance Pending

Tasks:

- Run the earlier pure-PvP validation baseline with treasure disabled; the mechanically complete Phase 5 rewrite is now enabled for its dedicated manual PIE acceptance.
- Implement the 30-second and 15-second hunt-intel thresholds.
- Deliver only living enemy positions to each team.
- Add approximate world markers and hunt-intel HUD warnings.
- Clear all markers on death, phase change, round end, and match end.
- Verify spectator information does not exceed teammate information.
- Verify the native spectator HUD and target first-person weapon for listen-host-to-client, client-to-host, and client-to-client observation; include live damage, armor absorption, firing, reload, weapon switching, target death, no-target state, and next-round HUD restoration.

Current implementation status:

- The authoritative 30/15-second scheduler, 8/4-second pulses, living-enemy filtering, and server-side position error are implemented.
- A native Blueprint-independent HUD now presents the warning state and up to three short-lived screen-projected approximate world markers. The old Blueprint events remain optional compatibility hooks.
- Owner RPCs now include authoritative round and per-round pulse sequence data. Clients reject duplicate, stale, wrong-round, and out-of-phase samples; a reliable death fence prevents an older anonymous pulse from restoring a dead target's marker.
- Hunt coordinates are delivered only to a living participant or to a controller with a server-validated living teammate spectator target. Phase/round/match transitions, death, target changes, possession restoration, and teardown clear sampled coordinates.
- The warning tier itself is public round-clock information; it contains no position data. Enemy coordinates remain private to the authorized team delivery path.
- UE 5.7 Development Editor compiles and links after this implementation. The requested manual multiplayer visual and balance acceptance remains pending and must not be inferred from compilation.

Acceptance criteria:

- Hunt intelligence never reveals teammates as enemies.
- Both teams receive equivalent pulse timing.
- No stale marker survives into another round.
- Timeout draws remain below the initial target during repeated tests.

### Phase 4: Disconnect And Reconnect IP Baseline — C++ Baseline Implemented

Implemented in the current source:

- `UTacticalGameInstance` owns one scoped reconnect state machine, a sanitized remote-address cache, a native full-screen overlay/input block, five automatic attempts at `0/1/2/4/8` seconds, post-travel gameplay-state confirmation, manual retry, and deliberate leave.
- Non-Shipping builds provide `TacticalPauseAutoReconnect`, `TacticalResumeAutoReconnect`, and the preferred `TacticalTestDisconnect` command through the exact local tactical controller, while retaining `tactical.Reconnect.AutoRetry 0|1` for compatibility. The atomic test command pauses automatic recovery for only that PIE client before disconnecting its `GameNetDriver`; manual Retry remains available, and the helpers do nothing in Shipping.
- `UTacticalMatchRosterSubsystem` owns stable participant IDs, reserved team slots, live connection/pawn/proxy references, public status deadlines, connection states, and entered/settled-round metadata independently of `PlayerController` lifetime.
- Match ID, opaque per-participant credential, protocol version `1`, complete-claim validation, duplicate-active handling, owner-only credential delivery, recovery-source/match isolation, and two-stage credential rotation/ACK are implemented. The Shipping path is coded to require an engine-supplied valid, non-empty `FUniqueNetIdRepl`; this is not yet Steam Session/authentication validation. Editor and non-Shipping Development retain clearly isolated PIE/`GUEST` test fallbacks, but their temporary Standalone fallback world cannot overwrite an active remote recovery admission.
- `ConnectedPlaying`, `DisconnectedGrace`, `DisconnectedBodyRetained`, `DisconnectedWaiting`, `ReconnectedPlaying`, `ReconnectedSpectating`, `EligibleNextRound`, and `Abandoned` server states are implemented. `ATacticalPlayerState` also exposes privacy-safe replicated public status and an absolute status deadline without private admission data.
- An unexpected eligible `Buy`/`Combat` disconnect retains the living pawn under a non-replicating server proxy until that round's authoritative cleanup. Held actions and velocity are stopped while normal visibility, collision, damage, position, health, armor, weapon, magazine, and reserve ammunition remain on that pawn.
- Reconnected clients no longer overwrite retained health or reserve ammunition with startup defaults. HUD binding catches both widgets created after possession and weapons whose replicated reference resolves later, so preserved health, magazine, and reserve values are visible without waiting for another damage/fire event. Initial health replication and HUD rebinding use a silent progress-bar update; only an actual health decrease starts the native gradient vignette. The legacy solid Blueprint damage animation remains a mutually exclusive fallback.
- Proxy allocation failure is fail-safe rather than death-equivalent: the roster retains the unpossessed authoritative pawn and its Pawn/PlayerState association, server alive counting reads the retained pawn directly, and a later real death can still identify the attacker and settle normally. If a replacement connection disappears before takeover completes and a second proxy allocation also fails, the exact participant generation rolls back to the same proxyless body instead of clearing equipment, releasing the slot, or refreshing victory as though that living body died. Allocation failure never creates an elimination by itself.
- The initial 30-second status deadline now changes only `Reconnecting` to `Disconnected`; it applies no damage, equipment loss, settlement, kill credit, score, or round transition. A validated reconnect may repossess the same living pawn at any point in the same Buy/Combat round, including after the status deadline.
- Offline pawn death, status expiry, explicit abandonment, round-boundary cleanup, and world teardown have separate authoritative paths. Only real damage/death eliminates a successfully retained body; status expiry does not inherit a stale enemy kill reward or create a death at all.
- `EnteredRound` and `LastSettledRound` now guard the participant lifecycle. The older snapshot and `PendingSettlementRound` remain only as compatibility/failure fallbacks, not the exact live-state recovery mechanism.
- Reconnect admission and takeover are bound to the exact participant generation. The transaction snapshots identity, phase/round decision inputs, controller, state, proxy, retained pawn, and bootstrap pawn; every re-entrant callback boundary revalidates that generation and the expected actor ownership before continuing, and `EnteredRound` is committed only after successful takeover.
- Round economy is settled idempotently before `RoundEnd` is published. Settlement candidates are copied as weak references, participant settlement markers are committed before extensible money callbacks, and a surviving player's equipment is not cleared merely because that player disconnects during `RoundEnd`.
- A native Blueprint-independent hold-Tab scoreboard presents the fixed 3v3 red/blue roster, score, round, local-player marker, life/participation state, public connection state, and authoritative status-window countdown. Mid-match disconnect and abandonment keep a roster row while their locked slot exists; lobby abandonment still releases its slot. Controller input covers gameplay phases, and the Buy-menu C++ base handles Tab before UMG navigation when a purchase control owns focus.
- `EMatchEndReason` and the replicated `LastMatchEndReason` separate normal score victory/draw, `TeamDisconnectForfeit`, `AllTeamsDisconnectedAbort`, and future `HostLost`. At the end of `RoundEnd`, a normal result locked by the completed round wins first. Only if another round would start does one wholly disconnected qualified team forfeit without another round point, or two absent teams abort with winner `255` and no-result semantics. A reconnected restricted spectator has a valid team controller and therefore permits normal next-round progression.
- A native C++ special-result banner labels disconnect forfeits and technical aborts without changing the existing Blueprint match-result signature. Technical no-result reasons suppress the legacy draw presentation, `MatchEndSerial` provides one-shot match-result catch-up, and `RoundEndSerial` supplies deduplicated round-result catch-up for clients that reconnect during `RoundEnd`. A connected inactive player with no pawn is presented as `WAITING`, not falsely as `DEAD`.

Mechanically complete; still required before accepting the current IP reconnect baseline:

- UE 5.7 Development Game, Shipping Game, and Development Editor all compile and link after the retained-body lifecycle plus the reconnect HUD/test-control corrections. This verifies target compatibility, not Steam identity authentication or the future packaged connection flow.
- The rebuilt Editor module passes all 11 `Tcamp.Tactical` tests with zero test warnings or failures. The latest report is `Saved/Automation/ReconnectFeedbackFinal2/index.json`, with the test log at `Saved/Logs/ReconnectFeedbackFinal2Tests.log`; pure tests cannot prove real transport, replicated initial HUD values, presentation feedback, or possession transfer, so the manual matrix remains required.
- A fresh uncooked NullRHI Listen Server smoke reaches authoritative PreRound, routes the controller-scoped pause helper, records the disabled setting on its GameInstance, creates/binds native UI, and tears down without fatal/assert/ensure/Blueprint runtime/`Accessed None`/pending-kill/tactical/network errors in the checked path. Its latest log is `Saved/Logs/ReconnectFeedbackFinal2Smoke.log`; the atomic disconnect helper also rejects listen-host use, and this smoke does not replace the real two-player disconnect matrix.
- Visually accept the native roster for host and clients: hold Tab during Combat and with a Buy-menu control focused; in the non-host window run `TacticalTestDisconnect`; confirm that no automatic attempt occurs, teammates retain its row and see a monotonic `Reconnecting` countdown, and the native Retry action can recover both before and after status expiry. Verify that the fallback admission is ignored, the reconnected HUD immediately shows pre-disconnect health/magazine/reserve values without damage feedback, valid recovery clears the status without `TACTICAL_RECONNECT_ADMISSION_REJECTED`, and mid-match explicit leave remains `Left` while its roster slot is locked.
- The earlier real-time uncooked dedicated-server/client IP-driver smoke proves guest join, owner admission ACK, and entry into the bounded PreRound grace after interruption. This remains transport-baseline evidence, not Steam integration evidence; the full manual PIE/network-emulation matrix below is still required.
- Validate repeated failure callbacks, reconnect during every phase boundary, offline death, expiry/death races, invalid/rotated credentials, duplicate active connections, full reserved rosters, explicit leave, MatchEnd, travel, world teardown, and listen-host shutdown without duplicate pawn/item/reward/settlement effects.

Acceptance criteria:

- A short disconnect while alive returns the admitted UID-bound participant to the same pawn with its then-current transform, health, armor, active weapon, magazine, and reserve ammunition.
- The retained pawn remains visible and damageable, so disconnecting cannot evade combat or preserve equipment unfairly.
- A pawn killed while its owner is offline runs death, drop, elimination, and settlement effects exactly once; reconnecting afterward enters teammate-only spectating when the match still has a connected member on that team.
- Status-window expiry leaves the living pawn and all live state unchanged and cannot create any death, weapon drop, economy payment, or round result, including when it races a real death or phase transition.
- In 1v1, a living offline body must be killed for a normal elimination win; if it survives to Combat timeout the current round is a draw. If that completed round did not already lock a normal match result, continued unilateral full-team absence at the end of `RoundEnd` produces one match forfeit without changing the settled score, while two absent teams produce one no-result abort. It never auto-awards later empty-team rounds. In larger teams, one disconnected member does not end the match while a teammate remains connected.
- A disconnected participant's team slot remains reserved and cannot be occupied by a fresh join or a player from the opposite team.
- Invalid identity/token, wrong match, incompatible protocol, duplicate connection, and an abandoned or unavailable participant never receive another player's state or pawn. The immediately previous credential is accepted only during the replacement's unacknowledged overlap; after owner ACK it is retired, and the retired value or any older value is rejected.
- PreRound reconnect clears ready state; Buy reconnect follows the documented cutoff; Combat never spawns a replacement pawn; RoundEnd returns at the following Buy boundary only when both qualified teams retain a connected controller.
- Other connected players see one stable roster row for a disconnected or mid-match abandoned participant. The reconnect countdown is derived from the server-authored absolute deadline rather than a client-local estimate, and expiry changes the row state without changing whether its retained pawn is alive.
- World teardown creates no abandon penalty, false elimination, or fabricated settlement.

### Phase 5: Fixed-Layout Treasure Rewrite — Core Accepted, Guard-AI Rewrite Awaiting PIE Retest

Implemented in C++:

- The macro map skeleton remains fixed. Exactly three C++ candidate sites exist, and authority selects at most one for each round.
- Both teams receive the same selected-site announcement at 75 seconds remaining in `Combat`. At 70 seconds remaining, the chest activates only if exactly two guards are created successfully; incomplete guard creation fails closed.
- The chest remains locked while either tracked guard lives. Guards remain team-neutral and grant no direct AI-kill money.
- Treasure guards no longer run the generic roam/sniper StateTree. A dedicated server-authoritative C++ brain is the sole owner of target selection, combat movement, firing, reloading, and return-home requests. It keeps a stable target through brief occlusion, notices nearby players around its full body rather than only inside the forward sight cone, retreats below 450 cm, leaves retreat after 650 cm, stops before firing with clear sight, and repositions when sight or range is unsuitable.
- Tactical movement is bounded around the shared chest anchor while each guard owns a separate navigation-projected home post. Reposition candidates closer than the safe band are rejected, paths that cross too close to the player are rejected, and a leash break exclusively returns that guard to its own post. Guard creation fails closed if posts leave their authored sides, overlap, fall off navigation, or a collision-safe spawn cannot be completed.
- AI decision checks run at 0.1 seconds, but expensive position planning and equivalent movement requests are independently limited to 1.25 seconds. Candidate points are projected and scored cheaply, then path validation examines a rotating best-first batch of at most six routes per planning pass and stops at the first usable route. This bounds each decision's navigation cost while allowing later passes to reach lower-ranked candidates when the leading group is blocked. Every active move retains its request ID, and only the matching return request plus physical arrival may complete return-home. This prevents the previous invalid/aborted `MoveTo` restart storm without permanently rejecting a lower-ranked reachable route.
- Guards use the weapon's single authoritative spread layer and perform real magazine/reserve reloads. Their authoritative reload lock lasts at least as long as the NPC-specific visible montage or sequence, so firing cannot resume while that animation is still playing.
- Claiming is a server-authoritative first-valid-claimant transaction. It requires 2.0 seconds of continuous interaction and locks claimant firing. Releasing interaction, leaving range, dying, losing eligibility, or leaving the valid phase/state cancels the claim; nonlethal damage alone does not reset progress. Repeated start requests from the accepted claimant are idempotent and cannot release the firing lock or restart the timer.
- A successful claim awards 500 money exactly once to each eligible same-team stable-roster participant who entered the round, including dead and temporarily offline non-abandoned members, with every balance clamped to 9000. The claimant additionally heals by at most 40 without exceeding maximum health.
- Treasure provides no armor repair, overshield, speed, damage, AI-kill money, or round point. Chest, guard, claim, timer, interaction-offer, and public snapshot state are reset at round boundaries and teardown.
- A replicated atomic snapshot drives `UTreasureStatusUI`. The native widget presents announcement, guarded, unlocked, claiming, and claimed states; uses synchronized server time for activation countdown and claim progress; identifies the claimant/team; and shows one distance-labelled, edge-clamped world marker without actor queries. Actor-local Blueprint unlock, claim-start/cancel, loot, and close presentation hooks are also mirrored to remote clients.
- Because the current treasure Blueprint contains no authored Static Mesh, a code-owned brown-and-gold rounded-box placeholder is used automatically. Its lid opens after the guards die. A later asset assigned to the existing `Chest Mesh` component disables the placeholder without changing gameplay code.

Mechanical verification:

- UE 5.7 Development Editor, Development Game, and Shipping Game all compile and link after the rewrite and native fallback-model addition. A project Blueprint compile pass excluding the known unrelated `/Game/Variant_Horror` assets completes with `0` compiler errors and `0` compiler warnings; its log is `Saved/Logs/ChestVisualFallbackBlueprintCompile.log`.
- After the dedicated guard-AI rewrite, UE 5.7 Development Editor, Development Game, and Shipping Game all compile and link. The complete `Tcamp.Tactical` automation group passes `15/15` with zero test warnings and zero failures. The final post-review report is `Saved/Automation/TreasureGuardAIRewriteFinal3/index.json`, and the log is `Saved/Logs/TreasureGuardAIRewriteFinal3Tests.log`. Three dedicated rule tests cover combat-distance hysteresis, independent planning/request throttles, destination drift, retreat geometry, movement bounds, and return-request ownership.
- Multiplayer PIE has confirmed chest and guard activation, killing both guards, completing a claim, and seeing the native fallback chest model. The requested functional and visual acceptance is complete; contention, every cancellation/reward edge, marker readability, and balance remain part of the later full multiplayer matrix.
- The rewritten guard behaviour itself still requires a fresh multiplayer PIE retest: close-range retreat without running into the player, stop-and-fire behaviour, obstacle repositioning, reload gating, independent return posts, and steady frame pacing must be judged in the real map rather than inferred from pure tests.

Deferred increment:

- The three server-selected, manually authored local-layout presets are not implemented. They remain optional until the fixed-layout treasure loop passes manual PIE acceptance.

Acceptance criteria:

- Both teams receive the same site announcement and activation timing, and only one of the three valid sites activates.
- The chest cannot be claimed before both guards die.
- Only one player can complete a claim.
- Invalid, dead, or out-of-range players cannot claim.
- Every eligible teammate receives the team reward exactly once.
- The chest has no direct effect on round scoring.
- If local-layout variation is enabled, all clients present the same server-selected preset; every preset preserves valid starts, routes, collision, guard navigation, and treasure access without runtime procedural competitive geometry.

### Phase 6: Steam Listen Server Integration — Core Implementation Complete, Packaged Acceptance Pending

The selected Steam player-hosted path is now implemented in C++. Editor and PIE continue using OSS Null/IpNetDriver intentionally because one editor process cannot represent multiple independent Steam accounts. The remaining gate is a packaged two-account/two-PC runtime acceptance pass, not more Blueprint wiring.

Implemented:

- `OnlineSubsystemSteam` and `SteamSockets` are enabled in the descriptor, module dependencies, and UE 5.7 configuration. Packaged builds select Steam; editor worlds deliberately request OSS Null, and SteamSockets falls back to IpNetDriver for PIE diagnostics.
- `UTacticalSessionSubsystem` exclusively owns Create, Find, Join, Invite Accepted, Leave/Destroy, match-state publication, async operation state, generation fencing, delegate cleanup, and resolved travel addresses.
- A new presence/lobby Session begins as private `INITIALIZING`; the gameplay Listen Server publishes discoverable `LOBBY` only after its world is ready. It carries at most six public connections plus `TCAMP_FPS` product, engine build, protocol version, server name, and match state. Search and UI accept only compatible, non-full `LOBBY` results and fail closed on absent or unknown state.
- Match start serially publishes `IN_PROGRESS`, closes advertising/presence/invites, and then starts the provider Session. The provider-level reconnect opening is retained only for a known inactive roster identity; public discovery remains closed and authoritative `PreLogin`/roster admission rejects fresh, excess, or duplicate-active identities. Match end remains closed through result display and restart travel; the replacement gameplay world ends the old provider Session and republishes `LOBBY` when ready.
- Create/Find/Join use 25-second watchdogs, Destroy uses a 12-second watchdog, individual match-state submissions use a 5-second watchdog, and host/session cleanup has a final GameInstance fallback. Provider callbacks are generation-fenced and cleared through the exact interface that registered them; state updates have finite retries plus bounded `Starting`/`Ending` convergence polling.
- The packaged host opens the gameplay map as an authoritative Listen Server. Join and Steam reconnect travel through `GetResolvedConnectString`, with no manual IP UI and no bearer credential appended to the Steam travel URL.
- The native C++ frontend contains Host, Refresh/Find, selectable room rows, Join, status/error states, and Steam identity display. Escape opens an in-match menu with Resume, Invite, and Leave/End Match. Input ownership is paired across open/close and travel.
- Provider-qualified identity keys prevent cross-provider collisions. Shipping accepts only Steam identity at gameplay admission and enables the Steam Auth packet handler; development PIE/Null identities remain isolated test fallbacks.
- Client network loss first re-resolves/rejoins the active Steam Session and then enters the existing same-host retained-pawn admission flow. Raw address/token reconnect remains only a non-Steam diagnostic compatibility path.
- Host-initiated shutdown first raises the local admission fence and cancels a pending round-restart timer, publishes fail-closed `CLOSING`, then sends `HostLost`/no-result, freezes gameplay, performs bounded named-Session cleanup, and returns to the frontend. Network/travel/listen-start failure has the same bounded cleanup and cannot be counted as a round win, draw, or team-disconnect forfeit.
- The frontend uses `/Engine/Maps/Entry` with a lightweight native menu GameMode; the gameplay map remains explicitly included in packaging. App ID `480` remains development-only and the `TCAMP_FPS` product filter prevents unrelated Spacewar sessions from appearing.

Mechanical verification completed on 2026-09-10:

- UE 5.7 Development Editor, Development Game, and Shipping Game compile and link.
- The complete `Tcamp.Tactical` automation group passes `12/12` with exit code `0`.
- The final uncooked `-nosteam` Listen Server smoke (`Saved/Logs/SteamPhaseFinalListenSmoke.log`) loads `BP_TacticalGameMode_C`, falls back from disabled SteamSockets to IpNetDriver, listens on port `7777`, admits the local player, and reaches authoritative `PreRound` without a fatal error, assertion, or Blueprint runtime error.
- The final uncooked frontend smoke (`Saved/Logs/SteamPhaseFinalFrontendSmoke2.log`) loads `/Engine/Maps/Entry` with `TacticalMenuGameMode` without the previous null-HUD spawn warning. These local checks do not prove Steam overlay, lobby discovery, relay traversal, authentication-ticket behavior, or two-PC connectivity.

Acceptance criteria:

- Two Steam accounts on two packaged PCs can host, discover or accept an invite, join, play, leave, and return to the multiplayer menu.
- The Steam path works on one LAN and across different networks through the selected Steam networking/NAT-relay path without asking players to enter an IP address or configure port forwarding.
- The host remains the UE gameplay authority, and team selection, the 3v3 cap, phase flow, damage, economy, and reconnect admission cannot be client-forged.
- Full lobbies, incompatible builds, rejected join-in-progress, invite/join failure, and session destruction all produce bounded cleanup and clear UI states.
- A transient client interruption can recover only while the same listen host and authoritative world survive.
- Host exit, crash, or network loss ends the match as `HostLost`/no result, destroys or leaves the Steam Lobby/Session as possible, and returns clients with a clear host-loss result. It is not a score draw or a unilateral disconnect forfeit. Host migration is not implemented.

### Phase 7: ADS, Ballistic Origin, And Fire Responsiveness — Native First Pass Implemented, Rendered/Network Acceptance Pending

Implement Section 7.8, then 7.9, then 7.10. Keep each patch narrow and compile/test before considering its rendered behavior accepted. First audit native and imported animation assets; implement rifle ADS with the existing RAR animation path and pistol ADS without undoing the current Manny/Control Rig grip. Next unify the server gameplay muzzle and near-wall behavior while keeping owner/observer visual muzzles. Finally add owner-local fire presentation prediction and explicit echo suppression; do not migrate to GAS or predict damage/hit confirmation.

Acceptance gates: Development Editor/Game builds; focused automated tests for host/client muzzle invariance, camera-to-muzzle obstruction, valid/blocked shots and warmup ready-target behavior, plus fire-presentation deduplication/state reset; two-window rendered PIE for both weapons, ADS pose and FOV, fire cadence, host/client visual-vs-gameplay alignment, near-cover shots, reload/switch/death/reconnect; and latency/packet-loss PIE for duplicate/stale owner effects. Recheck six-player frame pacing before declaring the work complete. If an imported montage or pose cannot be safely wired without modifying its asset, document the exact unfinished asset step instead of reporting a false visual pass.

Mechanical gate as of 2026-10-12: the final native version builds for Development Editor and Development Game. After review fixes for repeated switch input, zero-reserve false local reload lock, and owner/reconnect ADS Tick dependency, `Tcamp.Tactical` again returned `25/25` successful tests in `Saved/Logs/ADSBallisticPredictionAutomationPostReview20261012.log`, including the pure ballistic path test and RAR ADS asset checks. A NullRHI tactical-map startup reached world BeginPlay in `Saved/Logs/ADSBallisticPredictionMapSmoke20261012.log` before those final review fixes. A final Windows map cook succeeded with `0` errors and `2` warnings in `Saved/Logs/ADSBallisticPredictionCookPostReview20261012.log`; the newly referenced RAR Aim Pose, aimed idle/walk, Aim In/Out, aimed fire and aimed reload assets are present in the cooked output. The warnings are an existing Paragon retarget `Rig` load warning and the test command's intentional `-NoSteam` flag. These checks do **not** cover actual two-window host/client RPC timing, near-cover projectile geometry, rendered sight/hand quality, six-player frame pacing, or Steam session/package testing; those acceptance gates remain open. The pistol's dedicated finger/wrist ADS pose and separate predicted sound/flash/kick are unfinished visual polish rather than verified features.

ADS sight-picture first pass on 2026-10-12: pistol `Barrel` → `Muzzle` and rifle `SOCKET_Scope` → `Muzzle` provide two-point virtual sight lines. Each line is raised just above the relevant gun top/front-sight tip; the RAR rear and front offsets differ by 7.3 cm to compensate for different reference-bone heights. The owning arms/gun receive bounded translation and rotation toward the camera ray, while server ballistics and third-person presentation remain unchanged. World zoom no longer changes the first-person weapon projection. The native RAR Aim In graph crossfades to its steady pose throughout the clip instead of snapping during the last 0.06 seconds. Development Editor/Game builds and `25/25` tactical tests passed (`Saved/Logs/ADSVisualAlignmentAutomation20261012.log`), and a NullRHI game-map startup succeeded (`Saved/Logs/ADSVisualAlignmentMapSmoke20261012.log`). This first pass is superseded by the overlap correction below; its mechanical tests never established rendered sight quality.

ADS transition/overlap correction on 2026-10-12: release preserves the decaying aim alpha and weapon profile; the same alpha drives FOV, native RAR base pose, and owner root. The unmatched Aim Out clip is bypassed; interrupted Aim In briefly fades and rifle aim-out is slower. Endpoint-authored rifle arms fire montages are skipped while ADS is transitioning, with the gun-body fire montage retained; `Fire_Aimed` is used only after full alpha and actual sight alignment. A non-recoil rifle sight line is cached in gun-component space so its reference follows the animated hand socket during an overlapping shot without following muzzle/recoil bones. The pistol copies the body pose and uses a Control Rig for right-hand direction; C++ now applies only ADS root translation, while the Rig primary and secondary aim axes have been calibrated to the registered gun barrel/up directions. The original Rig asset is backed up under `Saved/CodexADSBackup20261012`. Development Editor/Game builds, `25/25` tactical tests (`Saved/Logs/ADSOverlapFinalAutomation20261012.log`), and NullRHI map startup (`Saved/Logs/ADSOverlapFinalMapSmoke20261012.log`) passed. A fresh rendered standalone probe reduced settled pistol barrel-to-camera error from about 11.4° to 0.7° (`Saved/Logs/ProbeADSRuntime_20261012_calibrated.log`); a simultaneous rifle fire/aim probe converged near center (`Saved/Logs/ProbeRifleADSFireRuntime_20261012_fixed.log`). Two-window rendered PIE remains the acceptance gate for networked shot presentation, repeated ADS reversals, full pistol movement/reload poses and other visual edge cases; the rifle rear-sight accessory is not mounted, and separate RAR Aim Out remains bypassed pending pose matching.

ADS acceptance correction on 2026-10-12: test both input orders, particularly sustained hip fire followed by ADS. The implementation fades the pre-existing RAR hip-fire arms montage on aim entry rather than merely suppressing new arms montages; gun firing presentation continues. A rendered fire→aim probe reduced left drift from 6.44 cm to under 1 cm. The pistol retains its level view pivot and Manny/Control Rig. The Rig's spring output was bypassed for immediate right-hand aiming, and its AnimBP visual Aim Target is always the camera's far forward point rather than a close trace hit; authoritative ballistics are not changed. The first pistol pitch probe covered only level ADS followed by turning, so its yaw-only root target and pitch compensation were not accepted as an input-order solution; the later correction below supersedes them. Development Editor/Game builds and `25/25` tactical tests passed (`Saved/Logs/ADSFollowupAutomation20261012.log`). Two-window PIE and visual hand/skin polish remain acceptance steps.

Pistol ADS angle/order correction on 2026-10-12: the owner viewmodel now refreshes the pistol's arms-local sight line every non-montage frame and solves its root against the current camera-forward point, with no ADS-entry pitch latch or `-17.5*sin(pitch)` offset. Rendered standalone probes for both orders at ±35°, ±60°, and the reachable extremes place the rear sight within about 0.02 cm of the camera target and keep the barrel within about 0.11° (`Saved/Logs/PostPatchPistol_*.log`). Continuous pitch without shooting shows about 1.4–1.8 cm temporary lag and settles promptly. During a fire montage, the root deliberately remains frozen rather than chasing recoil; rapid pitch changes during that montage can temporarily displace the sight by about 11–14 cm. Follow-up work should separate the clean Control Rig pitch pose from montage recoil before replacing this freeze. Development Editor/Game builds and `25/25` tactical automation tests passed (`Saved/Logs/PistolADSOrderFixAutomation_20261012.log`); two-window PIE remains a manual acceptance gate.

### Phase 8: Integration And Multiplayer Verification

Test configurations:

1. 1v1 without treasure.
2. 2v2 without treasure.
3. 3v3 without treasure.
4. 3v3 with treasure enabled.
5. Six active players plus a seventh connection attempt.
6. Unexpected disconnect shorter than the status window while the pawn survives.
7. Pawn death while its owner is offline and reconnect before/after that death.
8. Status-window expiry with a still-living body, deliberate leave, repeated disconnect, and reconnect at every phase boundary.
9. Unilateral whole-team disconnect followed by enemy elimination, Combat timeout, recovery before the `RoundEnd` continuation boundary, and persistent absence through that boundary.
10. Both qualified teams fully disconnected, plus listen-host loss as a separate terminal case.
11. Valid identity, invalid or previously rotated credential, wrong Match ID, incompatible protocol, duplicate connection, and fresh join-in-progress.
12. Reserved 3v3 slot, full roster, next-round admission, MatchEnd, and map travel.
13. Steam Lobby destruction, listen-host exit/crash/network loss, client host-loss UI, and session cleanup on two packaged PCs.
14. Two-window and full-3v3 player presentation: Manny world body and pistol path, RAR rifle first-person arms/gun and matching world gun/parts, their respective mounts, muzzle direction, fire/reload, remote vertical aim, death/ragdoll, spectator third-person-body suppression/restore, respawn, and retained-pawn reconnect. Verify HUD/roster/spectator team identity separately while model tint is suspended.
15. Cosmetic tracer shots in owner, remote, and first-person spectator views: hit, wall, miss, penetration, warmup zero-damage, repeated automatic fire, vertical aim, weapon replacement, death/reconnect, and lost unreliable cosmetic events. Compare six-player frame, game/render/GPU, and local tracer-actor time with `tc.Tracer.Enabled 1` and `0`.

Metrics to record:

- Timeout-draw rate.
- Simultaneous-elimination-draw rate.
- Median time to first player engagement.
- Average and longest round duration.
- Team-side win rate.
- First-kill conversion into a round win.
- Treasure claim and contest rates.
- Treasure claim conversion into a round win.
- Average team money difference after each round.
- Purchase rate and win rate by weapon tier.
- Reconnect attempt success rate and median recovery time.
- Public-status-window expiry, deliberate-abandon, and Steam create/find/join/invite/authentication failure rates.

Initial acceptance targets:

- Timeout draws below 10%.
- Median first engagement below 30 seconds.
- Average round duration between 50 and 80 seconds.
- Team-side win rate between 45% and 55% over a meaningful sample.
- Treasure claimed in approximately 25% to 50% of enabled rounds.
- Average team economy difference after three rounds stays below the price of one rifle.
- No active-player, score, economy, equipment, or UI desynchronization during 3v3 network tests.
- No reconnect scenario duplicates a pawn, possession, item, reward, round settlement, or delegate binding.
- The active first-person Manny pistol/RAR rifle and third-person Manny poses remain correct during locomotion, aiming, firing, reloading, death, spectating, and reconnect; hands, weapon mounts, and muzzle direction remain credible from both the owner and another player's view.
- Cosmetic tracers originate from the visible muzzle, follow the server's shot result with a bounded miss endpoint, never modify hit/damage rules, and add no unacceptable hitch or persistent effect in the six-player profile.
- Team identity remains correct in the HUD, hold-Tab roster, spectator panel, damage rules, team switching, respawn, late replication, and reconnect while model tint is suspended. Immediate red/blue model readability returns as an explicit later art requirement rather than a current acceptance claim.
- Six directly rendered Manny players maintain acceptable frame pacing in the target hardware profile; measure `stat unit` and `stat anim` before treating a visual hitch as network or input failure.
- No unilateral full-team disconnect can avoid a match loss; when no normal score result is already locked, it produces `TeamDisconnectForfeit` without a fabricated round point. A completed round's legitimate score result cannot be reversed by disconnecting during its presentation. Two-team absence and host loss produce no-result abort reasons and are never counted as draws.
- Network emulation and forced-disconnect tests cover packet loss, latency, a same-process interrupted client, a stale connection, and a connection returning while the round changes. Automatic match continuity after terminating/restarting a client process, and all recovery after listen-host loss, are outside the selected Steam Listen Server delivery scope; a later design may allow a restarted client to join an existing surviving lobby as a new connection.

## 10. Immediate Execution Priority

Recommended order:

1. Render-check Section 7.8 in two-window PIE: rifle/pistol sight angle and hand/wrist contact, smooth FOV/arms, aim-in/out, aimed fire/reload, movement and vertical aim. Calibrate the pistol's dedicated finger/wrist pose if whole-rig alignment is insufficient.
2. Network-check Section 7.9's stable gameplay muzzle and two-stage aim with close walls/corners, thin props, a projectile, both player roles, NPCs, and warmup ready flags; compare the locally visible tracer against authoritative hit results.
3. Latency-check Section 7.10's owner-only montage prediction and multicast deduplication across rejected shots, reload, rapid switching, death/reconnect and auto fire; then decide from measured feel whether separate predicted sound/flash/visual kick is needed. Do not predict damage or hit markers.
4. Complete the focused two-window player-presentation PIE pass: Manny pistol, RAR first-person rifle and Manny world rifle, plus unarmed; idle, WASD, strafe, jump and ADS; fire, reload lockout and switch; right-hand/socket lock, left-hand contact and forward muzzle direction; remote pitch; death/ragdoll; spectator entry/exit with the real third-person Manny hidden/restored; respawn; and same-pawn reconnect with the correct first-person mesh/AnimBP and weapon restored. Confirm the HUD, roster, and spectator panel still identify both teams even though model tint is temporarily disabled.
5. Visually accept Sections 7.6–7.7 in owner/remote/2v1 first-person spectator PIE: tracer width, color, near-wall parallax, miss/penetration termination, warmup zero-damage, ADS/vertical aim and cut-off on weapon retirement; plus body impact for HP, armor-only/mixed, headshots, NPC hits, ragdoll and reconnect. Confirm no duplicate owner tracer, false hit marker, or replacement of the victim's directional HUD effect.
6. Repeat the player-presentation, tracer, body-impact, ADS, and predicted-fire pass at the six-player target while recording frame, game, render, GPU, animation, and cosmetic-actor time with `tc.Tracer.Enabled 1` versus `0` and `tc.BodyHitFx.Enabled 1` versus `0`. Consider pooling or Niagara only if the measured delta merits it; the SciFi/retarget path remains inactive.
7. Create a Windows Development package with App ID `480`, then launch each copy from a different Steam account on two physical PCs; do not use two PIE windows as Steam proof.
8. Validate Host -> Find/Join and Host -> Steam Invite/Accept on the LAN. Confirm the room reports at most six players, both peers reach unassigned `PreRound`, each chooses a team, and normal phase/gameplay authority remains unchanged.
9. Repeat across different networks without entering an IP address or forwarding a port. Confirm SteamSockets/relay is the active transport in both logs.
10. Validate full-room, incompatible-build/product/protocol, fresh join after match start, failed/cancelled join, voluntary client leave, host End Match, host process exit, and host network loss. Every path must return to the frontend with the documented result and no stale named Session.
11. Interrupt only the non-host client while the host/world survives; confirm Session-resolved reconnect restores the retained body or the correct teammate spectator state. Host loss must remain terminal and must never start that recovery loop.
12. Before any external distribution, replace development App ID `480` with the project's assigned Steam App ID and define `UE_PROJECT_STEAMSHIPPINGID=<assigned id>` in `Source/Tcamp_FPS.Target.cs` (or the equivalent release build configuration). Then repeat the packaged acceptance matrix against the real Steamworks application configuration; a successful Shipping compile is not runtime Steam proof.

## 11. Out Of Scope For This Pass

- Full map redesign or construction of a new three-lane map.
- Arbitrary procedural assembly, runtime-PCG modification of competitive geometry, or per-round movement of the map's macro skeleton.
- A bespoke multi-pack art-production pipeline; the selected solo scope assumes reuse of one coherent purchased modular environment kit.
- Capture points or any alternative round win condition.
- Overtime or post-timer combat extension.
- 5v5 support.
- New weapon tiers.
- Ranked matchmaking, party systems, skill rating, and advanced automatic matchmaking beyond the basic Steam Host/Find/Join/Invite/Leave flow.
- Rented or always-on dedicated-server packaging and deployment.
- Listen-server host migration.
- Recovery or match continuation after the listen host, its authoritative world, or its process is lost.
- Automatic match continuity after a client process restart; this requires a separate persistence and admission design if added later.
- Cross-map authoritative match checkpoints or backend-persisted match recovery.
- Seamless travel of an active match or its disconnected/abandoned `PlayerState` tombstones; the current project uses hard-travel world replacement. Any later seamless-travel design must explicitly rebuild or exclude these records against the destination roster registry.
- Mid-match substitute players taking an absent participant's reserved competitive roster slot.
