# Combat and health

Read this document before a change of a melee attack of a player, the damage rules, the health and
the death of a character, the heals, or the mana, and before a live test of a hit or a heal. The wire
contracts are in the protocol notes (sections 2.3, 4.5, 4.7, 4.15, 4.18, 4.31, and 4.32).

## Start here

| Task | Start here |
| --- | --- |
| A melee attack of a player on a creature: the contact report of the client, the hit check, the damage rule, the damage number | Protocol notes 2.3, 4.7, and 4.15, `apps/backend/src/world/combat.h`, `gamedata/combat_tables.h`, `server/rep/client_input.h`, `server/rep/combat_messages.h`, `server/http/aws_routes.cpp`, `tools/gamedata/combat.sh`, `tools/research/input_packet.py --contacts` |
| The weapon switch in a swing, the press after CanTransition, the attack from rest of an overencumbered character (stage U3) | `Combat::select_weapon`, `Combat::step_switch`, `switch_rules` (`world/combat.h`), `AttackFragment::switchBuffer`, `switchAt` (`gamedata/weapon_actions.h`), `AttackBits::attackAllowed`, test cases CF6 to CF8, MB4 |
| The basic attacks of a melee weapon (stage 3.3b-M1): the light chain, the partial and the full charge, the sub-hits, the strike of a contact, the ends of a sequence | Protocol notes 4.32, `world/attack_sequence.h` (`AttackSequences`), `world/combat.h` (`Combat::step`, `claim_strike`, `row_for`), `gamedata/weapon_actions.h`, `tools/gamedata/weapon-actions.sh` (`weapon_actions.py`), test cases MB1 to MB9 |
| A hit of an ability: the contact of an ability phase, the hit of an area, the weaken of a creature | Protocol notes 4.22, [Abilities](abilities.md), `world/combat.h` (`Combat::claim`, `Combat::strike`), `world/abilities.h` (`Abilities::contact_phase`), test cases AB4, AB5 |
| A hit of a projectile of a ranged or magic basic attack (the bow, the fire staff) | Protocol notes 4.27, [Projectiles](projectiles.md), `world/projectiles.h`, test cases PJ3, PJ8 |
| A critical hit, a hit from behind, a headshot, the flags of the damage number | Protocol notes 4.30, `world/combat.h` (`Combat::critical`, `Combat::source_angle`, `damage_flags`), `world/projectiles.cpp` (`head_contact`), [Passive rules](passive-rules.md), test cases CH1 to CH10 |
| A heal: the one heal path, the heal base, the outgoing and the received modifiers, the number of the owner, the notice of the healer (stage 3.3b-S6) | Protocol notes 4.31, `world/health.h` (`Health::heal`, `heal_base`, `outgoing`, `received`), `world/statuses.cpp` (`Statuses::give`), `server/rep/states/vitals_state.cpp` (bit 1), `server/rep/health_messages.h` (message 4298), `server/http/heal_commands.cpp`, test cases HE1 to HE11 |
| The mana of a character and the mana meter of the HUD (the blue bar below the crosshair) | Protocol notes 4.5, `apps/backend/src/server/rep/states/mana_state.cpp`, test case UT4 |
| The health and the death of a character: the damage of a creature, the damage mark, the death screen, the respawn | Protocol notes 4.18, `apps/backend/src/world/health.h`, `world/combat.h` (`Combat::creature_damage`), `server/rep/health_messages.h`, `server/rep/states/vitals_state.cpp`, `server/http/health_commands.cpp`, `tools/world heal` |

## Rules

- The health and the death of a character (protocol notes 4.18). Only the health system
  (`world/health.h`) changes the health and the life state of a character
  (`Entity::vitals.health`, `Entity::life`); the progress system sets the maximum. The
  damage of a creature attack is computed in one function (`Combat::creature_damage`, a
  project rule: change it there and in the notes together): the damage of the level and of
  the kind of the creature, the row of the contact, the level difference, and the armor of
  the character. The health 0 is the death: the vitals state gets the dead bit (bit 0 of
  `vitalsData`) for the owner and for each observer, and the session of the owner sends the
  death notice (type 3950) with the time to the respawn screen (5 s). Only the session of
  the character that took damage gets the damage mark (type 3601,
  `SessionSink::damage_taken`). A dead character takes no damage, does no damage, and is
  the target of no creature. The respawn request of the client (type 1938) makes the
  character live again with its full health at the nearest travel place, or at the place
  of its death with no travel places: the change of the place is a travel
  (`World::travel`). The world finds the character from the session. The health and the
  death are not in the database: each world entry starts with the full health, and a dead
  character that leaves the world gets its respawn place in its row (`World::leave`).
- The first vitals state of a session for a character that lives has no `vitalsData` and no
  `healthChangeFlags` (the stored bytes of case T2). Each later vitals state of a character
  has the two fields.
- The mana of a character is in `Entity::vitals` and goes to its owner in two states with the
  same values: group 1 of the vitals state, and the mana state (slot 47, type 1652, owner
  only, `states/mana_state.cpp`). The mana meter of the HUD (the blue bar below the
  crosshair) reads only the mana state: with no mana state it shows always. A change of the
  mana marks the two states (`ComponentKey::vitals`, `ComponentKey::mana`). Protocol notes
  4.5. Stage 1.3b: each change of the mana goes through one path (`Abilities::change_mana`:
  a cost, a potion), and the regeneration of the data runs after a spend
  ([Abilities](abilities.md)).
