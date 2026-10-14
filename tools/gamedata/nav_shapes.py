#!/usr/bin/env python3
"""Read a physics file of the game (`.rnr`) and make triangles from each of its shapes.

A physics file has the collision shape of one mesh asset. The client reads it with the
function at image address 0xCC6500 (the file) and 0x10F7920 (one shape). All numbers are
little-endian.

file   := magic:u32 = 0x1234ABCD  version:u32 (the game copy has 6 and 7)
          setCount:u32  set*            the surface types of the shape
          bounds:16 bytes               (version 3 and more; not used here)
          hasIds:u8                     (version 5 and more)
          flag:u8                       (version 7 and more)
          [idCount:i16 id:u16*]         (only with hasIds)
          shapeCount:u8                 (version 4 and more)
          shape*                        the first shape is the collision shape
set    := size:u32 text  count:u32 index:u16*
shape  := type:u32 body  userSize:u32 user:userSize bytes
          `user` is empty, or it has 48 bytes: the transform of the shape in the file (a box
          has its center at the origin, and the transform puts it at its place).
body   := 1  box:          half:3 f32, margin:f32
          2  sphere:       radius:f32
          3  convex hull:  vertexCount:u32 (31 bits) planeCount:u32 vertex:3 f32* plane:4 f32*
                           margin:f32 hasAdjacency:u8 [adjacency]
                           Each hull of the game copy has no plane and no adjacency: the
                           reader computes the faces from the vertices.
          5  cylinder:     a:3 f32, b:3 f32, radius:f32, margin:f32
          7  capsule:      a:3 f32, b:3 f32, radius:f32
          10 mesh:         version:u32 vertexCount:u32 triangleCount:u32 vertex:3 f32*
                           index:u16 * 3 * triangleCount  hasEdges:u8 [u16 * 3 * triangleCount]
                           treeSize:u32 tree:treeSize bytes
          11 compound:     count:u32 (transform:12 f32, shape)*
          12 transformed:  transform:12 f32, shape
          18, 19 scaled:   version:u32 scale:3 f32, shape
A transform has 12 numbers: three rows of a 3 x 4 matrix (rotation and scale, then the
translation in the fourth column).

The result of `read_physics` is a list of parts. A part is (kind, vertices, triangles):
`kind` is CONVEX (1) for a closed convex solid (a box, a sphere, a hull), CLOSED (2) for a
mesh that is a closed surface (each edge has one triangle on each side, after the vertices at
one place are made one vertex), and 0 for each other mesh. `vertices` is a flat list of
numbers (x, y, z for each vertex), and `triangles` is a flat list of vertex indexes (three
for each triangle). The triangles of a CONVEX or CLOSED part have their front to the outside:
the reader turns the triangles of a closed mesh whose volume is negative (655 of the 14,278
closed meshes of the game copy).

The module needs only the standard library of Python.
"""

from __future__ import annotations

import math
import struct
import sys

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository

from nav_assets import Failure  # noqa: E402

MAGIC = 0x1234ABCD
BOX, SPHERE, HULL, CYLINDER, CAPSULE, MESH, COMPOUND, TRANSFORMED, SCALED_A, SCALED_B = 1, 2, 3, 5, 7, 10, 11, 12, 18, 19
SHAPE_NAMES = {BOX: "box", SPHERE: "sphere", HULL: "convex hull", CYLINDER: "cylinder", CAPSULE: "capsule",
               MESH: "mesh", COMPOUND: "compound", TRANSFORMED: "transformed", SCALED_A: "scaled",
               SCALED_B: "scaled"}
# The number of sides of a round shape (a sphere, a cylinder, a capsule) as triangles.
ROUND_SIDES = 12
# A vertex is on a plane of a hull when it is this near (meters, for a hull of 1 m; the limit
# grows with the size of the hull).
PLANE_TOLERANCE = 0.002

IDENTITY = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)
OPEN, CONVEX, CLOSED = 0, 1, 2

Part = tuple[int, list[float], list[int]]


def volume(vertices: list[float], triangles: list[int]) -> float:
    """@return Six times the volume of a closed surface (negative: the triangles have their front to the inside)."""
    total = 0.0
    for i in range(0, len(triangles), 3):
        a, b, c = triangles[i] * 3, triangles[i + 1] * 3, triangles[i + 2] * 3
        total += (vertices[a] * (vertices[b + 1] * vertices[c + 2] - vertices[b + 2] * vertices[c + 1])
                  - vertices[a + 1] * (vertices[b] * vertices[c + 2] - vertices[b + 2] * vertices[c])
                  + vertices[a + 2] * (vertices[b] * vertices[c + 1] - vertices[b + 1] * vertices[c]))
    return total


