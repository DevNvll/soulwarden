#!/usr/bin/env python3
"""Find the collision of a slice of the game: its colliders and the slices that it spawns.

A slice (`.dynamicslice`) is an object stream with entities. An entity has components. This
module reads four component classes:

  transform (484ae67d-...)   m_worldTM (12 numbers: the three basis vectors, then the
                             translation): the place of the entity in its slice. It is
                             relative to the root of the slice, not to the parent of the
                             entity (m_parentId): each entity has m_onNewParentKeepWorldTM = 1,
                             and m_localTM has the same 12 numbers. Proof: the wall segments
                             of the settlement are children of an entity with an offset of
                             78 m, and only the place with no parent offset is the wall of
                             the reference place (protocol notes 4.19).
  collider  (5ba2d1fc-...)   m_configuration: m_collisionType (0: the physics file of a mesh,
                             1: a list of simple shapes), m_rnrAsset, m_collisionShape,
                             m_center, m_strFilter (the collision filter), m_scaleShapes
  mesh      (2f4bad46-..., 6b5fca5e-...)   the mesh asset of the entity (field 0x014314FD)
  the physics components of the engine (a slice of a place of interest uses them, a chunk
  does not):
    static physics     (95d89791-...)   Configuration/EnabledInitially, CollisionFilter
    mesh collider      (2d559eb0-...)   no field: the physics file of the mesh of the entity
    primitive collider (9cb3707a-...)   the shape component of the entity:
      box (5edf4b9e-...) Dimensions: the full size; sphere (e24cbff0-...) Radius;
      cylinder (b0c6aa97-...) and capsule (967ec13d-...) Height, Radius
    An entity with a collider of these and no static physics component is not a solid (a
    trigger): the tool does not take it. The same for a static physics component with
    EnabledInitially = 0 (the client tests it before it makes the body): the tool counts it.
  spawner   (b52d0d52-...)   m_sliceAsset, m_sliceVariant, m_aliasAsset: a slice that the game
                             makes at this place when the entity starts
  navigation mark (500a7bfe-...)   the navigation obstacle mesh component: what the level
                             designers gave the navigation mesh of the original server.
                             m_navArea (0 UNWALKABLE, 3 UNWALKABLE_SOLID, 8 WALKABLE_SOLID,
                             63 WALKABLE, 4 to 7 jump links), m_meshSource (3: the game rigid
                             body of the same entity, 6: m_inlineShape, a list of simple
                             shapes; 0, 1, 2: shapes that this tool does not read),
                             m_registerWithNavMesh. The names are from the client code (image
                             addresses 0x39A1460 and 0x39DC880).

The collision of one collider:
  - type 1: each shape of m_collisionShape, with the offset m_center:
      c6651a66-...  box       m_box: the half size on each axis
      27462017-...  box       m_aabb: the half size on each axis
      7f2ef312-...  sphere    m_Radius
      709b11ea-...  capsule   m_Height (along z, with its ends) and m_Radius; the tool makes a
                              cylinder of that height
  - type 0 with m_rnrAsset: that physics file.
  - type 0 with no m_rnrAsset: the physics file of the mesh of the same entity. It is the
    product of the same source asset whose path is the mesh path with the ending `.rnr`.

The module needs only the standard library of Python.
"""

from __future__ import annotations

import struct
import sys

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository

from nav_assets import (Catalog, Element, asset_id, field_id, number, parse, real, text,  # noqa: E402
                        type_bytes, walk)
from nav_shapes import IDENTITY, multiply  # noqa: E402

ENTITY_TYPE = type_bytes("75651658-8663-478d-9090-2432dfcafa44")
TRANSFORM_TYPE = type_bytes("484ae67d-abd0-4d9c-b2c8-9bb0eec900e0")
COLLIDER_TYPE = type_bytes("5ba2d1fc-db9d-4b81-88c9-89787d5c19f1")
MESH_TYPES = (type_bytes("2f4bad46-c857-4dcb-a454-c412de67852a"), type_bytes("6b5fca5e-d112-4114-8d77-5cecc0078aca"))
SPAWNER_TYPE = type_bytes("b52d0d52-99cd-41be-b6f7-d2519c15bf14")
NAVIGATION_TYPE = type_bytes("500a7bfe-52fe-aa45-6b0e-b1253a4d7bf7")