- The mana maximum and the regeneration (stage 3.5b.2, a task of the orchestrator; protocol notes 4.5; test case MN1): the maximum is
  the base mana of the progression tables (ManaMax of the row Player, 100) times 1 plus the MaxMana parts of the threshold abilities
  of the attributes (`attributes.abilities` of `progression-tables.json`: Focus 100 gives `Foc_Bonus_100_1`, MaxMana 0.1 and ManaRate
  0.2), the perks, and the effects; the regeneration is the base rate (2) times 1 plus the ManaRate parts (`Vitals::manaRateFactor`)
  [game data]. A change of the maximum keeps the part of the current value, as the client does [client code 0x676B310]. The client
  computes the same maximum itself: its meter is the current value of the state over its own maximum, and it takes the base of that
  maximum from the maximum field of the first mana state [live check of the final run: a first state with 110 gave the client 121, and
  the meter stayed at 91 percent at full mana]. Thus the current value has the parts (110 of 110) and the maximum field of the mana
  state and ManaMax of the vitals state have the base (`Vitals::manaBase`, 100) [live check]. With no threshold rows (an older
  progression file) the maximum is the base: the client then has 110 with Focus 100 or more, and the meter shows 91 percent at full
  mana (the fault that the user saw before stage 3.5b.2).
- The heals (stage 3.3b-S6; before: `Health::heal_amount` of stage 1.3b). Each heal of a character goes through one function
  (`Health::heal`): an ability (a status with HealthModifierDamageBased: Divine Embrace, the pulses of the Beacon and of Sacred
  Ground), a potion, a food, a dust, a status of the gear, and a life steal (DmgPctToHealth). A heal has a kind
  (`HealKind`: ability, consumable, lifeSteal, regeneration), an amount, a healer, and a number mark. The function: a character
  that lives gets the amount times its received factor, to its maximum; the vitals state changes for the owner and for each
  observer (the health of a friend is public); the owner sees the number of the real gain (after the cap) one time; a healer
  that is a different character gets the notice (message 4298), only its session. A heal of a dead character, of a full
  character, or of an amount that is not above 0 changes nothing (no number, no notice). The rest rule (3 percent of the
  maximum each second after 6 s with no fight) and the regeneration of the gear values (`Entity::gear.regeneration`) run in
  `Health::tick`, with no number and no modifier (rules of this project).
- The base of a heal of an ability (`Health::heal_base`; a decision of the user of 2026-10-15, through the orchestrator): the
  base damage of the weapon of the cast at its gear score (the first factor of the damage rule, `Combat::weapon_base`) times
  1 + the scaling of Focus with the curve and the table of a damage attribute (the ScalingFocus of the weapon, LifeStaff 0.0095,
  game data, times the damage value of the Focus of the healer: the column ModifierValueSum of the Focus table, game data) + the
  level part of the damage rule (the level damage of the base attributes file, 0.025, times the level above 1: stage 3.3b-M1, a
  decision of the user of 2026-10-15 through the orchestrator: a heal scales with the level as damage does; case HE12). No part of
  a different attribute (the Intelligence of the life staff): a heal scales with Focus. A weapon with no ScalingFocus has no Focus
  part. The base is a value of the source of the heal at the cast (`HealSource`: the
  healer, the base, the row of the spell, the ability table of the weapon): a status keeps it, and its ticks heal with it (the
  client has a cached heal scaling value for each source of a status: StatusEffectSource 0x5CB7F20; the snapshot is a decision
  of an agent). The amount of a status of an ability: the base times HealthModifierDamageBased of the row (Divine Embrace 1.2,
  the Beacon and Sacred Ground 0.16) times the potency and the stacks, times the potency factor of its categories (the
  EffectPotencyMods of the other statuses of the target: the Beacon makes Sacred Ground 0.9 times; a decision of an agent:
  the factor of the heal of the status), times the outgoing factor of the healer, times the received factor of the target.
  A row with HealthModifierDamageBased and no UseHealScalingValue (its base is the core damage of the source) gives no heal
  in this stage (`StatusCounts::pulsesWithNoHeal`). Live check L9: the weapon damage that the client shows in the attribute
  window (`GetCoreDamage`) has the same base and Focus curve, and also the level part (0.025 for each level above 1) and the
  other attributes of the weapon (the Intelligence of the life staff, 0.004): the heal base has the level part (stage 3.3b-M1),
  not the other attributes.
- The outgoing factor of a heal of an ability (`Health::outgoing`): 1 plus one additive pool (a decision of an agent, as the
  damage parts and the critical pool) of the HealScalingValueMultiplier of the statuses of the healer (live check L9: the
  tooltip "outgoing healing is reduced by 3% per stack"), of the rules with OnHealed of the healer whose conditions pass for
  the target (`Passives::heal_bonus`: Divine Blessing +0.3 at 50 percent health or less), and the heal factor of the equip
  load of the healer less 1 (live check L9: the tooltip "Light: Outgoing Healing +30%", "Heavy: -30%"). 0 or more. A healer
  that left the world: no outgoing part. The name of the column (a multiplier of the heal scaling value) gives it only to
  the heals with UseHealScalingValue.
- The received factor of a heal (`Health::received`): 1 plus the HealMod of the statuses of the target (each kind; live
  check L9: "incoming healing is reduced by 6%"; game texts "reduces healing taken"), plus ConsumableHealMod (a consumable),
  NonConsumableHealMod (an ability, the gear, a life steal), and DmgPctToHealthHealMod (a life steal; game text of Anointed:
  "healing from non-consumables and lifesteal"), each with the limits of its categories (Disease -0.5), in one additive pool
  (a decision of an agent). 0 or more.
