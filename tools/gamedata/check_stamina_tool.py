#!/usr/bin/env python3
"""Check of the stamina data tool (stamina.py, stage 3.4f-B), with made-up sources.

The check runs the readers of the dodge action, the exhausted action, the dodge fragments, the Stamina component, and the writer of the
tool (main) with made-up action lists, animation database, data tables, and player slice. No file of the game is necessary.

  case                 the sources                                                   expected result
  valid                the retail form (a BackNav variant with CanTransition at      status 0: the classes, the costs 45, 45, 50, the
                       0.500221 s)                                                   times 0.1, 0 to 0.5, 0.5, the windows of a second dodge
  nonfinite-time       an ExitTime "nan" before the mark PayStaminaCost              status 1, the file stays
  malformed-time       an ExitTime "0.1x"                                            status 1, the file stays
  missing-cost         the cost table has no row Dodge_Roll_Slow                     status 1, the file stays
  other-variant        a standing variant pays at 0.2 s                              status 1, the file stays
  no-component         the player slice has no Stamina component                     status 1, the file stays
  exhausted-part       the action Exhausted ends at 0.5 of the maximum               status 1, the file stays

Usage: check_stamina_tool.py WORK_FOLDER     status 0: each case gives its result
"""

from __future__ import annotations

import contextlib
import io
import json
import struct
import sys
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import stamina as tool  # noqa: E402
from appearance_tables import Element, field_id  # noqa: E402

CASES = ("valid", "nonfinite-time", "malformed-time", "missing-cost", "other-variant", "no-component", "exhausted-part")
MARK = "the file of an earlier run\n"
STATE = {"case": "valid"}


def dodge_actions() -> str:
    return """<ActionList><Actions><Action Name="Dodge" FragmentName="Dodge_Roll"><Activities>
<IfThenElse><ThenActivity><ChangeActionFragment><Default UseDefaultFragment="false" FragmentName="Dodge_Roll"/>
<Conditional Name="FastRoll" UseDefaultFragment="false" FragmentName="Dodge_Roll_Fast"/>
<Conditional Name="SlowRoll" UseDefaultFragment="false" FragmentName="Dodge_Roll_Slow"/></ChangeActionFragment></ThenActivity></IfThenElse>
<IfThenElse><ThenActivity><DisableStaminaRegen><Default OnEnter="Disable" OnExit="Enable"/></DisableStaminaRegen>
<SetStaminaRegenDelay><Default OnEnter="NoEffect" OnExit="Set" RegenDelayTime="1.00"/></SetStaminaRegenDelay></ThenActivity></IfThenElse>
<IfThenElse><ThenActivity><PayStaminaCost><Default CostID="Dodge_Roll" DisableRegenWhileActive="true"/>
<Conditional Name="FastRoll+SpearReducedDodgeStaminaActive" CostID="Dodge_Roll_Fast" CostMultiplier="0.8"/>
<Conditional Name="FastRoll" CostID="Dodge_Roll_Fast"/><Conditional Name="SlowRoll" CostID="Dodge_Roll_Slow"/></PayStaminaCost>
</ThenActivity></IfThenElse></Activities></Action></Actions>
<Conditions><ActionConditionEquipLoadCategory Name="FastRoll" Value="Fast"/><ActionConditionEquipLoadCategory Name="SlowRoll" Value="Slow"/>
</Conditions></ActionList>"""


def global_actions() -> str:
    exit_part = "0.50" if STATE["case"] == "exhausted-part" else "1.00"
    return f"""<ActionList><Actions><Action Name="Exhausted"><ActivationCondition><And><Or><And>
<IfStaminaValue ComparisonValue="0.00" ComparisonValueIsPercentage="true" ComparisonType="LessThanOrEqual"/></And></Or></And>
</ActivationCondition><DeactivationCondition>
<IfStaminaValue ComparisonValue="{exit_part}" ComparisonValueIsPercentage="true" ComparisonType="Equal"/></DeactivationCondition>
</Action></Actions></ActionList>"""


def fragment(tags: str, pay: str = "0.1", transition: str = "0.5") -> str:
    case = STATE["case"]
    if case == "nonfinite-time" and tags == "":
        pay = "nan"
    if case == "malformed-time" and tags == "":
        pay = "0.1x"
    return f"""<Fragment Tags="{tags}"><AnimLayer><Blend ExitTime="0"/><Animation name="roll"/></AnimLayer>
<ProcLayer><Blend ExitTime="{transition}"/><Procedural type="CAGE-Named"><ProceduralParams><Name value="CanTransition"/></ProceduralParams></Procedural></ProcLayer>
<ProcLayer><Blend ExitTime="0.266667"/><Procedural type="CAGE-TransitionBuffered"><ProceduralParams><Condition value="CanDodgeCancel"/></ProceduralParams></Procedural>
<Blend ExitTime="0.233333"/><Procedural type="CAGE-TransitionInstant"><ProceduralParams><Condition value="CanDodgeCancel"/></ProceduralParams></Procedural></ProcLayer>
<ProcLayer><Blend ExitTime="{pay}"/><Procedural type="CAGE-Named"><ProceduralParams><Name value="PayStaminaCost"/></ProceduralParams></Procedural></ProcLayer>
<ProcLayer><Blend ExitTime="0"/><Procedural type="Invulnerability"><ProceduralParams><ShowImmuneOnHit value="true"/></ProceduralParams></Procedural>
<Blend ExitTime="0.5"/><Procedural type="" contextType=""/></ProcLayer></Fragment>"""