def is_closed(vertices: list[float], triangles: list[int]) -> bool:
    """@return True when each edge of a mesh has the same number of triangles on its two sides."""
    same: dict[tuple[float, float, float], int] = {}
    index = [same.setdefault((round(vertices[i], 3), round(vertices[i + 1], 3), round(vertices[i + 2], 3)), len(same))
             for i in range(0, len(vertices), 3)]
    edges: dict[tuple[int, int], int] = {}
    for i in range(0, len(triangles), 3):
        a, b, c = index[triangles[i]], index[triangles[i + 1]], index[triangles[i + 2]]
        if a == b or b == c or a == c:
            continue
        for edge in ((a, b), (b, c), (c, a)):
            edges[edge] = edges.get(edge, 0) + 1
    return bool(edges) and all(edges.get((b, a), 0) == count for (a, b), count in edges.items())


def outward(part: Part) -> Part:
    """@return The part with the kind CLOSED when it is a closed mesh, and with its front to the outside."""
    kind, vertices, triangles = part
    if kind == OPEN and is_closed(vertices, triangles):
        kind = CLOSED
    if kind != OPEN and volume(vertices, triangles) < 0.0:
        triangles = [index for i in range(0, len(triangles), 3)
                     for index in (triangles[i], triangles[i + 2], triangles[i + 1])]
    return kind, vertices, triangles


def multiply(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, ...]:
    """@return The 3 x 4 matrix a * b (first b, then a)."""
    return (
        a[0] * b[0] + a[1] * b[4] + a[2] * b[8], a[0] * b[1] + a[1] * b[5] + a[2] * b[9],
        a[0] * b[2] + a[1] * b[6] + a[2] * b[10], a[0] * b[3] + a[1] * b[7] + a[2] * b[11] + a[3],
        a[4] * b[0] + a[5] * b[4] + a[6] * b[8], a[4] * b[1] + a[5] * b[5] + a[6] * b[9],
        a[4] * b[2] + a[5] * b[6] + a[6] * b[10], a[4] * b[3] + a[5] * b[7] + a[6] * b[11] + a[7],
        a[8] * b[0] + a[9] * b[4] + a[10] * b[8], a[8] * b[1] + a[9] * b[5] + a[10] * b[9],
        a[8] * b[2] + a[9] * b[6] + a[10] * b[10], a[8] * b[3] + a[9] * b[7] + a[10] * b[11] + a[11])


def apply(matrix: tuple[float, ...], vertices: list[float]) -> list[float]:
    m = matrix
    out = [0.0] * len(vertices)
    for i in range(0, len(vertices), 3):
        x, y, z = vertices[i], vertices[i + 1], vertices[i + 2]
        out[i] = m[0] * x + m[1] * y + m[2] * z + m[3]
        out[i + 1] = m[4] * x + m[5] * y + m[6] * z + m[7]
        out[i + 2] = m[8] * x + m[9] * y + m[10] * z + m[11]
    return out


def determinant(m: tuple[float, ...]) -> float:
    return (m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8])
            + m[2] * (m[4] * m[9] - m[5] * m[8]))


def box_part(half: tuple[float, float, float]) -> Part:
    x, y, z = half
    vertices = [-x, -y, -z, x, -y, -z, x, y, -z, -x, y, -z, -x, -y, z, x, -y, z, x, y, z, -x, y, z]
    triangles = [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6, 3, 0, 4, 3,
                 4, 7]
    return CONVEX, vertices, triangles


def sphere_part(radius: float, center: tuple[float, float, float] = (0.0, 0.0, 0.0)) -> Part:
    return capsule_part(center, center, radius)


