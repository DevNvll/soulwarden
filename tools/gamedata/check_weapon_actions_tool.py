#!/usr/bin/env python3
"""Check of tools/gamedata/weapon_actions.py (stage 3.3b-M1) with made-up files in memory: no file of the game.

Each case prints "ok NAME: ..." or "FAIL NAME: ...". Status 0: each case passed. Case MB9 of the test program runs it.

  variant          the variant of a fragment: no FragTags, the tags in the weapon tags, the most tags, the weapon file first
  clock            the start of a clip: the sum of the ExitTime values before it in its layer; its end: the next clip
  length           the length of a fragment: the StartTime of the blend (seconds), the speed, and a second clip of one animation
                   that continues the time of that animation (the first swing of the war hammer: 1.4467 s)
  length unknown   an animation that the game copy does not have: the length 0
  animation file   the length of an animation chunk file (chunk 0x3002: (end - start) x ticks x seconds per tick)
  conditions       an AND of IfAbilityActive tests with Not: the nodes that a clip needs and must not have; an Or, or a test of a
                   different kind: not an AND of node tests
  graph            the graph from Attack_Start_Charge: the transitions of the basic attacks (the kinds of their conditions), a
                   transition to an ability not in the graph, the action of a transition resolved to its fragment, the strikes of
                   the body and of the attachment by their damage key, a clip with a node condition, the payment with its condition,
                   the dodge windows
  refusals         a transition to an action that no action list has, and a damage clip with no row: the tool stops (Failure)
  abilities        format 2 (stage 3.3b-M2): an ability action list with an action whose cooldown is an ability, a fragment that it
                   selects by a node and by a perk (the last selection that holds wins in the backend), a spell cast with the mark of its
                   condition, a transition into the ability from a basic fragment (CanAttack_<ability>), the transitions inside the
                   ability with their conditions as terms (the hits of IfAbilityNumHits, a node, an input, a collision, the water of the
                   game: false), a transition with a test that the backend has no source for (not exported), a spell cast of a fragment,
                   and the shape of each row of a strike
  incomplete       the whole tool (main) with sources that are not complete (code review of stage 3.3b-M1, finding M1-R6): a graph
                   that names a fragment that the body file does not have, and an attachment file with no fragment: status 1, and the
                   old output file stays; the complete sources: status 0 and a new file
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path
from xml.etree import ElementTree

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import weapon_actions as tool  # noqa: E402
from appearance_tables import Failure  # noqa: E402

BODY = """<AnimDB><FragmentList>
<Attack_Start_Charge>
 <Fragment Tags="2H_Melee+TestAxe"><AnimLayer><Blend ExitTime="0" StartTime="0"/><Animation name="test_charge"/></AnimLayer>
  <ProcLayer><Blend ExitTime="0"/><Procedural type="CAGE-TransitionInstant"><ProceduralParams><Condition value="Attack_Heavy_NotHeld"/><NewAction value="Attack_Primary_C1"/><NewFragment value=""/></ProceduralParams></Procedural>
   <Blend ExitTime="0.333333"/><Procedural type="CAGE-TransitionInstant"><ProceduralParams><Condition value="Attack_Primary_Released"/><NewAction value="Attack_Heavy"/></ProceduralParams></Procedural>
   <Blend ExitTime="0.333334"/><Procedural type="CAGE-TransitionInstant"><ProceduralParams><Condition value=""/><NewAction value="Attack_Heavy_Charged"/></ProceduralParams></Procedural></ProcLayer>
  <ProcLayer><Blend ExitTime="0"/><Procedural type="CAGE-TransitionBuffered"><ProceduralParams><Condition value="CanAttack_Ability_Test"/><NewAction value="Ability_Missing_Introa"/></ProceduralParams></Procedural></ProcLayer>
 </Fragment>
 <Fragment Tags="2H_Melee+TestAxe" FragTags="FireOil"><AnimLayer/></Fragment>
