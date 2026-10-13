#!/usr/bin/env python3
"""Print the fragment slot of each faceted component of a slice from the game data.

Each faceted component in a slice has the field `m_replicationIndex`. That number is the slot
of its state fragments in a state record (it agrees with the live component list of
gde_components.py for slices/player.dynamicslice). Extract the slice first with pakx.py.

Usage: slice_slots.py SLICE_FILE [--all]     (--all: include components with index 0 or junk)
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import objstream  # noqa: E402

MAX_SLOT = 4096


def main() -> None:
    show_all = "--all" in sys.argv
    path = next(argument for argument in sys.argv[1:] if not argument.startswith("--"))
    _, roots = objstream.parse(objstream.decode(Path(path).read_bytes()))
    names, types = objstream.load_names()
    rows = []

    def field(element) -> str:
        return "" if element.name is None else names.get(element.name, f"#{element.name:08X}")

    def walk(element, entity: str, component) -> None:
        label = field(element)
        type_name = types.get(element.type_id, element.type_id)
        if type_name == "75651658-8663-478d-9090-2432dfcafa44":  # AZ::Entity
            for child in element.children:
                if child.name == 0x5E237E06 and child.value is not None:  # entity name
                    entity = child.value.decode("utf-8", "replace")
        if label == "m_clientFacetPtr":
            component["facet"] = type_name
        if label == "m_replicationIndex" and element.value is not None:
            rows.append((int.from_bytes(element.value, "big"), component.get("facet", "?"), entity))
        for child in element.children:
            walk(child, entity, component if label != "element" else {})

    for root in roots:
        walk(root, "", {})
    for slot, facet, entity in sorted(rows):
        if show_all or 0 < slot < MAX_SLOT:
            print(f"{slot:5d} {facet:60s} entity {entity}")


if __name__ == "__main__":
    main()