def frame(a: tuple[float, float, float], b: tuple[float, float, float]):
    """@return The unit vector from a to b and two unit vectors at right angles to it."""
    axis = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    size = math.sqrt(axis[0] ** 2 + axis[1] ** 2 + axis[2] ** 2)
    axis = (0.0, 0.0, 1.0) if size < 1e-9 else (axis[0] / size, axis[1] / size, axis[2] / size)
    other = (1.0, 0.0, 0.0) if abs(axis[0]) < 0.9 else (0.0, 1.0, 0.0)
    u = (axis[1] * other[2] - axis[2] * other[1], axis[2] * other[0] - axis[0] * other[2],
         axis[0] * other[1] - axis[1] * other[0])
    size = math.sqrt(u[0] ** 2 + u[1] ** 2 + u[2] ** 2)
    u = (u[0] / size, u[1] / size, u[2] / size)
    v = (axis[1] * u[2] - axis[2] * u[1], axis[2] * u[0] - axis[0] * u[2], axis[0] * u[1] - axis[1] * u[0])
    return axis, u, v


def rings_part(rings: list[tuple[tuple[float, float, float], float]], u, v) -> Part:
    """A solid of revolution: a list of (center, radius) rings. A ring with the radius 0 is one point."""
    vertices: list[float] = []
    starts: list[tuple[int, int]] = []
    for center, radius in rings:
        start = len(vertices) // 3
        if radius <= 0.0:
            vertices.extend(center)
            starts.append((start, 1))
            continue
        for side in range(ROUND_SIDES):
            angle = 2.0 * math.pi * side / ROUND_SIDES
            c, s = math.cos(angle) * radius, math.sin(angle) * radius
            vertices.extend((center[0] + u[0] * c + v[0] * s, center[1] + u[1] * c + v[1] * s,
                             center[2] + u[2] * c + v[2] * s))
        starts.append((start, ROUND_SIDES))
    triangles: list[int] = []
    for (low, low_count), (high, high_count) in zip(starts, starts[1:]):
        for side in range(ROUND_SIDES):
            following = (side + 1) % ROUND_SIDES
            if low_count == 1 and high_count == 1:
                continue
            if low_count == 1:
                triangles.extend((low, high + side, high + following))
            elif high_count == 1:
                triangles.extend((low + side, high, low + following))
            else:
                triangles.extend((low + side, high + side, high + following, low + side, high + following,
                                  low + following))
    return CONVEX, vertices, triangles


def cylinder_part(a, b, radius: float) -> Part:
    _, u, v = frame(a, b)
    return rings_part([(a, 0.0), (a, radius), (b, radius), (b, 0.0)], u, v)


def capsule_part(a, b, radius: float) -> Part:
    axis, u, v = frame(a, b)
    rings = []
    steps = ROUND_SIDES // 4
    for step in range(steps + 1):
        angle = -math.pi / 2 + (math.pi / 2) * step / steps
        offset, ring = math.sin(angle) * radius, math.cos(angle) * radius
        rings.append(((a[0] + axis[0] * offset, a[1] + axis[1] * offset, a[2] + axis[2] * offset),
                      0.0 if step == 0 else ring))
    for step in range(steps + 1):
        angle = (math.pi / 2) * step / steps
        offset, ring = math.sin(angle) * radius, math.cos(angle) * radius
        rings.append(((b[0] + axis[0] * offset, b[1] + axis[1] * offset, b[2] + axis[2] * offset),
                      0.0 if step == steps else ring))
    return rings_part(rings, u, v)


def hull_part(points: list[tuple[float, float, float]], planes: list[tuple[float, float, float, float]]) -> Part:
    """The faces of a convex hull: for each plane, the vertices on it in the sequence around the face."""
    vertices = [value for point in points for value in point]
    triangles: list[int] = []
    extent = max((abs(value) for value in vertices), default=1.0)
    limit = PLANE_TOLERANCE * max(1.0, extent)
    for nx, ny, nz, distance in planes:
        size = math.sqrt(nx * nx + ny * ny + nz * nz)
        if size < 1e-9:
            continue
        on_plane = [index for index, (x, y, z) in enumerate(points)
                    if abs((nx * x + ny * y + nz * z + distance) / size) <= limit]
        if len(on_plane) < 3:
            continue
        cx = sum(points[index][0] for index in on_plane) / len(on_plane)
        cy = sum(points[index][1] for index in on_plane) / len(on_plane)
        cz = sum(points[index][2] for index in on_plane) / len(on_plane)
        _, u, v = frame((0.0, 0.0, 0.0), (nx, ny, nz))

        def angle(index: int) -> float:
            dx, dy, dz = points[index][0] - cx, points[index][1] - cy, points[index][2] - cz
            return math.atan2(dx * v[0] + dy * v[1] + dz * v[2], dx * u[0] + dy * u[1] + dz * u[2])

        on_plane.sort(key=angle)
        for position in range(1, len(on_plane) - 1):
            triangles.extend((on_plane[0], on_plane[position], on_plane[position + 1]))
    return CONVEX, vertices, triangles