</Attack_Start_Charge>
<Attack_Primary>
 <Fragment Tags="2H_Melee+TestAxe"><AnimLayer><Blend ExitTime="0" StartTime="0.1"/><Animation name="test_swing" speed="0.96"/>
   <Blend ExitTime="0.43333334" StartTime="0.38"/><Animation name="test_swing" speed="1.3"/></AnimLayer>
  <ProcLayer><Blend ExitTime="0.333333"/><Procedural type="CAGE-TransitionBuffered"><ProceduralParams><Condition value="CanAttack_Heavy"/><NewAction value="Attack_Start_Charge_C2"/></ProceduralParams></Procedural>
   <Blend ExitTime="0.333334"/><Procedural type="CAGE-TransitionInstant"><ProceduralParams><Condition value="CanAttack_Heavy"/><NewAction value="Attack_Start_Charge_C2"/></ProceduralParams></Procedural></ProcLayer>
  <ProcLayer><Blend ExitTime="0"/><Procedural type="CAGE-Named"><ProceduralParams><Name value="CanDodgeCancel"/></ProceduralParams></Procedural>
   <Blend ExitTime="0.2666"/><Procedural type="CAGE-Named"><ProceduralParams><Name value="CanDodgeBuffer"/></ProceduralParams></Procedural>
   <Blend ExitTime="0.3667"/><Procedural type="CAGE-Named"><ProceduralParams><Name value="CanTransition"/></ProceduralParams></Procedural></ProcLayer>
  <ProcLayer><Blend ExitTime="0"/><Procedural type="PayStaminaCost"><ProceduralParams><CostID value="Test_Cost"/><CostMultiplier value="1"/><Condition value="Test_Stance_Active"/></ProceduralParams></Procedural></ProcLayer>
 </Fragment>
 <Fragment Tags="2H_Melee"><AnimLayer/><ProcLayer><Blend ExitTime="0.1"/><Procedural type="CAGE-Damage"><ProceduralParams><DamageKey value="x"/><DamageTableRow value="Generic_Row"/></ProceduralParams></Procedural></ProcLayer></Fragment>
</Attack_Primary>
<Attack_Primary_Strike2>
 <Fragment Tags="2H_Melee+TestAxe"><AnimLayer><Blend ExitTime="0" StartTime="0"/><Animation name="test_missing"/></AnimLayer></Fragment>
</Attack_Primary_Strike2>
</FragmentList></AnimDB>"""

COMMON = """<AnimDB><FragmentList>
<Attack_Start_Charge_C2>
 <Fragment Tags="2H_Melee"><AnimLayer/><ProcLayer><Blend ExitTime="0"/><Procedural type="CAGE-TransitionInstant"><ProceduralParams><Condition value="Attack_Heavy_NotHeld"/><NewFragment value="Attack_Primary_Strike2"/></ProceduralParams></Procedural></ProcLayer></Fragment>
</Attack_Start_Charge_C2>
<Attack_Heavy><Fragment Tags="2H_Melee"><AnimLayer/></Fragment></Attack_Heavy>
<Attack_Heavy_Charged><Fragment Tags="2H_Melee"><AnimLayer/></Fragment></Attack_Heavy_Charged>
</FragmentList></AnimDB>"""

ATTACHMENT = """<AnimDB><FragmentList>
<Attack_Primary>
 <Fragment Tags="2H_Melee+TestAxe">
  <ProcLayer><Blend ExitTime="0.3"/><Procedural type="CAGE-Damage"><ProceduralParams><DamageKey value="Atk1"/><DamageTableRow value="Test_Primary"/><MeleeAttackShapeCastType value="MeleeAttackShapeCastTypeCapsule"/><MeleeAttackShapeRadius value="0.25"/></ProceduralParams></Procedural>
   <Blend ExitTime="0.1"/><Procedural type=""><ProceduralParams/></Procedural></ProcLayer>
  <ProcLayer><Blend ExitTime="0.3"/><Procedural type="CAGE-Damage"><ProceduralParams><DamageKey value="atk1"/><DamageTableRow value="Test_Primary"/><MeleeAttackShapeCastType value="MeleeAttackShapeCastTypeBox"/></ProceduralParams></Procedural>
   <Blend ExitTime="0.15"/><Procedural type=""><ProceduralParams/></Procedural></ProcLayer>
  <ProcLayer><Blend ExitTime="0.3"/><Procedural type="CAGE-Damage"><ProceduralParams><DamageKey value="atk2"/><DamageTableRow value="Test_Primary_Pull"/><Condition value="TestPullNode"/></ProceduralParams></Procedural>
   <Blend ExitTime="0.1"/><Procedural type=""><ProceduralParams/></Procedural></ProcLayer>
  <ProcLayer><Blend ExitTime="0.3"/><Procedural type="CAGE-Damage"><ProceduralParams><DamageKey value="atk3"/><DamageTableRow value="Test_Odd"/><Condition value="TestStateCondition"/></ProceduralParams></Procedural></ProcLayer>
 </Fragment>