- A target of a direct heal (Divine Embrace) must be able to gain health (code review of stage 3.3b-S6, finding S6-R3, case RW3):
  the choice skips a friend at its full health and goes on to the next friend or to the caster; a caster at its full health with
  no such friend: no heal and no cost (a rule of this project: the client plays the cast; the rule "no heal, no cost" of stage
  3.3b-S6).
- The order: the outgoing factor times the received factor (a decision of the user of 2026-10-15: multiply where the data and
  the client prove no order).
- The heals of a consumable and of a life steal keep their proved rules (crit-heal-formulas.md): a potion is Health +
  HealthModifierPercent times the maximum health (game texts), a life steal is DmgPctToHealth times the damage of the hit
  (Critical Gains: "10% of damage done"), each times the heal factor of the equip load of the character (stage 1.3b) and the
  received factor of its kind.
- The number of the owner (live check L3; protocol notes 4.31): the vitals state of the owner that carries a heal has bit 1
  of healthChangeFlags (`Vitals::healShown`: the world revision of the heal); the client shows a green "+N", N the change of
  its own health. Each heal with a number is its own owner state (code review of stage 3.3b-S6, finding S6-R7, case RW7): the
  vitals keep such heals for 2 s on the world clock (`Vitals::healSteps`: the revision, the health before, the health after,
  the time; `kHealStepKeep`; 512 at most as a bound of the memory: code review of stage 3.3b-M1, finding M1-R8, case RB8: nine
  life steals of one strike lost the first number with the old limit of 8), and the encoder
  sends, from the health of the last owner state of the session (`Known::ownerHealth`), a state with the health before each heal
  and no bit 1 when the health changed with no number, the state after the heal with bit 1, and a last state with no bit 1 for a
  change with no number after it. The observer view never has bit 1 (live check L3: an observer shows no number). The kinds with a number:
  a heal of an ability, a life steal, the application of a consumable (a potion), and a tick of a consumable with
  ShowUiDamageNumbersOnHeal (the regen potions of the data); no number for a tick of a food, a status of the gear, the rest
  rule, and the command (a decision of an agent from the column of the data; the reader of the column in the client was not
  found in a bounded search: bit 1 of byte +0x368 of the status row, loader 0x44B6EFF).
- The notice of the healer (message 4298, live check L3): the session of the healer gets the id of the entity Stats of the
  healed character and the gain; the client shows a green "+N" at the friend. A self heal sends none.
- A melee attack of a player (protocol notes 4.15). The client finds each melee contact
  itself and reports it in the third list of its input entry (the contact report). The
  report is on in each start: the backend answers one remote configuration request of the
  client with the document that turns the client setting
  `javelin.enable-client-authoritative-melee` on (`server/http/aws_routes.cpp`; the exact
  request of protocol notes 2.3; each other request to that host keeps the reply 404: the
  route compares the full path text and the host with no port, `Router::add_exact`).
  `SOULWARDEN_MELEE_REPORT=0` (backend option `--no-melee-report`) turns it off: an attack then
  has no effect. The document has this one setting: do not add a setting without a live
  check.
- A contact is a claim of a client, not a fact. The session gives each claim to the combat
  system of the world (`world/combat.h`, `Combat::claim`), which finds the attacker from the
  session and checks the claim: the character lives, the target is a creature of the world
  that lives and that the session observes, the reference names a component of its slice, a strike of the sequence of the
  basic attacks of the character fits the contact (stage 3.3b-M1, below; with no weapon actions: an attack input of the client
  fits it and the same attack did not hurt the creature), the character has a melee weapon in its hand, the exact position of
  the character is 5 m or less from the creature, and the client made 64 claims or less in 1 s (stage 3.3b-M1, a decision of an
  agent: 12 before; a chain of two or three strikes in one second on many creatures is legal). A contact in the window of a
  phase of an ability of the character (stage 3.3a, `Abilities::contact_phase`) is a hit of
  that phase: the damage row of the phase, the weapon of the cast, and one hit of a creature
  for each phase in place of each attack; the other checks stay. A refused claim changes nothing
  and gets no reply; the log counts the refusals with their cause. The values of one client
  for the check (`world::AttackTrack`: its attack inputs and the counts) are in its session,
  as the values of the position check. The two limits of a hit (the rate of the claims, and
  one hit of a creature for each attack) are values of the character, not of a session
  (`world::HitLimits`, `Combat::limits_of`, with the character id as the key): a new session
  of a character gets the limits that the character has, thus a client that connects again
  and again gets no more hits. The session takes each input step one time: a client sends
  an entry in 5 reports. An input entry that ends before the end of its body has no
  position of the character (`ClientInput::hasCharacter`).
