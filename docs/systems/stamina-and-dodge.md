# Stamina and dodge

Read this document before a change of the stamina of a player (its payments, its gains, its regeneration, the empty stamina), of
the dodge (its input, its checks, its payment, its immune window), or of a passive rule that uses them, and before a live test of
a dodge. The wire contract of the Stamina state and the client code are in the protocol notes (section 4.38). The creature hit that
a dodge avoids is in [Combat and health](combat-and-health.md) and [Projectiles](projectiles.md), the basic attack sequence that
decides a dodge in an attack in [Combat and health](combat-and-health.md), and the passive rules in [Passive rules](passive-rules.md).

## Start here

| Task | Start here |
| --- | --- |
| The stamina path, the regeneration, the empty stamina, the dodge, the immune window | `world/stamina.h` (`world::Stamina`, `DodgeVerdict`, `StaminaCounts`), test cases SM2, CRG11 |
| The values of the game data | `tools/gamedata/stamina.sh` (`stamina.py`, `check_stamina_tool.py`), `gamedata/stamina_data.h` (`StaminaData`, `StaminaTables`), [Game data](../development/game-data.md), test case SM1 |
| The Stamina state of the owner, the key StaminaRate of the speed table | `server/rep/states/stamina_state.cpp`, `server/rep/states/stat_multiplier_state.cpp`, protocol notes 4.38, test cases SM2, SM3 |
| The statuses with the cells Stamina and StaminaRate, a payment of a fragment | `Statuses::give`, `Gear::refresh`, `Stamina::pay_cost`, test case SM3 |
| The passive rules of the stamina and of the dodge | `Passives::hit_evaded`, `Passives::hit_taken`, [Passive rules](passive-rules.md), test case SM4, `tools/world passives --table stamina` |
| A creature hit in a dodge, a shot through a dodge, no immunity for a different character | `Health::creature_hit` (`CreatureHitOutcome`), `Projectiles` (`CreatureFlight::passed`), test cases CRG11, CRG12 |
| The corrections of the code review of stage 3.4f-B: the window of an earlier dodge (CS-R1), a refused dodge in an attack (CS-R2), a next dodge before the payment (CS-R5) | `Stamina::keep_window`, `Stamina::immune_since`, `Stamina::can_dodge`, `AttackBits::dodgeAllowed`, `gamedata/stamina_data.cpp`, `tools/gamedata/stamina.py`, test cases RZ1, RZ2, RZ5 |
| The first record of the owner: the Stamina state 100 of 100 (stage 3.4f-C) | `server/rep/states/stamina_state.cpp`, protocol notes 4.38, test cases SM6, T1, T2 |
| The development list | `tools/world stamina`, the lines `stamina` of `tools/world list` |

## Rules

- The stamina of a character is a value of the character in the world (`Entity::vitals`: `stamina`, `staminaMax`,
  `staminaRegenFrom`, `staminaHeldUntil`, `windedUntil`, `exhausted`; rule MP-1). Only the stamina system changes it, through one
  path (`Stamina::change`): the payment of a dodge at its mark, a payment of a fragment of a basic attack (`Stamina::pay_cost`), the
  gain of a status (the cell Stamina), the gain of a hit rule (the cell Stamina of a passive rule: the runeglass gems), and the
  regeneration of the tick. A creature has no stamina. A dead character gets no change.
- The values come from the game data (`stamina-data.json`): the maximum is `m_initMax` of the Stamina component of the player slice
  (100), the regeneration is `m_regenRate` (33.33 in one second) times the factor of the load class (`EncumbranceData`, row Player:
  1 for each class) times 1 + the StaminaRate of the statuses. The client predicts the same values (live checks LS5 to LS7).
- A payment stops at 0: no debt (live check LS6). It sets the delay of the regeneration: `StaminaRegenDelay` of the base vitals (1 s),
  or `StaminaRegenDelayOnEmpty` at 0 (1 s). A payment of cost 0 changes nothing, also not the delay (live check LS7: a light attack
  of the great axe does not stop the regeneration).
- The empty stamina. At 0 the character is winded for `m_maxWindedTime` (2 s) and exhausted until its stamina is full again (the
  action Exhausted of the global action list: it starts at 0 percent and ends at 100 percent). The regeneration starts after its
  delay also while the character is winded (live check LS6). An exhausted character gets no dodge.
- A gain stops at the maximum. The full stamina ends the regeneration and the exhausted state. A revival and a respawn give the full
  stamina (`Stamina::refilled`); a world entry starts full (`Stamina::entered`).
- The dodge input. The client sends the dodge bit (322) in an input step for each press, also for a press that it refuses itself
  (live check LS9). Thus the bit is a request: the server decides the dodge with its own stamina. At rest (no fragment of an attack)
  each step with the bit is a request; in an attack the basic attack sequence decides it (a dodge in a CanDodgeCancel window of the
  fragment, `AttackSequences::take_dodge`). The time of the dodge is the world time of the input step (a rule of this project: no
  rewind of the time; research task D).