</Attack_Primary>
</FragmentList></AnimDB>"""

ACTIONS = """<ActionList>
<Action Name="Attack_Primary_C1" FragmentName="Attack_Primary"/>
<Action Name="Attack_Heavy" FragmentName="Attack_Heavy"/>
<Action Name="Attack_Heavy_Charged" FragmentName="Attack_Heavy_Charged"/>
<Action Name="Attack_Start_Charge_C2" FragmentName="Attack_Start_Charge_C2"/>
<Conditions>
 <And Name="TestPullNode"><IfAbilityActive AbilityName="Test_Pull"/><Not><IfAbilityActive AbilityName="Test_PullHold"/></Not></And>
 <And Name="TestStateCondition"><IfAbilityActive AbilityName="Test_Pull"/><IfMannequinTag TagName="Stance"/></And>
 <Or Name="TestEither"><IfAbilityActive AbilityName="Test_A"/><IfAbilityActive AbilityName="Test_B"/></Or>
 <Not Name="TestNeither"><Or><IfAbilityActive AbilityName="Test_A"/><IfAbilityActive AbilityName="Test_B"/></Or></Not>
</Conditions>
</ActionList>"""


class FakeSources:
    def __init__(self, files: dict[str, bytes]) -> None:
        self.files = files
        self.used: dict[str, int] = {}

    def has(self, path: str) -> bool:
        return path in self.files

    def read(self, path: str) -> bytes | None:
        return self.files.get(path)

    def paths(self, prefix: str, suffix: str) -> list[str]:
        return sorted(path for path in self.files if path.startswith(prefix) and path.endswith(suffix))

    def check_complete(self) -> None:
        pass


def chunk_file(frames: int, seconds: float) -> bytes:
    """A made-up animation chunk file: the header, one table entry, and the motion parameters chunk."""
    table = 16
    chunk = table + 16
    data = bytearray(b"CrCh" + struct.pack("<III", 0x746, 1, table))
    data += struct.pack("<HHIII", tool.MOTION_CHUNK, 0x925, 1, 132, chunk)
    data += struct.pack("<IIifii", 0, 500, 1, seconds, 0, frames) + bytes(108)
    return bytes(data)


def main() -> int:
    results: list[tuple[str, bool, str]] = []
    folder = tool.ANIMATION_FOLDER + "test/"
    files = {tool.BODY_FOLDER + "player_testaxe.adb": BODY.encode(), tool.BODY_FOLDER + "player_2h_melee.adb": COMMON.encode(),
             tool.ADB_FOLDER + "playermaleanims_2h_meleeattach.adb": ATTACHMENT.encode(),
             tool.ACTION_FOLDER + "playeractions_attack.actionlist": ACTIONS.encode(),
             folder + "test_swing.caf": chunk_file(55, 1.0 / 30.0), folder + "test_charge.caf": chunk_file(21, 1.0 / 30.0)}
    sources = FakeSources(files)
    body = tool.read_fragments(sources, tool.BODY_FOLDER + "player_testaxe.adb")
    common = tool.read_fragments(sources, tool.BODY_FOLDER + "player_2h_melee.adb")
    attachment = tool.read_fragments(sources, tool.ADB_FOLDER + "playermaleanims_2h_meleeattach.adb")
    weapon = {"2h_melee", "testaxe"}

    chosen = tool.select([body["Attack_Primary"], common.get("Attack_Primary", [])], weapon)
    generic = tool.select([[], body["Attack_Primary"][1:]], weapon)
    oil = tool.select([body["Attack_Start_Charge"][1:]], weapon)
    results.append(("variant", chosen is body["Attack_Primary"][0] and generic is body["Attack_Primary"][1] and oil is None,
                    "the weapon variant (2 tags) over the common one; the common one alone; a variant with FragTags is never chosen"))

    transitions = [clip for clip in body["Attack_Start_Charge"][0]["procedural"] if clip["type"] == "CAGE-TransitionInstant"]
    starts = [round(clip["start"], 6) for clip in transitions]
    ends = [None if clip["end"] is None else round(clip["end"], 6) for clip in transitions]
    results.append(("clock", starts == [0.0, 0.333333, 0.666667] and ends == [0.333333, 0.666667, None],
                    f"starts {starts}, ends {ends}"))

    animations = tool.Animations(sources)
    length = tool.fragment_length(body["Attack_Primary"][0], animations)
    expected = 0.43333334 + (55 / 30.0 - (0.1 + 0.43333334 * 0.96)) / 1.3
    charge = tool.fragment_length(body["Attack_Start_Charge"][0], animations)
    results.append(("length", abs(length - round(expected, 6)) < 1e-6 and abs(length - 1.446667) < 1e-3 and abs(charge - 0.7) < 1e-6,
                    f"two clips of one animation: {length} s (the war hammer: 44 input steps); one clip: {charge} s"))
    missing = tool.fragment_length(body["Attack_Primary_Strike2"][0], animations)
    results.append(("length unknown", missing == 0.0 and "test_missing" in animations.missing, f"length {missing}"))
    # ExitTime -1 (the sprint attack of the hatchet): the clip starts at the end of the clip before it; a last clip that loops has no end.
    sources.files[tool.BODY_FOLDER + "player_exit.adb"] = (
        '<AnimDB><FragmentList><Attack_Sprint>'
        '<Fragment Tags="2H_Melee+TestAxe"><AnimLayer><Blend ExitTime="0" StartTime="0"/><Animation name="test_charge"/>'
        '<Blend ExitTime="-1" StartTime="0"/><Animation name="test_swing"/></AnimLayer></Fragment>'
        '<Fragment Tags="1H_Melee+TestAxe"><AnimLayer><Blend ExitTime="0" StartTime="0"/><Animation name="test_charge"/>'
        '<Blend ExitTime="-1" StartTime="0"/><Animation name="test_swing" flags="Loop"/></AnimLayer></Fragment>'
        '</Attack_Sprint></FragmentList></AnimDB>').encode()
    exits = tool.read_fragments(sources, tool.BODY_FOLDER + "player_exit.adb")["Attack_Sprint"]
    after = tool.fragment_length(exits[0], animations)
    looped = tool.fragment_length(exits[1], animations)
    results.append(("length after the clip before", abs(after - round(21 / 30.0 + 55 / 30.0, 6)) < 1e-6 and looped == 0.0,
                    f"ExitTime -1: {after} s (0.7 s + 1.833 s, not a start at -1 s); a last clip that loops: {looped} (not known)"))
    results.append(("animation file", abs(tool.caf_length(chunk_file(30, 1.0 / 30.0), "x") - 1.0) < 1e-6
                    and and_fails(lambda: tool.caf_length(b"nope" + bytes(20), "x")),
                    "30 frames of 1/30 s: 1 s; a file that is not a chunk file: refused"))

    action_fragments, named = tool.read_actions(sources)
    tests: list[tuple[str, bool]] = []
    simple = tool.node_tests(named["testpullnode"], False, tests)
    neither: list[tuple[str, bool]] = []
    simple_not_or = tool.node_tests(named["testneither"], False, neither)
    results.append(("conditions", simple and tests == [("Test_Pull", False), ("Test_PullHold", True)] and simple_not_or
                    and neither == [("Test_A", True), ("Test_B", True)] and not tool.node_tests(named["testeither"], False, [])
                    and not tool.node_tests(named["teststatecondition"], False, []),
                    "And with Not: needs Test_Pull, forbids Test_PullHold; Not(Or): forbids both; an Or and a mannequin test: no"))

    not_exported: dict[str, int] = {}
    clips: list[list] = []
    counts = {"clips": 0, "lengthsNotKnown": 0, "transitions": 0, "strikes": 0, "payments": 0}
    built = tool.build_weapon(("2H_Melee", "TestAxe"), body, common, attachment, action_fragments, named, animations, not_exported, clips, counts)
    graph = built[0] if built else None
    by_name = {fragment["name"]: fragment for fragment in graph} if graph else {}
    primary = by_name.get("Attack_Primary", {})
    strikes = {strike[0]: strike for strike in primary.get("strikes", [])}
    ok = (graph is not None and list(by_name) == ["Attack_Start_Charge", "Attack_Primary", "Attack_Heavy", "Attack_Heavy_Charged",
                                                  "Attack_Start_Charge_C2", "Attack_Primary_Strike2"]
          and [t[3] for t in by_name["Attack_Start_Charge"]["transitions"]] == ["notHeld", "released", "always"]
          and by_name["Attack_Start_Charge_C2"]["transitions"][0][4] == "Attack_Primary_Strike2"
          and not_exported == {"CanAttack_Ability_Test": 1}
          and set(strikes) == {"atk1", "atk2"} and strikes["atk1"][1:3] == [0.3, 0.45] and len(strikes["atk1"][4]) == 2
          and strikes["atk2"][3] == [["Test_Primary_Pull", ["Test_Pull"], ["Test_PullHold"], 0]]
          and clips == [["2H_Melee+TestAxe", "Attack_Primary", "atk3", "Test_Odd", "the condition TestStateCondition is not an AND of node tests"]]
          and primary["payments"] == [[0.0, "stamina", "Test_Cost", 1.0, "Test_Stance_Active"]]
          and primary["dodge"] == [[0.0, 0.2666, False], [0.2666, 0.6333, True]] and primary["canTransition"] == 0.6333
          and primary["transitions"][0][:5] == ["buffered", 0.333333, 0.666667, "virtual", "Attack_Start_Charge_C2"]
          and primary["transitions"][0][6] is None)
    results.append(("graph", ok, f"fragments {list(by_name)}, not exported {not_exported}, strikes {sorted(strikes)}, clips not exported {len(clips)}"))
    results.append(("no graph", tool.build_weapon(("2H_Melee", "Other"), {}, {}, {}, action_fragments, named, animations, {}, [], counts) is None,
                    "a tag set with no Attack_Start_Charge: no graph"))

    broken = BODY.replace('<Condition value="CanAttack_Heavy"/><NewAction value="Attack_Start_Charge_C2"/>',
                          '<Condition value="CanAttack_Heavy"/><NewAction value="Attack_Nowhere"/>', 1)
    sources.files[tool.BODY_FOLDER + "player_broken.adb"] = broken.encode()
    broken_body = tool.read_fragments(sources, tool.BODY_FOLDER + "player_broken.adb")
    no_row = ATTACHMENT.replace('<DamageTableRow value="Test_Primary"/>', '<DamageTableRow value=""/>', 1)
    sources.files[tool.ADB_FOLDER + "broken_attach.adb"] = no_row.encode()
    broken_attach = tool.read_fragments(sources, tool.ADB_FOLDER + "broken_attach.adb")
    results.append(("refusals", and_fails(lambda: tool.build_weapon(("2H_Melee", "TestAxe"), broken_body, common, attachment, action_fragments,
                                                                    named, animations, {}, [], counts))
                    and and_fails(lambda: tool.build_weapon(("2H_Melee", "TestAxe"), body, common, broken_attach, action_fragments, named,
                                                            animations, {}, [], counts)),
                    "an action that no list has, and a damage clip with no row: the tool stops"))

    results.append(("abilities", *abilities_case()))
    results.append(("incomplete", *incomplete_sources()))

    failures = 0
    for name, passed, detail in results:
        print(f"{'ok' if passed else 'FAIL'} {name}: {detail}")
        failures += 0 if passed else 1
    print(f"weapon actions check: {failures} failed check(s)")
    return 1 if failures else 0


ABILITY_BODY = """<AnimDB><FragmentList>
<Attack_Start_Charge>
 <Fragment Tags="2H_Melee+GreatAxe"><AnimLayer/>
  <ProcLayer><Blend ExitTime="0"/><Procedural type="CAGE-TransitionInstant"><ProceduralParams><Condition value="Attack_Heavy_NotHeld"/><NewAction value="Attack_Primary_C1"/></ProceduralParams></Procedural></ProcLayer>
  <ProcLayer><Blend ExitTime="0.1"/><Procedural type="CAGE-TransitionBuffered"><ProceduralParams><Condition value="CanAttack_Whirlwind"/><NewAction value="Test_Whirlwind"/></ProceduralParams></Procedural>
   <Blend ExitTime="0.2"/><Procedural type=""><ProceduralParams/></Procedural></ProcLayer>
 </Fragment>