- The basic attacks of a melee weapon (stage 3.3b-M1, protocol notes 4.32). The combat system follows the sequence of the
  basic attacks of each character (`world/attack_sequence.h`, `AttackSequences`; the character id is the key, rule MP-1) with
  the input steps of its session (`Combat::step`, before the contacts of the step), as the client plays the attack graph of the
  weapon in its hand (`weapon-actions.json`, game data): the clock of a fragment is (k - s + 1) / 30 at the input step k of a
  fragment that started at s; from rest the attack input starts Attack_Start_Charge; an instant transition takes the first step
  of its window whose input fits (the attack input or its hold for CanAttack_Heavy, the attack input for
  CanAttack_Primary_Rapier, neither for Attack_Heavy_NotHeld, the release for Attack_Primary_Released, each step for the full
  charge); a buffered transition takes a press in its window and starts its fragment at the end of the window; a fragment ends
  at its length (the end of its animation). Source: the data of the action lists and of the animation databases, and the live
  checks I1 (the client fragments of the action layer and the contacts of 7 weapons). A contact is a hit of a strike (the damage
  clips of one damage key of a fragment: body, attachment, each shape) when the clock of a visit at the input step of the contact
  is in the window of the strike, 1 step before its start to 2 steps after its end, and on the world clock not more than 250 ms
  before its time and 0.5 s after it (decisions of an agent; live: each contact in its window). The row of the strike is the
  first row whose clip condition the learned nodes of the character meet (the pull of the great axe: Passive_Greataxe_Mauler_
  HeavyAddPull; a strike with no row for the character is no match). A strike hurts each creature one time (a ledger of the
  last 4 visits: a second contact of the same strike is `sameAttack`), and 32 creatures at most. A press within 0.25 s of the
  length of a fragment (`attack_rules::kLengthBand`) can be the next step of the chain or a new attack: the visit keeps the other
  fragment, and a contact that fits only that one selects it (a decision of an agent: the length of a fragment with two clips of
  one animation is not fully known). The sequence ends at a change of the weapon, an accepted ability (also the aim of an aimed
  ability), a travel, the death, a new session or a new input sequence of the client (a reconnect), and a dodge (bit 322): at once
  in a CanDodgeCancel window that starts after the start of the fragment, at the end of a CanDodgeBuffer window; a dodge in a
  cancel window that starts with the fragment is no end (live check: the client plays no dodge there). From stage 3.4f-B the
  dodge that ends the sequence (`AttackSequences::take_dodge`) goes to the stamina system, which decides it with its own stamina and
  makes the character immune for 0.5 s ([Stamina and dodge](stamina-and-dodge.md)). The payments of a fragment (PayStaminaCost,
  CAGE-PayManaCost) pass with the visit: a stamina payment goes through the stamina path (stage 3.4f-B; the stamina rows of the basic
  attacks of the reachable graphs are 0: no change, live check LS7), a mana payment goes through the one mana path, and a payment with
  a condition (the stance of the greatsword) is counted only. With no file (or a weapon with no graph: the dagger) the rule of stage 3.2a stays: the light
  or the heavy row of the weapon table, and one hit of a creature in 250 ms.
- The corrections of the code review of stage 3.3b-M1 (test cases RB1 to RB5; each one fails on the tree of stage 3.3b-M1):
  - The due time (M1-R1): each visit has the time of the world clock at which its start is due (`AttackVisit::dueAt`): the
    time of the press that started the attack from rest, and 1/30 s for each input step from that press. The earliest time of a
    strike counts from it. A client that sends its input numbers faster than 30 in a second does not reach the full charge or a
    strike before its time.
  - The release (M1-R2): a step with neither the attack input nor its hold is a release too. The full charge of a charge
    fragment (the transition with no condition) needs the press or the hold at its step.
  - The fragment of a visit (M1-R3): a contact that fits only the other fragment of the band selects it only at its hit
    (`AttackSequences::note_hit`); the first hit of a visit commits its fragment, and a later contact cannot hit with the other one.
  - The lost reports (M1-R4): the steps of a gap of 90 steps at most get the last hold of the client and no press, release, or
    dodge. A buffered transition starts its fragment at its due step (the end of its window), also when the step of the report
    came later.
  - The end (M1-R5): a visit that ended has no contact more than 2 steps after its end. The loader refuses a strike that starts
    after the end of a fragment of a known length.
- The weapon switch (stage U3, research Q6; game data: the marks CanUnsheathe_Buffer, CanUnsheathe, and CanTransition in the ProcLayer of
  each fragment of the weapon actions, `AttackFragment::switchBuffer` and `switchAt`, the smaller of the two last marks; the grid rules
  62 Attack -> Sheathe and 116 Sheathe -> Attack; live checks SW-L1 to SW-L3). A key of a weapon slot (`Combat::select_weapon`) at rest
  switches the weapon at the step of the key. A key in a swing waits for the switch mark of the fragment that plays (the war hammer
  swing 1.133 s, the great axe swing 1.1 s): the switch comes at that step when the buffer mark (0.4 s, 0.3 s) opens within 20 steps of
  the key (BackTicks 20 of the key input); until then the old swing plays and hits. A key with no such mark in its 20 steps is lost:
  the weapon stays, as in the client (live check SW-L3: the client kept the war hammer in its bar). At the switch the weapon of the
  world changes (the damage, the passive rules), and the attack sequence ends. A press from the key to 3 steps after the switch starts
  nothing then: it is kept, and the first light swing of the new weapon starts 3 steps after the switch (live check SW-L1: the great axe
  swing at the key step + 3; SW-L2: a press 21 steps before the switch still played). An ability press in that time is refused
  (`CastVerdict::switching`). The log lines: `world: weapon: NAME switches to slot S at input N (the key at input K, D steps before)`
  and `world: weapon: NAME, the key of input N is lost: ...`. Test cases CF7 and MB4.
- The press after CanTransition (stage U3, research Q6; game data: the mark CanTransition of a fragment; live check CT-L1): a chain
  press at or after the CanTransition time of the fragment that plays gets the first fragment of the chain as its other fragment (the
  visit keeps both, and the first hit selects one, M1-R3). In CT-L1 the client always played the chain (the war hammer C2 and then
  Strike2 for a press 1.033 s to 1.33 s after the swing start; the great axe the same at 1.2 s), and the server accepted the hits of
  Strike2. Test case CF8.
