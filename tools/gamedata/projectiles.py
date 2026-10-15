#!/usr/bin/env python3
"""Write the projectile data of the game as one JSON file (stage 3.3b-S3).

The backend reads this file at its start (protocol notes 4.27). It has what the server needs to accept the shot of a
player and to follow its projectile: the ranged attack profiles (the range, the radius of the projectile, the range of
the hit near the muzzle, the pierce, bounce, and fall rules), the flight values of each projectile slice, the
ammunition, the profile of each weapon, the shots that the basic attacks of a weapon make (the joins of the attack, the
damage row, the charge, the projectile slice, the spell, and the mana cost). The collision volume of a creature (the
capsule of its character) is in the creature data (creature_data.py, the member "capsule" of a slice).

  Source                                           What the file gets
  sharedassets/genericassets/                      "attacks": for each profile and attack of the ranged attack data
  rangedattackdatabase.radb                        base: m_isHitScan, m_maxRange, m_projectileRadius,
                                                   m_projectileHitScanRange (the hit near the muzzle),
                                                   m_projectilePenetrationDepth, m_projectileAccelDelayDist,
                                                   m_projectileDamageFalloffDist, m_distForProjectileDamageFalloffMax,
                                                   m_projectileDamageMaxFalloff, m_bounceCountMax,
                                                   m_destroyProjectileOnBlock, m_destroyProjectileOnUnsuccesfulHit,
                                                   m_predictedProjectileAccel, and the number of directions of the
                                                   shotgun configuration of m_spreadshotConfig (m_numRings x
                                                   m_numSectorsPerRing x m_numShotsPerSector: the grouped spread of the
                                                   blunderbuss; 0: no spread)
  the slice of each projectile (an AmmoPrefabPath  "slices": m_baseSpeed, m_extraSpeed, m_timeForMaxSpeed, m_accel,
  of the ammunition, a SpellPrefabPath of a spell) m_flyingLifetime, m_stuckLifetime of its projectile component
  AmmoItemDefinitions                              "ammo": AmmoID, AmmoType, DamageModifier, the slice of AmmoPrefabPath
  WeaponItemDefinitions                            "weapons": WeaponID, RangedAttackProfile, AmmoType (the weapons with a
                                                   profile of the ranged attack data)
  the animation databases of the player            "shots": each CAGE-RangedAttack clip (RangedAttackName,
  (animations/mannequin/adb/**.adb, XML)           DamageTableRow, ChargeValue, AmmoSlot, ConsumeAmmo, SliceOverride)
                                                   whose damage row is a light or heavy row of the player damage table
                                                   with the weapon category of a weapon of the profile
  SpellData                                        "shots": each spell with the casting type Projectile whose
                                                   RangedAttackProfile is the profile of a weapon and whose
                                                   DamageTableRow is the light or the heavy row of that weapon
                                                   (TooltipPrimaryAttackData, TooltipAlternateAttackData): the spell,
                                                   its RangedAttackName, its row, its slice, and its ManaCost id
  DamageData (the table DamageTable)               the weapon category and the attack type of each damage row

Output: {"format": 1, "counts": {...},
"attacks": [[profile, attack, hitScan, maxRange, radius, nearRange, penetration, accelDelay, falloffStart, falloffEnd,
             falloffMost, bounces, destroyOnBlock, destroyOnMiss, [x, y, z] predicted acceleration, spreadDirections], ...],
"slices": [[path, baseSpeed, extraSpeed, timeForMaxSpeed, [x, y, z] acceleration, flyingLifetime, stuckLifetime], ...],
"ammo": [[id, type, damageModifier, slice], ...],
"weapons": [[weaponId, profile, ammoType], ...],
"shots": [[profile, attack, row, charge, slice, spell, manaCost, heavy, ammoSlot, consume(, ability)], ...]}.
A shot with the 11th value `ability` true (stage 3.3b-S2b) is a projectile spell of an ability of a player weapon (a Projectile
spell of a player profile whose row has the attack type Ability or MagicSpell: the shot of the Pylon, the Beacon, the trap of
the musket): only the abilities system of the backend launches it.
`slice` is an index into `slices`; -1 in a shot: the slice of the ammunition of the shot. `spell` is empty for a clip of
an animation database. `heavy` is true for a row with the attack type Heavy. A clip whose damage row is not a light or a
heavy row of the player damage table (a clip of a creature, of an ability) is not a shot of the file. The tool stops with
status 1 and writes no file when a file that it needs has no source, when a table has no column that the tool reads, when
a number of the ranged attack data or of a projectile slice is not a finite number of its size, when a ChargeValue of a
clip is not a number, when a SliceOverride of a clip or the prefab of a spell shot names no slice with a projectile component,
when a player weapon profile (PLAYER_PROFILES) has no shot of its light or heavy row, and when the ammunition of an empty slot
of a weapon whose shot takes the slice of its ammunition has no slice (code review of stage 3.3b-S3, finding F7;
tools/gamedata/check_projectile_tool.py). An ammunition prefab that names no slice is -1 (the siege and creature ammunition of
the game copy: 9 rows): no shot of a player takes it.

The tool needs only the standard library of Python and the modules of this folder.

Usage: projectiles.py [OUTPUT.json]
       projectiles.py --find TEXT...   print the attacks, the shots, and the weapons with a text (no file)
Default output: <SOULWARDEN_GAMEDATA_DIR>/projectile-data.json
Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game),
             SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata),
             SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR (see creature_data.py)
"""