</Attack_Start_Charge>
<Attack_Primary><Fragment Tags="2H_Melee+GreatAxe"><AnimLayer/></Fragment></Attack_Primary>
<Ability_Test_Whirlwind>
 <Fragment Tags="2H_Melee+GreatAxe"><AnimLayer/>
  <ProcLayer><Blend ExitTime="1.9"/><Procedural type="CAGE-TransitionInstant"><ProceduralParams><Condition value="Test_LessThan_1"/><NewFragment value="Ability_Test_Exit"/></ProceduralParams></Procedural>
   <Blend ExitTime="0.4"/><Procedural type="CAGE-TransitionInstant"><ProceduralParams><Condition value=""/><NewFragment value="Ability_Test_Exit"/></ProceduralParams></Procedural></ProcLayer>
  <ProcLayer><Blend ExitTime="0.1"/><Procedural type="CAGE-TransitionInstant"><ProceduralParams><Condition value="Test_Hit_Or_Water"/><NewAction value="Test_FollowUp"/></ProceduralParams></Procedural>
   <Blend ExitTime="0.1"/><Procedural type="CAGE-TransitionInstant"><ProceduralParams><Condition value="Test_Unknown"/><NewFragment value="Ability_Test_Never"/></ProceduralParams></Procedural></ProcLayer>
  <ProcLayer><Blend ExitTime="0.5"/><Procedural type="CAGE-Named"><ProceduralParams><Name value="Test_SpellMarker"/></ProceduralParams></Procedural></ProcLayer>
 </Fragment>