- The weight (stage U3, [Items and equipment](items-and-equipment.md)): a press at rest of an overencumbered character (above 1.5 x the
  limit) starts no attack (`AttackBits::attackAllowed`; the client shows "Overencumbered - Cannot attack" and plays no swing); a chain
  of an attack that plays goes on. Test case CF6.
- The fragments of the abilities (stage 3.3b-M2; [Abilities](abilities.md)): an accepted press of an ability of the great axe
  or the war hammer starts the fragment of its action as a visit of the sequence with the cast (`AttackSequences::start_ability`).
  The fragments of one ability keep its cast (the exit of Whirlwind, the uppercut of Charge). A contact of an ability visit is a
  hit of the abilities system (`Abilities::strike_hit`) with the reach of the shape of its row. The sequence notes a hit (its
  ledger, the hits of the cast for IfAbilityNumHits, a transition at a collision) only after the hit is accepted. Two strikes
  can fit one step (the late tolerance of a spin and the start of the next spin): the strike of the time window of the step comes
  first (the window that holds the step, else the nearest one; at a tie the strike that started last), and then the ledger: a
  strike that hit the target already is a repeat (code review of stage 3.3b-M2, finding M2-R2: the ledger chose the strike, and two
  identical contacts of one step hit two spins; case RH2). The sequence gives the strikes, the spell casts, and the marks of a
  fragment of an ability to the abilities system at their times (`SequenceEvent`), each with its due time on the world clock: the
  due time of the visit and the time of the item, with the tolerance of a contact (one step and `attack_rules::kWorldEarly`). An
  input number that reaches an item earlier (a faster client, the gap fill) does not run it: the item waits for its due time, and
  it runs only when no end of the sequence interrupted its visit (finding M2-R1; case RH1), and only when no end of the sequence
  came after it at all (`SequenceEvent::generation`, `State::generation`; code review of stage 3.3b-M2b, finding M2b-R2, case RX2:
  a travel or a change of the weapon after the visit had ended at the end of its fragment, also with a later basic attack). An end
  of the sequence at rest (no visit plays) counts too (`AttackSequences::end`). One function runs each item at its due time
  (`Combat::run_event`). The start of a fragment that waited for a buffered window is an item too (`SequenceEvent::Kind::start`: the
  spells of its action, finding M2-R5), and the start of a fragment at the step of the press too (finding M2b-R1, case RX5: the
  spells of the action wait for the due time of the visit). The block press (bit 484) is a bit of the step (`AttackBits::block`).
- The damage of an attack is computed in one function (`Combat::damage`), from the combat
  tables (game data) and the entity of the attacker: the weapon of the item, its gear score,
  the damage row of the light or of the heavy attack, the attributes and the level of the
  attacker, and the mitigation, the weakness, and the absorption of the creature. The rule is
  a project rule (protocol notes 4.15): change it there and in the notes together. The health
  goes down through the creature system, each observer gets the vitals state, and only the
  session of the attacker gets the damage number (`SessionSink::damage_dealt`, message type
  2071 for the damage receiver facet of its own character). The client computes no damage.
  Each hit of a player on a creature goes through one function (`Combat::strike`, stage
  3.3a): `Combat::damage` with the row, `Creatures::hurt` with the attacker (the target, the
  credit, the rewards of a kill), the damage number, and the log line. A melee attack, the
  contact of an ability, and the hit of an area of an ability use it; a new hit uses it too.
  The hit of a projectile (stage 3.3b-S3, [Projectiles](projectiles.md)) uses it with the row of
  its shot, the weapon and the gem of its launch, the damage modifier of its ammunition (1.2 for
  each arrow and shot of the game data) as the factor, and its point as the source. A melee
  contact of a ranged weapon is refused (`no melee weapon`): the bow and the musket hit only
  with their projectiles. From stage B1 the weapon of a hit (`HitWeapon`) also has the ability action of its cast (`action`: the
  rules with InAction), the volley (`volley`: the cast number of the arrows of a bow ability; the rules with NumConsecutiveHits), and
  a part of the damage (`part`: the hit rate of a pierce, [Projectiles](projectiles.md)), which `Combat::strike` adds to the damage
  parts of the hit (the sum with BaseDamage of the rules and the gear).
  From stage 3.3b-S4 a creature that lives after the hit moves by the impact of the row (the pull or
  the push of the game data, from the source of the hit: the last argument of `Combat::strike`, the
  attacker when it is not given; [Forced movement](forced-movement.md)).
  The damage of a creature to a character (`Combat::creature_damage`) is multiplied by the
  weaken of the creature (`Creature::damage_factor`: 1 plus the `DMGStandard` of each of its
  effects, not below 0). From stage 3.4f-D the part of an effect is the DMG<type> of its row for the damage type of the hit (the
  rule of the damage of a character: Greatsword_Burst_Weaken has DMGStandard -0.1 and -0.15 for each type), or DMGStandard for a row
  with no typed part; a buff of the creature raises it (the Howl of the Evil Knight hound: 0.1, a bite of 29.9 does 32.9 with it; live
  proof P2 of stage 3.4f-D, case CRH3). The heal of a creature has its own path ([Creatures](creatures.md), `Creatures::heal`).
- The gear in the two damage rules (stage 1.3b, protocol notes 4.15, 4.18, and 4.23; the
  rules in [Gear and perks](gear-and-perks.md)): the part of the gear of the attacker, the
  conversion of a gem, the DMG and ABS parts, the armor parts, a rend of the creature, and
  BaseDamageReduction. A hit of a fixed amount (a tick, a chain) goes through
  `Combat::strike_amount`. The damage number has a second entry for a converted part. The
  hit of an ability uses the conversion of the gem of the weapon at the press of the cast
  (`CastConversion`, the last argument of `Combat::strike`), not the weapon in the slot at
  the time of the hit (code review of stage 3.3b-S1, finding F3, test case RS3).