STATIC_PHYSICS_TYPE = type_bytes("95d89791-6397-41bc-aac5-95282c8ad9d4")
MESH_COLLIDER_TYPE = type_bytes("2d559eb0-f6fe-46e0-9fce-e8f375177724")
PRIMITIVE_COLLIDER_TYPE = type_bytes("9cb3707a-73b3-4ee5-84ea-3cf86e0e3722")
BOX_SHAPE_TYPE = type_bytes("5edf4b9e-0d3d-40b8-8c91-5142bcfc30a6")
SPHERE_SHAPE_TYPE = type_bytes("e24cbff0-2531-4f8d-a8ab-47af4d54bcd2")
CYLINDER_SHAPE_TYPES = (type_bytes("b0c6aa97-e754-4e33-8d32-33e267db622f"), type_bytes("967ec13d-364d-4696-ab5c-c00cc05a2305"))
ENABLED_FIELD = field_id("EnabledInitially")
ENGINE_FILTER_FIELD = field_id("CollisionFilter")
DIMENSIONS_FIELD = field_id("Dimensions")
ENGINE_RADIUS_FIELD = field_id("Radius")
ENGINE_HEIGHT_FIELD = field_id("Height")
# The filter of a static physics component with no filter text.
DEFAULT_FILTER = "Static"

# The fields of a simple shape in m_collisionShape.
BOX_SHAPES = (field_id("m_box"), field_id("m_aabb"))
RADIUS_FIELD = field_id("m_Radius")
HEIGHT_FIELD = field_id("m_Height")
ON_START_FIELD = field_id("m_spawnOnInitialize")

ID_FIELD = field_id("Id")
NAME_FIELD = field_id("Name")
COMPONENTS_FIELD = field_id("Components")
LOCAL_FIELD = field_id("m_localTM")
WORLD_FIELD = field_id("m_worldTM")
PARENT_FIELD = field_id("m_parentId")
CONFIGURATION_FIELD = field_id("m_configuration")
TYPE_FIELD = field_id("m_collisionType")
RNR_FIELD = field_id("m_rnrAsset")
SHAPES_FIELD = field_id("m_collisionShape")
CENTER_FIELD = field_id("m_center")
FILTER_FIELD = field_id("m_strFilter")
SCALE_SHAPES_FIELD = field_id("m_scaleShapes")
DYNAMIC_FIELD = field_id("m_isDynamic")
MESH_ASSET_FIELD = 0x014314FD
INSTANCES_FIELD = 0xC41BD26A
SLICE_ASSET_FIELD = field_id("m_sliceAsset")
SLICE_VARIANT_FIELD = field_id("m_sliceVariant")
ALIAS_FIELD = field_id("m_aliasAsset")
NAV_AREA_FIELD = field_id("m_navArea")
NAV_SOURCE_FIELD = field_id("m_meshSource")
NAV_REGISTER_FIELD = field_id("m_registerWithNavMesh")
NAV_INLINE_FIELD = field_id("m_inlineShape")
# Values of m_navArea (client code 0x39A1460) and of m_meshSource (0x39DC880).
AREAS_NOT_WALKABLE = (0, 3)
AREAS_WALKABLE = (8, 63)
SOURCE_RIGID_BODY, SOURCE_INLINE = 3, 6
# The navigation mark of a collider in the collision data file.
MARK_NONE, MARK_NOT_WALKABLE, MARK_CUT, MARK_WALKABLE = 0, 1, 2, 3


def matrix_of(value: bytes | None) -> tuple[float, ...] | None:
    """@return The 3 x 4 matrix (three rows) of a transform field: 12 numbers, basis vectors then translation."""
    if value is None or len(value) != 48:
        return None
    v = struct.unpack(">12f", value)
    return (v[0], v[3], v[6], v[9], v[1], v[4], v[7], v[10], v[2], v[5], v[8], v[11])


def entity_id(element: Element | None) -> int | None:
    if element is None:
        return None
    for candidate in (element, *element.children):
        if candidate.value is not None and len(candidate.value) == 8:
            return int.from_bytes(candidate.value, "big")
    return None


class Collider:
    __slots__ = ("matrix", "kind", "reference", "filter", "name", "dynamic", "mark")

    def __init__(self, matrix, kind: str, reference, collision_filter: str, name: str, dynamic: bool,
                 mark: int = MARK_NONE) -> None:
        self.matrix = matrix            # the place of the collider in the slice (3 x 4)
        self.kind = kind                # "rnr": a physics file; "mesh": the mesh of the entity; "shape": a simple shape
        self.reference = reference      # an asset id, or the values of the simple shape
        self.filter = collision_filter
        self.name = name
        self.dynamic = dynamic
        # MARK_NOT_WALKABLE: a solid whose top is no floor. MARK_CUT: no solid, a volume in which no floor is free.
        self.mark = mark


class Spawner:
    __slots__ = ("matrix", "slice", "variant", "alias", "on_start")

    def __init__(self, matrix, slice_asset, variant: str, alias, on_start: bool) -> None:
        self.matrix = matrix
        self.slice = slice_asset
        self.variant = variant
        self.alias = alias
        self.on_start = on_start