</Ability_Test_Whirlwind>
<Ability_Test_Whirlwind_Upgrade><Fragment Tags="2H_Melee+GreatAxe"><AnimLayer/></Fragment></Ability_Test_Whirlwind_Upgrade>
<Ability_Test_Exit><Fragment Tags="2H_Melee+GreatAxe"><AnimLayer/></Fragment></Ability_Test_Exit>
<Ability_Test_FollowUp><Fragment Tags="2H_Melee+GreatAxe"><AnimLayer/></Fragment></Ability_Test_FollowUp>
</FragmentList></AnimDB>"""

ABILITY_ATTACHMENT = """<AnimDB><FragmentList>
<Ability_Test_Whirlwind>
 <Fragment Tags="2H_Melee+GreatAxe">
  <ProcLayer><Blend ExitTime="0.5"/><Procedural type="CAGE-Damage"><ProceduralParams><DamageKey value="Atk1"/><DamageTableRow value="Test_Pull_Near"/><OffsetZ value="1.2"/></ProceduralParams></Procedural>
   <Blend ExitTime="0.1"/><Procedural type=""><ProceduralParams/></Procedural></ProcLayer>
  <ProcLayer><Blend ExitTime="0.5"/><Procedural type="CAGE-Damage"><ProceduralParams><DamageKey value="Atk1"/><DamageTableRow value="Test_Pull_Far"/><OffsetZ value="4.3"/></ProceduralParams></Procedural>
   <Blend ExitTime="0.1"/><Procedural type=""><ProceduralParams/></Procedural></ProcLayer>
  <ProcLayer><Blend ExitTime="0.6"/><Procedural type="CAGE-CastSpell"><ProceduralParams><SpellName value="Test_Ground"/><Condition value=""/></ProceduralParams></Procedural></ProcLayer>
 </Fragment>