def database() -> str:
    groups = []
    for name in ("Dodge_Roll", "Dodge_Roll_Fast", "Dodge_Roll_Slow"):
        variants = [fragment(""), fragment("LeftNav"), fragment("BackNav", transition="0.500221"),
                    fragment("Prone+RightNav", transition="0.766667")]
        if STATE["case"] == "other-variant" and name == "Dodge_Roll":
            variants.append(fragment("RightNav", pay="0.2"))
        groups.append(f"<{name}>{''.join(variants)}</{name}>")
    return f"<AnimDB><FragmentList>{''.join(groups)}</FragmentList></AnimDB>"


def node(name=None, value=None, children=()):
    element = Element()
    element.name = name
    element.value = value
    element.children = list(children)
    return element


def player_roots() -> list:
    values = {"m_min": 0.0, "m_initMax": 100.0, "m_regenRate": 33.33, "m_blockRegenRate": 10.0, "m_maxWindedTime": 2.0}
    if STATE["case"] == "no-component":
        values.pop("m_maxWindedTime")
    component = node(children=[node(field_id(label), struct.pack(">f", value)) for label, value in values.items()])
    return [node(children=[component])]


class Sheet:
    def __init__(self, name: str, kind: str, rows: list[dict]) -> None:
        self.name = name
        self.type = kind
        self.rows = rows
        self.row_count = len(rows)
        self.columns = {column: index for index, column in enumerate(sorted({key for row in rows for key in row}))}

    def cell(self, row: int, column: str):
        return self.rows[row].get(column, "")


def sheets() -> dict:
    costs = [{"CostID": "Dodge_Roll", "StaminaCost": 45.0}, {"CostID": "Dodge_Roll_Fast", "StaminaCost": 45.0}]
    if STATE["case"] != "missing-cost":
        costs.append({"CostID": "Dodge_Roll_Slow", "StaminaCost": 50.0})
    load = {"ContainerTypeID": "Player"}
    for suffix in ("Fast", "Normal", "Slow", "Overburdened"):
        load[f"EquipLoadStaminaCostMult{suffix}"] = 1.0
        load[f"EquipLoadStaminaRegenMult{suffix}"] = 1.0
    return {tool.VITALS_TABLE: Sheet(tool.VITALS_TABLE, tool.VITALS_TYPE, [{"VitalsID": "Player", "StaminaRegenDelay": 1.0,
                                                                          "StaminaRegenDelayOnEmpty": 1.0,
                                                                          "StaminaFullyDepletedDelay": 1.0}]),
            tool.STAMINA_TABLE: Sheet(tool.STAMINA_TABLE, tool.STAMINA_TYPE, costs),
            "EncumbranceLimits": Sheet("EncumbranceLimits", tool.LOAD_TYPE, [load])}


class Sources:
    used: dict = {}

    def __init__(self, *arguments) -> None:
        pass

    def read(self, path: str) -> bytes | None:
        texts = {tool.DODGE_ACTIONS: dodge_actions(), tool.GLOBAL_ACTIONS: global_actions(), tool.PLAYER_ADB: database()}
        return texts[path].encode() if path in texts else None

    def stream(self, path: str) -> list | None:
        return player_roots() if path == tool.PLAYER_SLICE else None

    def check_complete(self) -> None:
        pass


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    work = Path(sys.argv[1])
    work.mkdir(parents=True, exist_ok=True)
    failures = 0
    for case in CASES:
        STATE["case"] = case
        target = work / f"{case}.json"
        target.write_text(MARK, encoding="utf-8")
        errors = io.StringIO()
        with patch.object(tool, "Sources", Sources), patch.object(tool, "read_sheets", lambda _: sheets()), \
                patch.object(sys, "argv", ["stamina.py", str(target)]), contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(errors):
            status = tool.main()
        text = target.read_text(encoding="utf-8")
        if case == "valid":
            document = json.loads(text) if text != MARK else {}
            dodge = document.get("dodge", {})
            classes = {klass: (entry["fragment"], entry["cost"]) for klass, entry in dodge.get("classes", {}).items()}
            times = dodge.get("fragments", {}).get("Dodge_Roll", {})
            passed = (status == 0 and classes == {"light": ("Dodge_Roll_Fast", 45.0), "medium": ("Dodge_Roll", 45.0),
                                                  "heavy": ("Dodge_Roll_Slow", 50.0)}
                      and times == {"pay": 0.1, "immune": [0.0, 0.5], "canTransition": 0.5,
                                    "cancel": [[0.266667, 0.5, True], [0.5, None, False]]}
                      and document["component"] == {"min": 0.0, "max": 100.0, "regenRate": 33.330002, "blockRegenRate": 10.0,
                                                    "maxWindedTime": 2.0}
                      and dodge["regenDelayOnExit"] == 1.0 and len(dodge["costsNotApplied"]) == 1
                      and document["exhausted"] == {"action": "Exhausted", "enter": 0.0, "exit": 1.0})
            detail = f"status {status}, classes {classes}, times {times}"
        else:
            passed = status == 1 and text == MARK and not (work / f"{case}.json.tmp").exists()
            detail = f"status {status}, file {'kept' if text == MARK else 'replaced'}: {errors.getvalue().strip()[:160]}"
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {detail}")
        failures += 0 if passed else 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