from __future__ import annotations

import json
import math
import os
import struct
import sys
from pathlib import Path
from xml.etree import ElementTree

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure, field_id  # noqa: E402
from creature_data import TABLES_FOLDER, Sources, walk  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402

OUTPUT_NAME = "projectile-data.json"
RADB_PATH = "sharedassets/genericassets/rangedattackdatabase.radb"
ADB_FOLDER = "animations/mannequin/adb/"
WANTED = (TABLES_FOLDER, "slices/", "sharedassets/genericassets/", ADB_FOLDER)
AMMO_TYPE = "AmmoItemDefinitions"
WEAPON_TYPE = "WeaponItemDefinitions"
SPELL_TYPE = "SpellData"
DAMAGE_TYPE = "DamageData"
# The damage table of the weapons of a player. The game has more tables of this type: one for each creature family.
DAMAGE_TABLE = "DamageTable"
USED_TYPES = (AMMO_TYPE, WEAPON_TYPE, SPELL_TYPE, DAMAGE_TYPE)
AMMO_COLUMNS = ("AmmoID", "AmmoType", "DamageModifier", "AmmoPrefabPath")
WEAPON_COLUMNS = ("WeaponID", "RangedAttackProfile", "AmmoType", "TooltipPrimaryAttackData", "TooltipAlternateAttackData")
SPELL_COLUMNS = ("SpellID", "SpellPrefabPath", "CastingTypes", "RangedAttackProfile", "RangedAttackName", "DamageTableRow",
                 "ManaCost")
DAMAGE_COLUMNS = ("DamageID", "WeaponCategory", "AttackType")
# The attack types of the rows of a basic attack (the damage table): a row of an ability ("Ability") is not a basic shot.
BASIC_ATTACK_TYPES = ("Light", "Heavy")
# The attack types of the rows of the abilities (stage 3.3b-S2b: the projectile spells of an ability; the rows of the magic
# abilities have MagicSpell: IceMagic_Pylon).
ABILITY_ATTACK_TYPES = ("Ability", "MagicSpell")

# The ranged attack data (the field names of the client: research abilities-shared.md Q3, abilities-ranged.md Q2).
PROFILES_FIELD = field_id("m_rangedAttackProfiles")
ATTACKS_FIELD = field_id("m_rangedAttackData")
KEY_FIELD = field_id("value1")
VALUE_FIELD = field_id("value2")
ATTACK_REALS = ("m_maxRange", "m_projectileRadius", "m_projectileHitScanRange", "m_projectilePenetrationDepth",
                "m_projectileAccelDelayDist", "m_projectileDamageFalloffDist", "m_distForProjectileDamageFalloffMax",
                "m_projectileDamageMaxFalloff")