</Ability_Test_Whirlwind>
</FragmentList></AnimDB>"""

ABILITY_ACTIONS = """<ActionList>
<Action Name="Test_Whirlwind" FragmentName="Ability_Test_Whirlwind"><Activities>
 <IfThenElse><IfCondition><Not><IfExternalCondition ConditionName="Ability_Startup"/></Not></IfCondition><ThenActivity><Group>
  <SetCooldownTimer><Default AbilityID="Ability_Test_Whirlwind"/></SetCooldownTimer></Group></ThenActivity></IfThenElse>
 <IfThenElse><IfCondition><And><Not><IfFragmentPlaying FragmentName="Ability_Test_Exit"/></Not><IfAbilityActive AbilityName="Test_MoreSpins"/>
   <Not><IfAbilityActive AbilityName="Test_ArtifactPerk" External="true"/></Not></And></IfCondition>
  <ThenActivity><ChangeActionFragment><Default UseDefaultFragment="false" FragmentName="Ability_Test_Whirlwind_Upgrade"/></ChangeActionFragment></ThenActivity></IfThenElse>
 <IfThenElse><IfCondition><And><IfAbilityActive AbilityName="Test_Shield"/><IfMannequinMarker MarkerName="Test_SpellMarker"/></And></IfCondition>
  <ThenActivity><CastSpell><Default SpellName="Test_Shield_Spell"/></CastSpell></ThenActivity></IfThenElse>