- The checks of a dodge (`Stamina::dodge`), in this sequence: the stamina data is there (`noData`), the character lives (`dead`),
  its input is not locked (`locked`: a travel that the client did not confirm), it is not overburdened (`overburdened`: the action
  Dodge of the client has no fragment for that class), the bag is not over its weight limit (`encumbered`, stage U3: the client shows
  "Encumbered - Cannot run, dodge, or jump" and plays no roll, but it sends the press; before the stage the server started the dodge
  and took 45 stamina; [Items and equipment](items-and-equipment.md)), it is not exhausted (`exhausted`), its stamina is above 0 (`noStamina`: the
  condition IfStaminaValue above 0 of the action Dodge; live check LS6 accepts a dodge at 5). A press while a dodge plays: in a
  buffered window CanDodgeCancel of the fragment (0.267 s to 0.5 s) the next dodge starts at the end of the window (`buffered`; one
  for each window); in an instant window (0.5 s and later) at once; before a window and before the mark CanTransition: `dodging` (no
  second dodge: a duplicate of the input).
- An accepted dodge. The fragment is the dodge class of the load class (light: Dodge_Roll_Fast, medium: Dodge_Roll, heavy:
  Dodge_Roll_Slow). The cost of the class (45, 45, 50) times the cost factor of the load class is paid at the mark PayStaminaCost
  (0.1 s) through the stamina path. The dodge stops the regeneration from its start to its mark CanTransition (0.5 s); there the
  delay of its exit starts (SetStaminaRegenDelay of the action: 1 s; live check LS5). A death, a travel, or a leave before the mark
  of the payment ends the dodge with no payment (`paymentsCancelled`).
- The immune window: from the start of the fragment to the end of its Invulnerability clip (0 s to 0.5 s), the end not included
  (`Stamina::immune`: 499 ms immune, 500 ms not). A creature hit at a time in the window (`Health::creature_hit` with the time of the
  contact) does no damage, no mark, and no effect (`CreatureHitOutcome::evaded`); the passive rules with OnHitTakenWhileInvulnerable
  get it (`Passives::hit_evaded`). The immunity is of the character of the dodge only (test case CRG12). From stage B2 the trackers of
  the 12 elemental weapon perks act on the next hit (Shirking Shock II of the bow build: [Passive rules](passive-rules.md); live check
  B2-3: an arcane arrow of an elite archer at 0.31 s of a dodge gave the tracker, and the next arrow had 713.6 Lightning).
- A dodge in a bow action (stage B2; code review of stage U3, finding U3-R7): an accepted dodge ends the action of a bow ability at its
  cancel mark, and its arrows after the cancel do not fly ([Abilities](abilities.md)).
- The window of an earlier dodge (code review of stage 3.4f-B, finding CS-R1; test case RZ1): a dodge that a next dodge replaces, or
  that ends early (a death, a travel), keeps its immune window for `kPastWindowKeep` (1 s after its end; at most `kPastWindows`, 8,
  for each character; `Stamina::keep_window`). A contact that the world tests later, at a time in such a window, is evaded
  (`Stamina::immune_since`): the flight of a shot that the world tests after the start of the buffered next dodge. A rule of this
  project; a damage that an earlier tick applied stays.
- A refused dodge in an attack (finding CS-R2; test case RZ2): the combat system asks the stamina system first
  (`Stamina::can_dodge`: the data, the life, the lock, the load class, the exhausted state, the stamina above 0). Only a dodge that
  it can accept ends the visit of the attack sequence (`AttackBits::dodgeAllowed`). A refused dodge keeps the attack that plays, its
  strikes, and its events (the second spin of Whirlwind): the client keeps it too (live check LS9).
- A next dodge before the payment (finding CS-R5; test case RZ5, the case `replace-before-pay` of `check_stamina_tool.py`): the
  loader and the tool refuse a dodge fragment whose next dodge starts before its mark PayStaminaCost (the end of a buffered window,
  the start of an instant window). Else a chain of next dodges never pays (a fault of the data; the retail windows start at 0.267 s
  and 0.5 s, after the payment at 0.1 s).
- A creature shot through a dodge flies on: the profiles of the basic attacks of the creatures have
  `m_destroyProjectileOnUnsuccesfulHit` false (research task D). The flight keeps the characters that it passed
  (`CreatureFlight::passed`): it never hits them again, and it can hit a later character or a wall (live proof P2, P3: the shot
  stops at a wall behind the character).
- The Stamina state (slot 42, type 4297) goes only to the owner (one encoder, `encode_stamina_state`). The client of the owner does
  not take its value at once (live checks LS2, LS3): the state is the record of the server copy of the component. A rewind of the
  movement prediction of the client copies the server copy into the predicted values (client code 0x68AC2B0, 0x6917C70, from the
  handler of a server movement state 0x486D660).