ATTACK_WHOLES = ("m_isHitScan", "m_bounceCountMax", "m_destroyProjectileOnBlock", "m_destroyProjectileOnUnsuccesfulHit")
PREDICTED_ACCEL = "m_predictedProjectileAccel"
SPREAD_CONFIG = "m_spreadshotConfig"
SPREAD_COUNTS = ("m_numRings", "m_numSectorsPerRing", "m_numShotsPerSector")
# The projectile component of a slice (registration at image RVA 0x68F2BA0: m_baseSpeed +0x100, m_extraSpeed +0x104,
# m_timeForMaxSpeed +0x108).
SLICE_REALS = ("m_baseSpeed", "m_extraSpeed", "m_timeForMaxSpeed", "m_flyingLifetime", "m_stuckLifetime")
SLICE_ACCEL = "m_accel"
# The paperdoll slots of the ammunition (client enum PaperdollSlotTypes at image RVA 0x5D19E40).
AMMO_SLOTS = {"PaperdollSlotTypes::ARROW_AMMO_SLOT": 12, "PaperdollSlotTypes::CARTRIDGE_AMMO_SLOT": 13}


def slice_path(prefab: str) -> str:
    return "slices/" + prefab.strip().replace("\\", "/").lower().lstrip("/") + ".dynamicslice"


def child(element, wanted: int):
    return next((node for node in element.children if node.name == wanted), None)


def first(element, wanted: int):
    return next((node for node in walk(element) if node is not element and node.name == wanted), None)


def text_of(element) -> str:
    return element.value.decode("utf-8", "replace") if element is not None and element.value is not None else ""


def real_of(element, what: str) -> float:
    """@return The 32-bit float of a field. A field with a different size, or a value that is not finite, stops the tool."""
    if element is None or element.value is None or len(element.value) != 4:
        size = "no value" if element is None or element.value is None else f"{len(element.value)} bytes"
        raise Failure(f"{what} is not a 32-bit float ({size})")
    value = struct.unpack(">f", element.value)[0]
    if not math.isfinite(value):
        raise Failure(f"{what} is not a finite number")
    return round(value, 6)


def whole_of(element, what: str) -> int:
    if element is None or element.value is None or not 1 <= len(element.value) <= 8:
        raise Failure(f"{what} is not a whole number")
    return int.from_bytes(element.value, "big")


def vector_of(element, what: str) -> list[float]:
    """@return The three floats of an AZ::Vector3 (12 bytes, big-endian)."""
    if element is None or element.value is None or len(element.value) not in (12, 16):
        raise Failure(f"{what} is not a vector of three floats")
    values = struct.unpack(">3f", element.value[:12])
    if not all(math.isfinite(value) for value in values):
        raise Failure(f"{what} has a number that is not finite")
    return [round(value, 6) for value in values]