- Critical hits (stage 3.3b-S5, protocol notes 4.30, test cases CH1 to CH8). `Combat::strike` collects the facts of the
  hit first: the attack (`HitContext::attack`: one press, one cast, or one projectile), the kind (a contact, a projectile,
  an area), the health of the target before the hit, a hit from behind, and a headshot. Then it asks the evaluator of the
  passive rules for the bonuses ([Passive rules](passive-rules.md)), then `Combat::critical` decides the critical hit. The
  rule and the source of each part:
  - The chance: the CritChance of the weapon in the hand (`criticals` of the combat data), plus the CritChance of the fixed
    node rules and of the rules of the hit, plus the CritChanceModifier of the statuses of the attacker; 0 to 1. Source:
    game data, and a decision of the user (2026-10-15: the chance of the data from the weapon, the perks, the nodes, and the
    statuses).
  - The roll: each hit with a chance above 0 takes one independent number of the generator of the world
    (`World::random_unit`, `World::set_random_seed` in a test). The hit is critical when the number is below the chance.
    Source: a decision of the user. A hit of a fixed amount (`Combat::strike_amount`: a tick, a chain) takes no number.
  - A row with an explicit CanCrit 0 in the data (`DamageRow::noCrit`: the burst of Gravity Well,
    `GreatAxe_GravityWell_Burst`) is never critical: no roll, no hit from behind, no headshot. A blank CanCrit cell can be
    critical. The combat file of format 2 keeps the difference (a blank cell is null). Source: game data (code review of
    stage 3.3b-S5, finding F2, test case RV2).
  - The multiplier: the DmgCoefCrit of the row when it has one (11 rows: Path of Destiny 1.2), or else the
    CritDamageMultiplier of the weapon; plus one additive pool of the bonuses (the CritDamage of the rules, the
    CritDamageModifier of the statuses, HeadshotDamage for a headshot). Source: a decision of the user (one additive
    pool); DmgCoefCrit in the place of the weapon value is a decision of an agent (the row value is the critical
    coefficient of the row, as DmgCoefHead is its head coefficient). The multiplier changes the damage that the target
    takes: both parts of a conversion.
  - A hit from behind (`Combat::source_angle`): the angle between the front of the creature (its yaw) and the source of the
    hit (the attacker, the point of a projectile, the center of an area) is not less than m_isHitFromBehindMinAngle of the
    slice of the creature (the wolf: 2.0944, 120 degrees). At m_isHitByCritFromBehindMinAngle or more (the wolf: 2.3562,
    135 degrees) and with a row that has no NoBackstab, the hit is critical with no roll. A source at the creature (less
    than 0.05 m) has no direction: no hit from behind. Source: game data (the DamageReceiver of the slice) and the decision of
    the user (where the data makes such a hit critical, it is critical).
  - A headshot: only a projectile, when its line meets the head volume of the creature
    ([Projectiles](projectiles.md)), and its row has no NoHeadshot. A headshot is critical with no roll. Source: game data
    (the HitVolume with m_isHeadshot), the client (the special number for Headshot with Ranged and CanCrit, live check
    L6), and the decision of the user. A melee contact has no headshot: the contact of the client has no bone.
  - The flags of the damage number (`damage_flags`): Ranged for each hit of a projectile, Crit and CanCrit for each
    critical hit, Headshot for each headshot, DamageCausedDeath for a kill. Only real facts of the hit: no other code
    writes a flag.
  - A combat file with no `criticals` (from a tool before stage 3.3b-S5) gives no critical hit and no roll. The loader
    refuses a file with only one of the two lists `criticals` and `hitRules`.
- ArmorPenetration of a rule (stage 3.3b-S5): the part of the mitigation of the target that the hit does not meet. The
  defense factor is the mitigation times (1 plus the armor part of the target) times (1 minus the penetration, 0 to 1).
  Source: game data (the cell of the rule); the place in the formula is a decision of an agent.
- A shot of a creature (stage 3.4f-A, [Projectiles](projectiles.md)): the hit of the projectile on a character is one damage result of
  the same function (`Health::creature_hit`) with the row of the release (its factor, its added damage, its type: the damage of the
  creature rule), at the time of the contact of the flight, not at the release. The damage mark goes to the owner (its source place is
  the place of the creature), each observer gets the vitals state. The counter of Riposte takes such a hit in its window (its
  absorption), and the counter strikes only a creature in its reach (the reach of a shot is 0).
- A strike of a creature area (stage 3.4f-C, [Spell entities](spells.md)): the hit of the strike on a character is one damage result of
  the same function (`Health::creature_hit`) with the damage row of the spell row in the damage table of the slice (the lightning:
  `UndeadShamanDamageTable/Undead_Shaman_Lightning_Strike`, factor 0.85, Lightning), at the time of the window of the strike. The dodge
  decides it as a melee hit: in the immune window the strike does no damage (game data: the damage rows of the four strikes have
  IgnoreInvulnerable 0, and their tables have no column ForceAttackToHit; client code 0x692E4A0). Live proof P2: 121.9 damage for each strike of a level 16 Undead
  Shaman.
- The damage of a status tick (stage 3.4f-C, `Health::status_damage`; [Status effects](status-effects.md)): one loss of health with the
  DamageType of the status row, no armor (a project rule), no damage in the immune window of a dodge (but for IgnoreInvulnerable), and
  the damage number of the owner. A tick that takes the last health is the death of the character.