def hull_planes(points: list[tuple[float, float, float]]) -> list[tuple[float, float, float, float]]:
    """@return The planes of the convex hull of some points (a hull of the game has 30 points at most)."""
    planes: dict[tuple[int, int, int, int], tuple[float, float, float, float]] = {}
    count = len(points)
    extent = max((abs(value) for point in points for value in point), default=1.0)
    limit = 1e-4 * max(1.0, extent)
    for i in range(count):
        ax, ay, az = points[i]
        for j in range(i + 1, count):
            ux, uy, uz = points[j][0] - ax, points[j][1] - ay, points[j][2] - az
            for k in range(j + 1, count):
                vx, vy, vz = points[k][0] - ax, points[k][1] - ay, points[k][2] - az
                nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
                size = math.sqrt(nx * nx + ny * ny + nz * nz)
                if size < 1e-9:
                    continue
                nx, ny, nz = nx / size, ny / size, nz / size
                distance = -(nx * ax + ny * ay + nz * az)
                above = below = False
                for x, y, z in points:
                    side = nx * x + ny * y + nz * z + distance
                    if side > limit:
                        above = True
                    elif side < -limit:
                        below = True
                    if above and below:
                        break
                if above and below:
                    continue
                if above:
                    nx, ny, nz, distance = -nx, -ny, -nz, -distance
                key = (round(nx * 500), round(ny * 500), round(nz * 500), round(distance / limit / 20))
                planes.setdefault(key, (nx, ny, nz, distance))
    return list(planes.values())


class Statistics:
    """Counts for the report of a run."""

    def __init__(self) -> None:
        self.shape_types: dict[str, int] = {}
        self.user_sizes: dict[int, int] = {}
        self.versions: dict[int, int] = {}
        self.shape_counts: dict[int, int] = {}
        self.hull_adjacency = 0
        self.hull_planes_with_no_face = 0

    def count(self, table: dict, key) -> None:
        table[key] = table.get(key, 0) + 1