</Activities></Action>
<Action Name="Test_FollowUp" FragmentName="Ability_Test_FollowUp"/>
<Conditions>
 <And Name="Test_LessThan_1"><IfAbilityNumHits AbilityName="Ability_Test_Whirlwind" NumHits="1" ComparisonType="LessThan"/></And>
 <And Name="Test_Hit_Or_Water"><Or><IfInput InputName="character_collided"/><IfWaterDepth Comparison="GreaterThan" WaterDepth="Shallow"/></Or>
  <Not><IfInput InputName="attack_primary_virtual"/></Not><IfAbilityActive AbilityName="Test_Node"/></And>
 <And Name="Test_Unknown"><IfHasStatusEffect StatusEffect="Test_Status"/></And>
</Conditions>
</ActionList>"""


def abilities_case() -> tuple[bool, str]:
    """Format 2: the ability fragments of a weapon (stage 3.3b-M2) with made-up files."""
    files = {tool.BODY_FOLDER + "test.adb": ABILITY_BODY.encode(), tool.ADB_FOLDER + "attach.adb": ABILITY_ATTACHMENT.encode(),
             tool.ACTION_FOLDER + "playeractions_attack.actionlist": ACTIONS.encode(),
             tool.ACTION_FOLDER + "playeractions_ability_greataxe.actionlist": ABILITY_ACTIONS.encode()}
    sources = FakeSources(files)
    body = tool.read_fragments(sources, tool.BODY_FOLDER + "test.adb")
    attachment = tool.read_fragments(sources, tool.ADB_FOLDER + "attach.adb")
    action_fragments, named = tool.read_actions(sources)
    abilities = tool.read_abilities(sources)
    animations = tool.Animations(sources)
    not_exported: dict[str, int] = {}
    counts = {"clips": 0, "lengthsNotKnown": 0, "transitions": 0, "strikes": 0, "payments": 0}
    built = tool.build_weapon(("2H_Melee", "GreatAxe"), body, {}, attachment, action_fragments, named, animations, not_exported, [], counts,
                              abilities)
    if built is None:
        return False, "no graph"
    fragments, entries, uses = built
    by_name = {fragment["name"]: fragment for fragment in fragments}
    entry = entries[0] if entries else {}
    whirlwind = by_name.get("Ability_Test_Whirlwind", {})
    start = by_name.get("Attack_Start_Charge", {})
    terms = {transition[5]: transition[6] for transition in whirlwind.get("transitions", [])}
    strike = whirlwind.get("strikes", [[None, 0, 0, [], []]])[0]
    ok = (entry.get("ability") == "Ability_Test_Whirlwind" and entry.get("fragment") == "Ability_Test_Whirlwind"
          and entry.get("selections") == [["Ability_Test_Whirlwind_Upgrade", ["Test_MoreSpins"], [], [], ["Test_ArtifactPerk"]]]
          and entry.get("casts") == [["Test_Shield_Spell", ["Test_Shield"], [], [], [], "Test_SpellMarker"]]
          and any(t[3] == "ability" and t[4] == "Ability_Test_Whirlwind" and t[6] == "Ability_Test_Whirlwind" for t in start.get("transitions", []))
          and terms.get("Test_LessThan_1") == [[["hits", 1, False]]] and terms.get("") == [[]]
          and terms.get("Test_Hit_Or_Water") == [[["input", "collided", False], ["input", "attack", True], ["node", "Test_Node", False]]]
          and "Test_Unknown" not in terms and not_exported.get("Test_Unknown") == 1
          and set(by_name) >= {"Ability_Test_Whirlwind", "Ability_Test_Whirlwind_Upgrade", "Ability_Test_Exit", "Ability_Test_FollowUp"}
          and "Ability_Test_Never" not in by_name
          and whirlwind.get("markers") == [["Test_SpellMarker", 0.5]] and whirlwind.get("casts") == [[0.6, "Test_Ground", [], []]]
          and [row[3] for row in strike[3]] == [0, 1] and [shape[8] for shape in strike[4]] == [1.2, 4.3]
          and ["Test_MoreSpins", "selection", "Test_Whirlwind Ability_Test_Whirlwind_Upgrade"] in uses
          and ["Test_Node", "transition", "Ability_Test_Whirlwind -> Ability_Test_FollowUp"] in uses
          and ["Test_Shield", "cast", "Test_Whirlwind Test_Shield_Spell"] in uses)
    return ok, (f"ability {entry.get('ability')}, selections {entry.get('selections')}, casts {entry.get('casts')}, terms {terms}, "
                f"not exported {not_exported}, fragments {sorted(by_name)}")


def incomplete_sources() -> tuple[bool, str]:
    """The whole tool with made-up sources (only the source transport and the tag sets are replaced): a missing fragment of the graph
    and an empty attachment file stop the tool with status 1 and keep the old file; the complete sources write a new file."""
    import json
    import os
    import tempfile
    body = BODY.replace("TestAxe", "GreatAxe")
    files = {tool.BODY_FOLDER + "player_greataxe.adb": body.encode(), tool.BODY_FOLDER + "player_2h_melee.adb": COMMON.encode(),
             tool.ADB_FOLDER + "playermaleanims_2h_meleeattach.adb": ATTACHMENT.replace("TestAxe", "GreatAxe").encode(),
             tool.ACTION_FOLDER + "playeractions_attack.actionlist": ACTIONS.encode(),
             tool.ANIMATION_FOLDER + "test/test_swing.caf": chunk_file(55, 1.0 / 30.0),
             tool.ANIMATION_FOLDER + "test/test_charge.caf": chunk_file(21, 1.0 / 30.0)}
    parsed = ElementTree.fromstring(body)
    groups = parsed.find("FragmentList")
    groups.remove(groups.find("Attack_Primary"))
    variants = {"complete": dict(files),
                "missing fragment": {**files, tool.BODY_FOLDER + "player_greataxe.adb": ElementTree.tostring(parsed)},
                "empty attachment": {**files, tool.ADB_FOLDER + "playermaleanims_2h_meleeattach.adb": b"<AnimDB><FragmentList/></AnimDB>"}}
    saved = (tool.Sources, tool.weapon_tag_sets, sys.argv, sys.stdout, sys.stderr)
    details = []
    passed = True
    try:
        with tempfile.TemporaryDirectory() as folder:
            for case, sources in variants.items():
                target = Path(folder) / "weapon-actions.json"
                target.write_text("old file\n")
                tool.Sources = lambda *_, files=sources: FakeSources(dict(files))
                tool.weapon_tag_sets = lambda _: [("2H_Melee", "GreatAxe")]
                sys.argv = ["weapon_actions.py", str(target)]
                with open(os.devnull, "w") as quiet:
                    sys.stdout, sys.stderr = quiet, quiet
                    status = tool.main()
                    sys.stdout, sys.stderr = saved[3], saved[4]
                text = target.read_text()
                kept = text == "old file\n"
                good = (status == 0 and not kept and json.loads(text)["counts"]["strikes"] > 0) if case == "complete" else (status == 1 and kept)
                passed = passed and good
                details.append(f"{case}: status {status}, old file {'kept' if kept else 'replaced'}")
    finally:
        tool.Sources, tool.weapon_tag_sets, sys.argv, sys.stdout, sys.stderr = saved
    return passed, "; ".join(details)


def and_fails(call) -> bool:
    try:
        call()
    except (Failure, struct.error, ElementTree.ParseError):
        return True
    return False


if __name__ == "__main__":
    sys.exit(main())