def read_attacks(roots: list) -> list[list]:
    """@return One row for each attack of each profile of the ranged attack data."""
    container = next((first(root, PROFILES_FIELD) for root in roots if first(root, PROFILES_FIELD) is not None), None)
    if container is None:
        raise Failure(f"{RADB_PATH} has no m_rangedAttackProfiles")
    rows = []
    for pair in container.children:
        profile = text_of(child(pair, KEY_FIELD))
        data = child(pair, VALUE_FIELD)
        attacks = first(data, ATTACKS_FIELD) if data is not None else None
        if not profile or attacks is None:
            raise Failure(f"{RADB_PATH}: a profile has no name or no m_rangedAttackData")
        for entry in attacks.children:
            name = text_of(child(entry, KEY_FIELD))
            values = child(entry, VALUE_FIELD)
            if not name or values is None:
                raise Failure(f"{RADB_PATH}: an attack of the profile {profile} has no name or no data")
            where = f"{RADB_PATH}: {profile}/{name}"
            reals = {field: real_of(child(values, field_id(field)), f"{where} {field}") for field in ATTACK_REALS}
            wholes = {field: whole_of(child(values, field_id(field)), f"{where} {field}") for field in ATTACK_WHOLES}
            predicted = vector_of(child(values, field_id(PREDICTED_ACCEL)), f"{where} {PREDICTED_ACCEL}")
            spread = child(values, field_id(SPREAD_CONFIG))
            directions = 0
            if spread is not None:
                counts = [first(spread, field_id(field)) for field in SPREAD_COUNTS]
                if all(count is not None for count in counts):
                    directions = math.prod(whole_of(count, f"{where} {field}") for count, field in zip(counts, SPREAD_COUNTS))
            rows.append([profile, name, bool(wholes["m_isHitScan"]), reals["m_maxRange"], reals["m_projectileRadius"],
                         reals["m_projectileHitScanRange"], reals["m_projectilePenetrationDepth"],
                         reals["m_projectileAccelDelayDist"], reals["m_projectileDamageFalloffDist"],
                         reals["m_distForProjectileDamageFalloffMax"], reals["m_projectileDamageMaxFalloff"],
                         wholes["m_bounceCountMax"], bool(wholes["m_destroyProjectileOnBlock"]),
                         bool(wholes["m_destroyProjectileOnUnsuccesfulHit"]), predicted, directions])
    if not rows:
        raise Failure(f"{RADB_PATH} has no attack")
    return rows


def read_projectile_slice(path: str, roots: list) -> list | None:
    """@return The flight values of the projectile component of a slice, or nothing when the slice has none."""
    found = {}
    for root in roots:
        for node in walk(root):
            for field in (*SLICE_REALS, SLICE_ACCEL):
                if field not in found and node.name == field_id(field):
                    found[field] = node
    if "m_baseSpeed" not in found:
        return None
    for field in (*SLICE_REALS, SLICE_ACCEL):
        if field not in found:
            raise Failure(f"{path}: the projectile component has no {field}")
    reals = {field: real_of(found[field], f"{path} {field}") for field in SLICE_REALS}
    return [path, reals["m_baseSpeed"], reals["m_extraSpeed"], reals["m_timeForMaxSpeed"],
            vector_of(found[SLICE_ACCEL], f"{path} {SLICE_ACCEL}"), reals["m_flyingLifetime"], reals["m_stuckLifetime"]]


def read_clips(data: bytes, path: str) -> list[dict]:
    """@return Each CAGE-RangedAttack clip of an animation database: its parameters, its fragment group, and its tags."""
    try:
        tree = ElementTree.fromstring(data)
    except ElementTree.ParseError as error:
        raise Failure(f"{path}: not an XML animation database: {error}") from error
    clips = []
    for group in tree.iter():
        for fragment in group.findall("Fragment"):
            for procedural in fragment.iter("Procedural"):
                if procedural.get("type") != "CAGE-RangedAttack":
                    continue
                params = procedural.find("ProceduralParams")
                if params is None:
                    continue
                values = {item.tag: item.get("value", "") for item in params}
                values["group"] = group.tag
                values["tags"] = fragment.get("Tags", "")
                clips.append(values)
    return clips


def read_sheets(sources: Sources) -> dict[str, list[Sheet]]:
    sheets: dict[str, list[Sheet]] = {kind: [] for kind in USED_TYPES}
    for path in sources.paths(TABLES_FOLDER, ".datasheet"):
        data = sources.read(path)
        if data is None:
            continue
        kind = sheet_type(data)
        if kind in sheets:
            try:
                sheets[kind].append(Sheet(data))
            except (struct.error, ValueError, IndexError) as error:
                raise Failure(f"{path}: the table did not parse: {error}") from error
    sources.check_complete()
    for kind, columns in ((AMMO_TYPE, AMMO_COLUMNS), (WEAPON_TYPE, WEAPON_COLUMNS), (SPELL_TYPE, SPELL_COLUMNS),
                          (DAMAGE_TYPE, DAMAGE_COLUMNS)):
        if not sheets[kind]:
            raise Failure(f"the game copy has no data table of the type {kind}")
        for sheet in sheets[kind]:
            if kind == DAMAGE_TYPE and sheet.name != DAMAGE_TABLE:
                continue
            for column in columns:
                if column not in sheet.columns:
                    raise Failure(f"the table {sheet.name} has no column {column}")
    return sheets