class SliceContent:
    __slots__ = ("colliders", "spawners", "entities", "navigation_areas", "navigation_not_read", "triggers",
                 "disabled")

    def __init__(self) -> None:
        self.colliders: list[Collider] = []
        self.spawners: list[Spawner] = []
        self.entities = 0
        self.navigation_areas: dict[int, int] = {}
        # Navigation marks that the tool does not use, by cause.
        self.navigation_not_read: dict[str, int] = {}
        self.triggers = 0     # engine colliders with no static physics component
        self.disabled = 0     # engine colliders whose static physics component is not on at its start


def simple_shape(shape: Element) -> tuple | None:
    """@return ("box", half x, half y, half z), ("sphere", radius), or ("cylinder", height, radius)."""
    radius = height = None
    for node in shape.children:
        if node.name in BOX_SHAPES and node.value is not None and len(node.value) == 12:
            return ("box", *struct.unpack(">3f", node.value))
        if node.name == RADIUS_FIELD:
            radius = real(node)
        elif node.name == HEIGHT_FIELD:
            height = real(node)
    if radius is not None and height is not None:
        return "cylinder", height, radius
    if radius is not None:
        return "sphere", radius
    return None


def read_slice(data: bytes) -> SliceContent:
    """@return The colliders and the spawners of a slice, with their place in the slice."""
    roots = parse(data)
    content = SliceContent()
    entities: list[tuple[int | None, int | None, tuple | None, tuple | None, Element, str]] = []
    for root in roots:
        for node in walk(root):
            if node.type_id != ENTITY_TYPE:
                continue
            components = node.child(COMPONENTS_FIELD)
            if components is None:
                continue
            local = world = None
            parent = None
            for component in components.children:
                if component.type_id == TRANSFORM_TYPE:
                    local = matrix_of(getattr(component.child(LOCAL_FIELD), "value", None))
                    world = matrix_of(getattr(component.child(WORLD_FIELD), "value", None))
                    parent = entity_id(component.child(PARENT_FIELD))
            entities.append((entity_id(node.child(ID_FIELD)), parent, local, world, components,
                             text(node.child(NAME_FIELD))))
    content.entities = len(entities)
    for _identifier, _parent, local, world, components, name in entities:
        matrix = world or local or IDENTITY
        mesh = None
        instances: list[tuple] = []
        for component in components.children:
            if component.type_id in MESH_TYPES:
                for node in walk(component):
                    if node.name == MESH_ASSET_FIELD and mesh is None:
                        mesh = asset_id(node)
                    elif node.name == INSTANCES_FIELD:
                        instances = [m for m in (matrix_of(child.value) for child in node.children) if m is not None]
        # The navigation mark of the entity (one component at most in the game data).
        body_mark = MARK_NONE
        for component in components.children:
            if component.type_id != NAVIGATION_TYPE:
                continue
            area = number(component.child(NAV_AREA_FIELD))
            source = number(component.child(NAV_SOURCE_FIELD))
            content.navigation_areas[area] = content.navigation_areas.get(area, 0) + 1
            has_body = any(other.type_id == COLLIDER_TYPE for other in components.children)
            if number(component.child(NAV_REGISTER_FIELD)) == 0:
                cause = "a mark that is not registered (m_registerWithNavMesh is 0)"
            elif area not in AREAS_NOT_WALKABLE and area not in AREAS_WALKABLE:
                cause = f"a mark with the area {area} (a jump link, water, a steep slope)"
            elif source == SOURCE_RIGID_BODY and has_body:
                body_mark = MARK_NOT_WALKABLE if area in AREAS_NOT_WALKABLE else MARK_WALKABLE
                continue
            elif source == SOURCE_INLINE:
                inline = component.child(NAV_INLINE_FIELD)
                shapes = inline.children if inline is not None else []
                if area in AREAS_WALKABLE:
                    cause = "a walkable mark with a shape of its own (no solid: nothing to change)"
                elif not shapes:
                    cause = "a mark with an empty list of shapes"
                else:
                    for shape in shapes:
                        content.colliders.append(Collider(matrix, "shape", (simple_shape(shape), (0.0, 0.0, 0.0),
                                                                            shape.type_id, 1), "", name, False, MARK_CUT))
                    continue
            else:
                # 0: a shape that the level designer drew (the master slice of a tree has such a mark: the
                # collider of the tree makes its trunk a solid already). 1 and 2: the shape of a detectable
                # object. 3 with no rigid body. The tool does not read these shapes.
                names = {0: "a shape of the designer", 1: "the detectable object of the entity",
                         2: "a list of detectable objects", SOURCE_RIGID_BODY: "the rigid body of the entity, which it does not have"}
                cause = f"a mark with the shape source {source} ({names.get(source, 'not known')})"
            content.navigation_not_read[cause] = content.navigation_not_read.get(cause, 0) + 1
        static = None
        static_off = False
        engine_shape = None
        engine_mesh = engine_primitive = False
        for component in components.children:
            if component.type_id == STATIC_PHYSICS_TYPE:
                enabled = [node for node in walk(component) if node.name == ENABLED_FIELD]
                if not enabled or number(enabled[0]) != 0:
                    static = text(component.child(ENGINE_FILTER_FIELD)) or DEFAULT_FILTER
                else:
                    static_off = True
            elif component.type_id == MESH_COLLIDER_TYPE:
                engine_mesh = True
            elif component.type_id == PRIMITIVE_COLLIDER_TYPE:
                engine_primitive = True
            elif component.type_id == BOX_SHAPE_TYPE:
                for node in walk(component):
                    if node.name == DIMENSIONS_FIELD and node.value is not None and len(node.value) == 12:
                        x, y, z = struct.unpack(">3f", node.value)
                        engine_shape = ("box", x / 2, y / 2, z / 2)
            elif component.type_id == SPHERE_SHAPE_TYPE or component.type_id in CYLINDER_SHAPE_TYPES:
                radius = height = None
                for node in walk(component):
                    if node.name == ENGINE_RADIUS_FIELD and radius is None:
                        radius = real(node)
                    elif node.name == ENGINE_HEIGHT_FIELD and height is None:
                        height = real(node)
                if radius is not None:
                    engine_shape = ("sphere", radius) if component.type_id == SPHERE_SHAPE_TYPE or height is None \
                        else ("cylinder", height, radius)
        if engine_mesh or engine_primitive:
            if static is None and static_off:
                content.disabled += 1
            elif static is None:
                content.triggers += 1
            else:
                if engine_mesh and mesh is not None:
                    for instance in (instances or [IDENTITY]):
                        content.colliders.append(Collider(multiply(matrix, instance), "mesh", mesh, static, name, False))
                elif engine_mesh:
                    content.colliders.append(Collider(matrix, "none", None, static, name, False))
                if engine_primitive:
                    content.colliders.append(Collider(matrix, "shape", (engine_shape, (0.0, 0.0, 0.0), b"\0" * 16, 0),
                                                      static, name, False))
        for component in components.children:
            if component.type_id == COLLIDER_TYPE:
                configuration = component.child(CONFIGURATION_FIELD) or component
                collision_filter = text(configuration.child(FILTER_FIELD))
                center = configuration.child(CENTER_FIELD)
                offset = struct.unpack(">3f", center.value) if center is not None and center.value is not None \
                    and len(center.value) == 12 else (0.0, 0.0, 0.0)
                kind = number(configuration.child(TYPE_FIELD))
                dynamic = number(configuration.child(DYNAMIC_FIELD)) != 0
                rnr = asset_id(configuration.child(RNR_FIELD))
                shapes = configuration.child(SHAPES_FIELD)
                if kind == 1:
                    for shape in (shapes.children if shapes is not None else []):
                        values = (simple_shape(shape), offset, shape.type_id,
                                  number(configuration.child(SCALE_SHAPES_FIELD)))
                        content.colliders.append(Collider(matrix, "shape", values, collision_filter, name, dynamic,
                                                          body_mark))
                elif rnr is not None:
                    content.colliders.append(Collider(matrix, "rnr", rnr, collision_filter, name, dynamic, body_mark))
                elif mesh is not None:
                    for instance in (instances or [IDENTITY]):
                        content.colliders.append(Collider(multiply(matrix, instance), "mesh", mesh, collision_filter,
                                                          name, dynamic, body_mark))
                else:
                    content.colliders.append(Collider(matrix, "none", None, collision_filter, name, dynamic))
            elif component.type_id == SPAWNER_TYPE:
                content.spawners.append(Spawner(matrix, asset_id(component.child(SLICE_ASSET_FIELD)),
                                                text(component.child(SLICE_VARIANT_FIELD)),
                                                asset_id(component.child(ALIAS_FIELD)),
                                                number(component.child(ON_START_FIELD)) != 0))
    return content


def physics_path(catalog: Catalog, mesh: tuple[bytes, int]) -> str | None:
    """@return The physics file of a mesh asset: the product of its source with the same path and `.rnr`."""
    path = catalog.path(mesh)
    products = catalog.products(mesh[0])
    if path is not None and path.endswith(".cgf"):
        wanted = path[:-4] + ".rnr"
        for _sub, product in products:
            if product == wanted:
                return wanted
    physics = [product for _sub, product in products if product.endswith(".rnr")]
    if path is None and len(physics) == 1:
        return physics[0]
    return None