- A creature hit on a character in the window of a counter (Riposte, stage 3.3b-S4): the absorption of the status of the
  window takes the damage, and `Health::creature_hit` gives the hit to `Abilities::countered` after the damage: the
  counter starts one time and hits the attacker ([Forced movement](forced-movement.md)).
- The death of a character ends its spell entities only when the spell row has
  `EndOnCasterDeath` (`Spells::caster_died`; [Spell entities](spells.md)). The revival
  (`Health::heal` of a dead character) and the respawn refresh the gear: the statuses of the
  gear come back (finding F5, test case RS5).
- The weapon in the hand of a character is the item of the weapon slot that its client
  selected with a weapon key (`Entity::combat.selectedWeapon`), or the first weapon slot
  with an item. The sheathe bits and the active bits of the paperdoll state do not follow
  the keys yet (roadmap 3.1).

## Commands

```sh
tools/world heal NAME                # give a character its full health (a dead character lives again at its place)
tools/world hurt NAME 500            # lower the health of a character (the health 0 is its death)
tools/gamedata/weapon-actions.sh     # write weapon-actions.json (the attack graphs of the melee weapons) to the game data folder
tools/gamedata/weapon-actions.sh --find greataxe   # print the graph of a weapon: transitions, strikes, payments
tools/world list                     # the line "basic attacks": the sequences, the visits, the strike hits, the ends by cause
```

- `tools/gamedata/weapon-actions.sh` (stage 3.3b-M1) reads the weapon tables, the animation databases of the body and of the
  weapon attachment, the action lists, and the animation files of the player (`SOULWARDEN_OODLE_LIB` reads the pak files; 5 s)
  and writes `weapon-actions.json` (12 tag sets of melee weapons, 119 fragments, 275 transitions, 89 strikes, 3 payments; 0.07
  MB). With no file the backend starts, and each melee hit uses the rule of stage 3.2a (the log says it at the start).

- `tools/gamedata/combat.sh` writes `combat-tables.json` to the game data folder (14,073 items
  with a weapon, 587 weapons, 414 damage rows, the defense values of 8,779 vitals rows; from stage
  3.3b-S5 also `criticals`, the critical chance and multiplier of each weapon, and `hitRules`, the CanCrit, NoHeadshot,
  NoBackstab, DmgCoefHead, and DmgCoefCrit cells of each damage row). It
  reads the data tables and the base attributes file of the game copy from the sources of
  the creature data tool: the extracted files of the work folder are sufficient for it (6 s),
  and `SOULWARDEN_OODLE_LIB` reads the pak files. `tools/gamedata/combat.sh --find TEXT...` prints
  the weapons and the creatures with a text (`1hswordt1`, `wolf_grey_7`). The backend log
  says at the start if the tables are loaded. With no file the backend starts, and each hit
  claim is refused (`no combat tables`). The damage rows of the abilities (the rows
  `GreatAxe_*` and `Warhammer_Damage_*`) are in the same file (`CombatTables::row_index`).

## Traps

- The death screen: the text "YOU DIED!" with a count of 5 s, then the respawn screen
  with the button "Respawn" at `1000 581` (window of 1256x660). The client shows its
  loading screen for 5 s to 8 s after the click. `tools/world heal NAME` makes a dead
  character live again at its place with no loading screen.
- A character is never dead after a login: the health and the death are not in the
  database. A dead character that leaves the world is at its respawn place (the nearest
  travel place) at its next world entry.
- The health bar of the HUD is grey at the full health and red below it.
- The melee contact of the client does not go through a wall: a player cannot hit a
  creature on the other side of the yard wall of the start settlement (7 tries, 0
  contacts).
- The blue bar below the crosshair is the mana meter: it shows when the client has no mana
  state, and it goes away at 100 percent of the maximum of the client (not of the state).
- A change of the maximum of the client keeps a full meter full (the client scales the current value): the meter shows a wrong
  maximum only after the next mana state. Probe the maximum with `tools/world fragment NAME 47 1652 010f<current><base>0000000000000000`
  (a value under the maximum shows the bar).
- Before stage 2.2 the meter did not move during the regeneration: the mana state writes only the server copy of the client. From
  stage 2.2 the session sends the message 4289 with each later mana state, and the meter fills with the prediction of the client
  (protocol notes 4.5, test case MN2). Without the ids of the player slice (`slice-ids.json`) the backend sends no 4289 (a log line):
  the meter then stands still again.
- A sword attack gives a contact at 1.8 m or less, a spear attack only at 2.0 m or more:
  select the sword (`tools/hx 'key 0x31'`) for a test of a hit at 1.5 m.
- A click with the left pointer button in the world is an attack: each attack moves the
  character 1 m to 2 m forward (from stage 3.2a an attack hurts a creature: the block
  "Hits").
- A test of an attack: `tools/world ignore Soulwarden on` (if not, the wolf attacks the
  character, and it moves to the character), `tools/hx 'look -900 0'`, `tools/world
  creature spawn Wolf_Grey_7 Soulwarden -2.2 0 0 --yaw 270`, then `tools/hx 'click 628 330'` (a light attack) or
  `'mousedown left' 'wait 600' 'mouseup left'` (a heavy attack). The backend log has one
  line for each hit (`world: hit: NAME hurts creature ID ... N damage of the type T ...,
  health A to B`), one line for the first refused claim of each cause (`rep: hit claim
  refused (CAUSE)`), and the counts at the end of the session (`rep: hit claims of the
  session`). From user test 2 (UT2-13) each claim refused with "no strike of the attack fits the contact" and each press of an
  ability refused with "a different cast plays" (`casting`) has its own line with the visits of the sequence of the character
  (`AttackSequences::describe`: for each visit its fragment, its start input, its end input and the cause of the end, the clock
  of the input in it, its nearest strike window, and its due time on the world clock against now; an ability that waits for its
  step), at most 32 lines of each in a session.