- The first record of the owner always has the Stamina state (stage 3.4f-C, criterion C-I10; test case SM6), also with the full
  stamina: 100 of 100, winded 0, the delay 0, the multipliers 1. The constructor of the client component (0x6719EB0) puts 95 into
  the server copy and into the predicted values; the init (0x6869E70) sets only the predicted values to `m_initMax` (100). With no
  fragment the server copy stayed 95 of 95, and the reconcile of a server movement state put 95 of 95 into the predicted values: the
  HUD showed a maximum of 95 (live trace of stage 3.4f-C: the predicted values 100 of 100 at the first reconcile, 95 of 95 at the
  second). With the fragment the client keeps 100 of 100 (live memory read of the component at +0xB0 and +0xD0; live proof P3: the
  bar in four equal parts, 10 of 100 after two dodges, 100 of 100 again 6 s later).
- The StaminaRate of the statuses (`StatusValues::staminaRate`: Holy Ground +0.25) is the key 0x0B of the speed table of the owner
  (the client multiplies its regeneration with it, client code 0x6888790), and the multiplier of the regeneration of the server.
- The cost of a rule depends on the players: the regeneration looks only at the characters whose stamina is not full
  (`m_regenerating`), and the dodges are in a due queue (`m_due`).

## The values of this build

| Value | Source | Value |
| --- | --- | --- |
| Maximum, minimum | Stamina component of `slices/player.dynamicslice` (`m_initMax`, `m_min`) | 100, 0 |
| Regeneration | `m_regenRate` | 33.33 in one second |
| Winded time at 0 | `m_maxWindedTime` | 2 s |
| Delay after a payment, at 0 | BaseVitals_Player `StaminaRegenDelay`, `StaminaRegenDelayOnEmpty` | 1 s, 1 s |
| Dodge costs | StaminaCosts_Player `Dodge_Roll_Fast`, `Dodge_Roll`, `Dodge_Roll_Slow` | 45, 45, 50 |
| Dodge times | `player.adb` (the fragments Dodge_Roll*: the variants with no tag and the standing variants) | pay 0.1 s, immune 0 to 0.5 s, CanTransition 0.5 s, buffered window 0.267 s to 0.5 s, instant window from 0.5 s |
| The delay of the exit | `playeractions_dodge.actionlist` SetStaminaRegenDelay OnExit | 1 s |
| Load factors | EncumbranceData, row Player | 1 for the cost and the regeneration of each class |

## Sources of the rules

| Rule | Source |
| --- | --- |
| The values above | Game data (`tools/gamedata/stamina.sh`) |
| The client predicts its stamina; the state does not change the predicted value | Live checks LS1 to LS3, LS8; client code 0x683E5D0 (static) |
| The regeneration, the delay of a payment and of the exit, no debt, the winded time, a payment of cost 0 | Live checks LS5 to LS7 |
| The bit 322 for each press, also a refused one | Live check LS9 |
| The checks of a dodge and the cost of the class | Game data (the action Dodge, the action Exhausted, the conditions EquipLoadCategory) |
| The time of a dodge is the world time of its input step; the immune window decides a contact at its time | A rule of this project (research task D) |
| A shot through a dodge flies on and never hits that character again | Game data (`m_destroyProjectileOnUnsuccesfulHit` false) and a rule of this project for the passed list |
| A buffered second dodge starts at the end of its window (one for each window); the loader refuses a buffered window with no end | Game data (CAGE-TransitionBuffered) and a rule of this project for the check |
| The maximum of the server is 100; the client showed 95 before stage 3.4f-C | Game data (100); client code and a live trace (95: the default of the server copy of the client component; protocol notes 4.38) |
| The first record has the Stamina state | Live trace and memory read of stage 3.4f-C (the cause of the 95) |
| The window of an earlier dodge (1 s, 8 windows), the check of a dodge before the end of an attack visit | Rules of this project (code review of stage 3.4f-B, findings CS-R1 and CS-R2) |
| A next dodge must start after the payment | A check of this project for the data (finding CS-R5) |

## Traps

- The client of the owner keeps its own predicted stamina: a Stamina state with a different value does not change the HUD or the
  dodge rule of the client (live checks LS2, LS3). A test of the server value must read the backend (`tools/world stamina`), not the
  HUD.
- Before stage 3.4f-C the client showed a maximum of 95, not 100 (live check LS8): the first record had no Stamina state, and the
  client default 95 of its server copy came into the prediction. A first record with no Stamina state brings this fault back (test
  case SM6). The value 95 is a default of the client code, not of the data: do not use it on the server.
- A timed status with only the cell Stamina must tick (`ticks()` of `world/statuses.cpp`): its gain comes at its first tick (at its
  start, with no DelayInitialTick). A status of 0.1 s with TickRate 0.25 gives its gain one time (Stamina Recovery, Mauler's Stamina).
- A dodge with no direction key rolls to the rear: in a live test the character can leave the line of a shot before the contact (live
  proof P2, the first dodge: no contact, no avoided hit). A dodge 0.25 s before the contact keeps the body on the line.
- The arrow of an archer crosses 10 m in 0.2 s: the pictures of `gamescopectl screenshot` (about 10 in a second) do not show it in
  the dodge. Use the log lines (`world: dodge: ... avoids the hit`) for the arrow, and the slow ball of a mage (20 m/s) for a video.
- The development route `/v1/dev/passives/table?table=stamina` lists the rows of the data with a stamina or dodge part; the
  character of a live run needs the great axe in the hand for the rules of its tree.