class Reader:
    def __init__(self, data: bytes, statistics: Statistics | None = None) -> None:
        self.data = data
        self.position = 0
        self.statistics = statistics or Statistics()

    def take(self, layout: str):
        size = struct.calcsize("<" + layout)
        if self.position + size > len(self.data):
            raise Failure(f"the file ends at offset {self.position} (need {size} bytes)")
        values = struct.unpack_from("<" + layout, self.data, self.position)
        self.position += size
        return values

    def skip(self, size: int) -> None:
        if size < 0 or self.position + size > len(self.data):
            raise Failure(f"the file ends at offset {self.position} (need {size} bytes)")
        self.position += size

    def shape(self, depth: int = 0) -> list[Part]:
        if depth > 8:
            raise Failure("the shapes are nested more than 8 levels")
        start = self.position
        (kind,) = self.take("I")
        self.statistics.count(self.statistics.shape_types, SHAPE_NAMES.get(kind, f"type {kind}"))
        parts: list[Part]
        if kind == BOX:
            x, y, z, _margin = self.take("4f")
            parts = [box_part((x, y, z))]
        elif kind == SPHERE:
            (radius,) = self.take("f")
            parts = [sphere_part(radius)]
        elif kind == HULL:
            vertex_count, plane_count = self.take("2I")
            vertex_count &= 0x7FFFFFFF
            if vertex_count > 1 << 20 or plane_count > 1 << 20:
                raise Failure(f"a convex hull at offset {start} has {vertex_count} vertices and {plane_count} planes")
            numbers = self.take(f"{vertex_count * 3}f")
            points = [numbers[i:i + 3] for i in range(0, len(numbers), 3)]
            numbers = self.take(f"{plane_count * 4}f")
            planes = [numbers[i:i + 4] for i in range(0, len(numbers), 4)]
            _margin, adjacency = self.take("fB")
            if adjacency:
                self.statistics.hull_adjacency += 1
                raise Failure(f"a convex hull at offset {start} has adjacency data, which this reader does not read")
            if not planes:
                if vertex_count > 64:
                    raise Failure(f"a convex hull at offset {start} has {vertex_count} vertices and no plane")
                planes = hull_planes(points)
            part = hull_part(points, planes)
            if len(part[2]) < 12:
                raise Failure(f"a convex hull at offset {start} gives fewer than 4 faces")
            parts = [part]
        elif kind == CYLINDER:
            ax, ay, az, bx, by, bz, radius, _margin = self.take("8f")
            parts = [cylinder_part((ax, ay, az), (bx, by, bz), radius)]
        elif kind == CAPSULE:
            ax, ay, az, bx, by, bz, radius = self.take("7f")
            parts = [capsule_part((ax, ay, az), (bx, by, bz), radius)]
        elif kind == MESH:
            _version, vertex_count, triangle_count = self.take("3I")
            if vertex_count > 65536 or triangle_count > 1 << 22:
                raise Failure(f"a mesh at offset {start} has {vertex_count} vertices and {triangle_count} triangles")
            vertices = list(self.take(f"{vertex_count * 3}f"))
            triangles = list(self.take(f"{triangle_count * 3}H"))
            if triangles and max(triangles) >= vertex_count:
                raise Failure(f"a mesh at offset {start} has a vertex index that is too large")
            (edges,) = self.take("B")
            if edges:
                self.skip(triangle_count * 6)
            (tree,) = self.take("I")
            self.skip(tree)
            parts = [(OPEN, vertices, triangles)]
        elif kind == COMPOUND:
            (count,) = self.take("I")
            if count > 1 << 16:
                raise Failure(f"a compound shape at offset {start} has {count} children")
            parts = []
            for _ in range(count):
                matrix = self.take("12f")
                parts.extend(transformed(self.shape(depth + 1), matrix))
        elif kind == TRANSFORMED:
            matrix = self.take("12f")
            parts = transformed(self.shape(depth + 1), matrix)
        elif kind in (SCALED_A, SCALED_B):
            _version, sx, sy, sz = self.take("I3f")
            parts = transformed(self.shape(depth + 1), (sx, 0.0, 0.0, 0.0, 0.0, sy, 0.0, 0.0, 0.0, 0.0, sz, 0.0))
        else:
            raise Failure(f"shape type {kind} at offset {start} is not supported")
        (user,) = self.take("I")
        self.statistics.count(self.statistics.user_sizes, user)
        if user == 48:
            parts = transformed(parts, self.take("12f"))
        elif user != 0:
            raise Failure(f"the shape at offset {start} has {user} bytes of user data, which this reader does not read")
        return parts


def transformed(parts: list[Part], matrix: tuple[float, ...]) -> list[Part]:
    out = []
    flip = determinant(matrix) < 0.0
    for convex, vertices, triangles in parts:
        if flip:
            triangles = [index for i in range(0, len(triangles), 3)
                         for index in (triangles[i], triangles[i + 2], triangles[i + 1])]
        out.append((convex, apply(matrix, vertices), triangles))
    return out


def read_physics(data: bytes, statistics: Statistics | None = None) -> list[Part]:
    """@return The parts of the collision shape of a physics file (its first shape)."""
    reader = Reader(data, statistics)
    magic, version = reader.take("2I")
    if magic != MAGIC:
        raise Failure(f"the file has the magic number 0x{magic:08X}, not 0x{MAGIC:08X}")
    if version < 4 or version > 7:
        raise Failure(f"file version {version} is not supported")
    reader.statistics.count(reader.statistics.versions, version)
    (sets,) = reader.take("I")
    if sets > 1 << 16:
        raise Failure(f"the file has {sets} surface sets")
    for _ in range(sets):
        (size,) = reader.take("I")
        reader.skip(size)
        (count,) = reader.take("I")
        reader.skip(count * 2)
    reader.skip(16)
    has_ids = 0
    if version >= 5:
        (has_ids,) = reader.take("B")
    if version >= 7:
        reader.skip(1)
    if has_ids:
        (count,) = reader.take("h")
        if count > 0:
            reader.skip(count * 2)
    (shape_count,) = reader.take("B")
    reader.statistics.count(reader.statistics.shape_counts, shape_count)
    shapes = [reader.shape() for _ in range(shape_count)]
    if reader.position != len(data):
        raise Failure(f"{len(data) - reader.position} bytes are after the last shape")
    if not shapes:
        raise Failure("the file has no shape")
    return [outward(part) for part in shapes[0]]


def triangle_count(parts: list[Part]) -> int:
    return sum(len(triangles) // 3 for _, _, triangles in parts)