- The reaction of a creature to a hit (user test 2, UT2-9) comes from the damage row of the hit and the rule of the action Reaction
  of the action lists ([Creatures](creatures.md)): a tick of a damage over time (`Combat::strike_amount`: no damage row) plays no
  reaction; `Combat::strike` gives `Creatures::hurt` the reaction of the row (`hit_reaction`), and the push of its impact lasts the
  reaction that the hit started at most (`HitResult::push`; with no reaction and no NoReaction the hit moves nothing).
- Each attack moves the character 0.7 m to 2 m to the creature. A creature has no body that
  stops the character: after some attacks the character is 1 m from the creature, and some
  attacks then give no contact (the thrust of a heavy sword attack at 1.0 m, each spear
  attack at 1.7 m or less). Spawn a new creature at the distance of the test.
- The distance at which an attack gives a contact depends on the weapon: a light sword
  attack 1.8 m (not 2.4 m), a great axe 2.3 m, a light spear attack 2.0 m to 3.4 m, a heavy
  spear attack only near 3.9 m (2 of 10 tries). No contact is not a fault of the backend:
  look at `tools/research/input_packet.py DATA_DIR/captures --contacts`.
- The damage number shows for about 1 s. Take the screenshots in a loop directly after
  the input (6 calls of `tools/shot` need 1 s). The number is the whole part of the damage
  (75.7 shows as 75).
- The kind of an attack comes from the time of the attack input: a click (the button down
  for less than 0.27 s) is a light attack, and a hold is a heavy attack. `'click X Y'` of
  the harness is a light attack. For a heavy attack keep the button down for 0.4 s or
  more (`'mousedown left' 'wait 600' 'mouseup left'`).
- While the button stays down, the client attacks again and again, and it attacks one more
  time after the button went up. Each contact is one hit.
- The mode of the client comes from a document that the client reads before the main menu
  (and again in the main menu). A backend that starts after the client asked gives no
  document for that start. Read the two values with the method of protocol notes 2.3 when
  an attack has no effect: the game log has the line `Retrieved layer's
  "remoteCfg.publicGameplay.ProductId" remote config ... successfully, size: 50` when the
  client got the document.
- `tools/gamedata/combat.sh` writes into the common game data folder: the next start of the
  user then has the damage. For a test of a different file, give the tool an output path
  and give the run its folder with `SOULWARDEN_GAMEDATA_DIR` (the folder needs the other game
  data files too).
- A weapon key (1, 2) selects the weapon slot for the damage of the backend. A world entry
  starts with the first weapon slot again, in the client and in the backend.
- A dead rabbit shows the text "No equipped Knife" at its name: the skinning interaction of
  the client, which the backend does not have.
- A live test of a critical hit (stage 3.3b-S5): the chance of a weapon is low (the war hammer 6 percent). Give the
  character a status with CritChanceModifier: `tools/world status Soulwarden TSE_CritChance_Inc_10PCT 600 10` (10 stacks
  for 600 s: each hit is critical). Give the seconds: the row has the duration -1, and the seconds 0 (the duration of the
  row) make it an application of one moment with no stack (the count "one application now"). For a hit from behind remove it (`tools/world status Soulwarden remove
  TSE_CritChance_Inc_10PCT`) and spawn the wolf with its back to the character: `tools/world creature spawn Wolf_Grey_7
  Soulwarden -2.2 0 0 --yaw 90` (`--yaw 270`: the wolf looks at the character). The log line after the hit line gives the
  cause: `world: hit: NAME, the hit on creature ID: critical: roll|behind|headshot (chance C, roll R, multiplier M)`. The
  roll 1.000 is "no number taken".
- A test of a combo (stage 3.3b-M1): the reaction of a wolf moves it back after a hit, and the next swing of a chain often makes
  no contact (the second swing of the war hammer gave none in 5 tries). Let the first swing miss: spawn the wolf 0.8 s after the
  first click (`tools/world creature spawn Wolf_Grey_7 NAME -2.4 0 0 --yaw 270 --level 65`; a level 65 wolf has 15,800 health).
- The spear gives no melee contact in the client of the runs of stage 3.3b-M1 (1.9 m to 3.3 m, a wolf of the level 18 and of the
  level 65), but in stage 3.2a it did. The sequence of the server follows its fragments (the action layer); the cause is open.
- A travel with `--screen off` turns the camera of the client to the north and selects the first weapon slot: turn the camera
  (`tools/hx 'look -300 0' 'wait 300' ...`) and press the weapon key after the travel, not before it.
- The action layer of the client (the predicted movement state, `tools/research/player_position.py`; a run with
  `SOULWARDEN_TRACE=6769980`) has the action number and the start step of the fragment that the client plays (for example the great
  axe: 5055 Attack_Start_Charge, 467 the first swing, 484 the second): compare it with the visits of the server
  (`world: hit: ... (light attack, FRAGMENT KEY, row ROW)`). The script of the evidence of stage 3.3b-M1
  (`tools/fragment_sampler.py` of the evidence folder) reads it while the keys of the harness play.
- The critical number is orange and larger, with no other mark. A number in yellow with an arrow is a weakness of the
  creature (thrust on a wolf), not a critical hit.