# The ammunition of an empty slot of each ammunition type (client code: the initializers at image RVA 0x221E50 and 0x221E70):
# the slice of a shot with the slice -1.
DEFAULT_AMMO = {"arrow": "defaultarrow", "shot": "defaultshot"}
# The profiles of the weapons of a player whose basic attacks are shots (research abilities-ranged.md Q2: the bow, the musket,
# the blunderbuss; abilities-magic.md Q2: the four magic weapons). The other profiles of the weapon table are creatures and
# thrown weapons with no basic shot.
PLAYER_PROFILES = ("bow", "musket", "blunderbuss", "fireball", "icemagic", "lifestaff", "voidgauntlet")


def check_joins(shot_rows: list, light_heavy: dict, weapons: list, ammo: list, profiles: dict) -> None:
    """Stops the tool when the joins that the backend needs are not complete (code review of stage 3.3b-S3, finding F7): each
    profile of PLAYER_PROFILES of a weapon with a light or heavy row (TooltipPrimaryAttackData, TooltipAlternateAttackData) has a shot of such a
    row (an animation database with no clip, or no animation database, gives none), and the ammunition of an empty slot of the
    ammunition type of each weapon whose shot takes the slice of the ammunition (slice -1) has a slice."""
    for profile, wanted in sorted(light_heavy.items()):
        if profile not in PLAYER_PROFILES:
            continue
        if not any(entry[0].lower() == profile and entry[2].lower() in wanted for entry in shot_rows):
            raise Failure(f"the profile {profiles.get(profile, profile)} has no shot of its light or heavy row")
    slice_of_ammo = {entry[0].lower(): entry[3] for entry in ammo}
    fallback_profiles = {entry[0].lower() for entry in shot_rows if entry[4] < 0}
    for weapon, profile, ammo_type in weapons:
        default = DEFAULT_AMMO.get(ammo_type.lower())
        if profile.lower() in fallback_profiles and default is not None and slice_of_ammo.get(default, -1) < 0:
            raise Failure(f"the weapon {weapon} shoots the ammunition of an empty slot, and the ammunition {default} has no projectile "
                          "slice")


def build_document(sources: Sources) -> dict:
    sheets = read_sheets(sources)
    radb = sources.stream(RADB_PATH)
    sources.check_complete()
    if radb is None:
        raise Failure(f"the game copy has no {RADB_PATH}")
    attacks = read_attacks(radb)
    profiles = {row[0].lower(): row[0] for row in attacks}
    attack_names = {(row[0].lower(), row[1].lower()) for row in attacks}

    # The damage rows of the player: the weapon category and the attack type of each one.
    rows: dict[str, tuple[str, str, str]] = {}
    for sheet in sheets[DAMAGE_TYPE]:
        if sheet.name != DAMAGE_TABLE:
            continue
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "DamageID")).strip()
            if identifier:
                rows.setdefault(identifier.lower(), (identifier, str(sheet.cell(row, "WeaponCategory")).strip(),
                                                     str(sheet.cell(row, "AttackType")).strip()))
    if not rows:
        raise Failure(f"the game copy has no table {DAMAGE_TABLE}")

    slices: list[list] = []
    slice_index: dict[str, int] = {}

    def projectile_slice(prefab: str, required: bool) -> int:
        """@return The index of the projectile slice of a prefab, or -1 for an empty prefab. A prefab that names no slice of the
        game copy, or a slice with no projectile component, is -1 for a row that the backend does not need (an ammunition of a
        siege weapon), and stops the tool for a prefab that a shot needs (a SliceOverride, the prefab of a spell): -1 there
        would mean "the slice of the ammunition" (code review of stage 3.3b-S3, finding F7)."""
        if not prefab.strip():
            return -1
        path = slice_path(prefab)
        if path not in slice_index:
            roots = sources.stream(path) if sources.has(path) else None
            entry = read_projectile_slice(path, roots) if roots is not None else None
            slice_index[path] = len(slices) if entry is not None else -1
            if entry is not None:
                slices.append(entry)
        if required and slice_index[path] < 0:
            raise Failure(f"the projectile slice {path} of a shot has no source or no projectile component")
        return slice_index[path]

    ammo = []
    for sheet in sheets[AMMO_TYPE]:
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "AmmoID")).strip()
            modifier = sheet.cell(row, "DamageModifier")
            if not identifier or any(entry[0].lower() == identifier.lower() for entry in ammo):
                continue
            if not isinstance(modifier, float) or not math.isfinite(modifier):
                raise Failure(f"the ammunition {identifier} has a DamageModifier that is not a number")
            ammo.append([identifier, str(sheet.cell(row, "AmmoType")).strip(), round(modifier, 6),
                         projectile_slice(str(sheet.source(row, "AmmoPrefabPath")), False)])

    weapons = []
    light_heavy: dict[str, set[str]] = {}
    categories: dict[str, set[str]] = {}
    for sheet in sheets[WEAPON_TYPE]:
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "WeaponID")).strip()
            profile = str(sheet.cell(row, "RangedAttackProfile")).strip()
            if not identifier or profile.lower() not in profiles or any(entry[0].lower() == identifier.lower() for entry in weapons):
                continue
            weapons.append([identifier, profiles[profile.lower()], str(sheet.cell(row, "AmmoType")).strip()])
            for column in ("TooltipPrimaryAttackData", "TooltipAlternateAttackData"):
                tooltip = str(sheet.cell(row, column)).strip().lower()
                if tooltip in rows:
                    light_heavy.setdefault(profile.lower(), set()).add(tooltip)
                    categories.setdefault(profile.lower(), set()).add(rows[tooltip][1].lower())

    shots: dict[tuple, list] = {}
    # The clips of the animation databases: a shot of a weapon with ammunition (the bow, the musket).
    for path in sources.paths(ADB_FOLDER, ".adb"):
        data = sources.read(path)
        if data is None:
            continue
        for clip in read_clips(data, path):
            row = rows.get(clip.get("DamageTableRow", "").strip().lower())
            attack = clip.get("RangedAttackName", "").strip()
            if row is None or row[2] not in BASIC_ATTACK_TYPES:
                continue
            try:
                charge = round(float(clip.get("ChargeValue", "0") or 0), 6)
            except ValueError as error:
                raise Failure(f"{path}: a ChargeValue that is not a number: {error}") from error
            for profile, wanted in categories.items():
                if row[1].lower() not in wanted or (profile, attack.lower()) not in attack_names:
                    continue
                key = (profile, attack.lower(), row[0].lower(), charge)
                slot = AMMO_SLOTS.get(clip.get("AmmoSlot", "").strip(), 0) if clip.get("UseAmmo", "") == "true" else 0
                shots.setdefault(key, [profiles[profile], attack, row[0], charge, projectile_slice(clip.get("SliceOverride", ""), True),
                                       "", "", row[2] == "Heavy", slot, clip.get("ConsumeAmmo", "") == "true"])
    sources.check_complete()
    # The spells of a magic weapon: a projectile spell of the profile with the light or the heavy row of the weapon.
    for sheet in sheets[SPELL_TYPE]:
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "SpellID")).strip()
            profile = str(sheet.cell(row, "RangedAttackProfile")).strip().lower()
            damage = str(sheet.cell(row, "DamageTableRow")).strip().lower()
            if not identifier or "projectile" not in str(sheet.cell(row, "CastingTypes")).lower() \
                    or damage not in light_heavy.get(profile, set()):
                continue
            attack = str(sheet.cell(row, "RangedAttackName")).strip()
            if (profile, attack.lower()) not in attack_names:
                continue
            key = (profile, attack.lower(), damage, identifier.lower())
            shots.setdefault(key, [profiles[profile], attack, rows[damage][0], 0.0,
                                   projectile_slice(str(sheet.source(row, "SpellPrefabPath")), True), identifier,
                                   str(sheet.cell(row, "ManaCost")).strip(), rows[damage][2] == "Heavy", 0, False])
    sources.check_complete()
    # The projectile spells of the abilities of a player weapon (stage 3.3b-S2b: the shot of the Pylon, the Beacon, the trap of
    # the musket): a Projectile spell of a player profile whose row is a row of an ability (the attack type Ability). The mark
    # `ability` (the 11th value): only the abilities system launches it; a shot item of a basic attack does not fit it. A spell
    # whose prefab names no projectile slice is left out (a count).
    ability_shots = 0
    ability_no_slice = 0
    for sheet in sheets[SPELL_TYPE]:
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "SpellID")).strip()
            profile = str(sheet.cell(row, "RangedAttackProfile")).strip().lower()
            damage = str(sheet.cell(row, "DamageTableRow")).strip().lower()
            attack = str(sheet.cell(row, "RangedAttackName")).strip()
            if not identifier or profile not in PLAYER_PROFILES or "projectile" not in str(sheet.cell(row, "CastingTypes")).lower() \
                    or damage not in rows or rows[damage][2] not in ABILITY_ATTACK_TYPES or (profile, attack.lower()) not in attack_names:
                continue
            key = (profile, attack.lower(), damage, identifier.lower())
            if key in shots:
                continue
            index = projectile_slice(str(sheet.source(row, "SpellPrefabPath")), False)
            if index < 0:
                ability_no_slice += 1
                continue
            shots[key] = [profiles[profile], attack, rows[damage][0], 0.0, index, identifier, str(sheet.cell(row, "ManaCost")).strip(),
                          False, 0, False, True]
            ability_shots += 1
    sources.check_complete()

    shot_rows = sorted(shots.values(), key=lambda entry: (entry[0].lower(), entry[1].lower(), entry[2].lower(), entry[5].lower()))
    check_joins(shot_rows, light_heavy, weapons, ammo, profiles)
    return {"format": 1,
            "counts": {"attacks": len(attacks), "slices": len(slices), "ammo": len(ammo), "weapons": len(weapons),
                       "shots": len(shot_rows), "abilityShots": ability_shots, "abilityShotsWithNoSlice": ability_no_slice},
            "attacks": attacks, "slices": slices, "ammo": ammo, "weapons": weapons, "shots": shot_rows}


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    arguments = sys.argv[1:]
    find = arguments[1:] if arguments[:1] == ["--find"] else None
    target = Path(arguments[0]) if arguments and find is None else gamedata / OUTPUT_NAME
    try:
        sources = Sources(game / "assets", WANTED)
        document = build_document(sources)
        if find is not None:
            wanted = [text.lower() for text in find]
            for member in ("attacks", "shots", "weapons"):
                for entry in document[member]:
                    if any(text in json.dumps(entry).lower() for text in wanted):
                        print(f"{member}: {entry}")
            for entry in document["slices"]:
                if any(text in entry[0] for text in wanted):
                    print(f"slices: {entry}")
            return 0
        target.parent.mkdir(parents=True, exist_ok=True)
        # The backend must not read a file that is not complete: write, then rename.
        temporary = target.with_name(target.name + ".tmp")
        temporary.write_text(json.dumps(document, separators=(",", ":"), allow_nan=False) + "\n", encoding="utf-8")
        temporary.replace(target)
    except Failure as error:
        print(f"projectiles: {error}. No file was written.", file=sys.stderr)
        return 1
    except (OSError, ValueError) as error:
        print(f"projectiles: {target}: {error}", file=sys.stderr)
        return 1
    counts = document["counts"]
    print(f"{target}: {counts['attacks']} ranged attacks, {counts['slices']} projectile slices, {counts['ammo']} ammunition rows, "
          f"{counts['weapons']} weapons with a profile, {counts['shots'] - counts['abilityShots']} shots of basic attacks, "
          f"{counts['abilityShots']} projectile spells of abilities (source: "
          f"{', '.join(f'{count} from the {name}' for name, count in sorted(sources.used.items()))})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
