# Navigation and terrain

Read this document before a change of the navigation data, the path search, the tiles near the
players, or the terrain heights. The wire contracts and the evidence are in the protocol notes
(sections 4.17 and 4.19).

## Start here

| Task | Start here |
| --- | --- |
| The navigation data: where a creature can stand, its path around a wall and up stairs, what stops its sight, the tiles near the players, the tool that makes the data from the collision shapes of the game copy | Protocol notes 4.19, `apps/backend/src/world/navigation.h`, `gamedata/navigation_data.h`, `tools/gamedata/navigation.sh` (`navigation_data.py`, `collision_data.py`, `navcore.cpp`, `nav_file.py`), `tools/world path`, `tools/world track`, `tools/research/nav_draw.py`, test cases NV1 to NV12 and RN1 to RN10 (`tools/gamedata/check_navigation_tool.py`) |
| The terrain heights: the height of the ground at a place with no navigation data, and the terrain after the cookies of the placements | Protocol notes 4.17 and 4.19, `apps/backend/src/gamedata/height_maps.h`, `world/navigation.h`, `tools/gamedata/heights.sh`, `tools/gamedata/check_heights_tool.py` |

## Rules

- The functions of the navigation (`world/navigation.h`) are the only place where the
  behavior gets a place: `ground_height(position)` (the height of the floor that is nearest
  to the height of a position: one place can have more than one floor) and
  `next_step(creature, from, goal, distance, nearest)` (the place after one step on the path
  of a creature to a goal, or "the creature waits", "no way", "no ground"; with `nearest`, a
  goal with no way gives the path to the place nearest to it, with the mark
  `NavStep::partial`: only for a fight). `footing(position)` says if a creature walks, and
  `sees(from, to)` if a solid is between two places (cell by cell on the line, with the
  exact heights of the line in each cell). Do not
  compute a place of a creature in a different function. The one other function is
  `slide(from, to)` (stage 3.3b-S4, [Forced movement](forced-movement.md)): the straight line of a pull or a push on the
  free floors, cell by cell with the rule of a step, to the last place before a wall, a drop, or a cell with no free
  floor; it keeps no path. The line test of a projectile is `line_stop(from, to)` (stage 3.3b-S3,
  [Projectiles](projectiles.md)): the segment between two points in the air, cell by cell with the exact heights of the
  segment (no eye height), each cell with the free interval of its floor (as `sees`; a part below the lowest floor is in
  the ground); it gives the part of the segment at the first cell that stops it. With no tile on the line nothing stops
  it. It is not a place of a creature. Stage 1.8f: the strike of a destructible quest object uses it too (the line from the
  eyes of the character to the near side of the object, [Quests](quests.md)), with `line_ready(from, to)` before it: a
  tile of the segment that the world does not have yet gets a request, and the caller waits (no data: ready). A point
  segment (`line_stop(p, p)`) tells if a solid holds the point. With navigation data the path is
  a path of the data; with no data of a place (no folder, a folder that is not correct, a
  region with no file, a tile that is not correct) the two functions have the rule of stage
  3.4b for that place (the straight line on the terrain of the folder `heights`, and
  nothing stops the sight).
- The navigation data (protocol notes 4.19) is game data: the folder `navigation` of the game
  data folder, one file for each region (`r_+XX_+YY.swnav`, 32 by 32 tiles of 64 m with cells
  of 0.25 m, each tile one zlib stream) and `navigation.json`. The tool
  (`tools/gamedata/navigation.sh`) makes it from the collision shapes of the game copy, the
  terrain after the cookies of the placements, the water, and the navigation marks of the
  level designers. The size of a creature (the agent: radius 0.35 m, height 1.8 m, step
  0.35 m, slope 50 degrees, wade 1.2 m) is a project value with a live check for each
  number; each file has the values in its header, and the backend refuses a folder whose
  files do not have the same values, as a whole, as for each other file that is not
  correct. A tile with more than 16 floors in one cell, and a tile stream with a byte after
  its zlib stream, are not correct; the tool stops for a cell with more than 16 floors
  (`kMaxCellFloors` of `navcore.cpp`), and it uses a stored report of the collision step only
  when the report is complete, of its format and tool version, and names no physics file
  that did not decode. The backend reads only the header and the directory of
  each file at its start. The tool stops (status 1, no region file) for a placed collider
  whose physics file does not decode.
- The world has only the tiles near its players (`navigation_rules`): each 500 ms it asks
  for the tiles that are 96 m or less from a character and 32 m or less from a creature on
  a path, and it removes a tile that no one asked for in 10 s. A thread of the navigation
  reads the tiles (`WorldOptions::navigationThread`; a test host reads them in the tick):
  the world thread reads no navigation file, and it takes the tiles that are ready at the
  start of a tick. The thread reads only the navigation data: it reads no entity. A
  creature whose tile is not there yet waits: the tile of its place, of its goal (the tile
  of the goal itself, before the goal goes to a free floor near it), of a link between two
  areas, and of each step (a path in the memory of a creature can go into a
  tile that the world removed). A tile that the world does not have is no proof of "no
  way". Do not read all tiles of a region, and do not
  read a tile in the world thread (the development command `tools/world path` is the one
  exception, for a test).
- The cost of the path search has limits (`navigation_rules`): all searches of one tick of
  the world make 4,000 steps at most (`kTickSteps`), one search makes 60,000 steps at most
  (`kSearchSteps`), the other searches wait in a queue, and a search continues in the next
  tick. A goal in a different area has no way with no search (the areas of the tiles, and
  the links over the tile edges, which the world makes only for a tile that comes or goes);
  only `next_step` with `nearest` (a fight) searches for the place nearest to such a goal, in
  the same limits.
  First the straight line, then A* on the cells. A step of a creature ends at a corner of
  its path: the client moves a creature on the straight line between two movement states.
  Test cases NV6 and NV10 have the limits with a time limit: a new rule keeps them.
- A path after a short movement that the path did not make (a push of a hit, 0.01 m to 1 m from the place of the last step;
  `navigation_rules::kMovedOffPath`, `kOffPath`): `next_step` keeps the path only when the straight line from the new place to
  the next corner of the path, or else to its next cell, is free (`walk_line`), and the real segment from the new place to the
  center of that cell is free too (`walk_segment`: the cells on the segment with the floor checks of `slide`). If not, it forgets
  the path and searches again from the new place (the count `pushedOffPath` of `tools/world list`). The corner of the path was free
  from the old place only: the line from the new place can go through a wall (code review of user test 2, finding U2-R1; test case
  RT1). More than 1 m from the place of the last step: a new search, as before.
- The real segment from the place of the creature (code review of stage B0, finding B0-R1; test case RQ1, with the pushes of 0.2 s
  to 1.0 s at 1 m/s). The line between the centers of two cells (`walk_line`) is not the line that the creature walks: `follow`
  moves it from its real place to the center of the corner. A creature that is not at the center of its cell (the first step of
  a new path, the place after a push) needs the real segment to be free too: the direct path of `next_step` needs
  `walk_segment(start, from, end)` (else the search makes a path of cells), and `follow` takes a corner only with a free real
  segment from the place of the creature (else a nearer corner, at last the next cell). From the center of a cell the two lines
  are the same.
- Which creature walks: a creature walks when a free floor of the navigation data is 0.5 m
  or less from its place in the plane (to the area of its cell) and 0.6 m or less in the height
  (`Navigation::footing`). The world asks when the tile of the creature is there: until then
  the creature has no answer (`CreatureMind::groundKnown`), and it does not walk yet. A
  creature on no free floor does not leave its place: it turns to its target and attacks in
  its reach. 23,021 of the 24,228 spawn points of kinds that fight walk.
- The gates of a settlement are closed for a creature: the level designers put a navigation
  mark with no solid into each gate (three in the start settlement), and the tool makes no
  floor free in it. A creature outside a gate sees a character in the gate, finds no way,
  and loses its target. Do not remove these marks to make a test pass.
- The terrain heights are game data (`tools/gamedata/heights.sh`, one file for each region in
  the folder `heights` of the game data folder, `gamedata::HeightMaps`). The backend opens a
  region file only when a position in it is asked, reads one tile of 64 m, and keeps 512
  tiles: do not load the 49 regions at the start. The terrain is not each floor. From stage
  3.4d the heights are the terrain after the cookies of the placements (the client changes
  the height map near some placements, protocol notes 4.19; `tools/gamedata/navigation.sh`
  makes the folder again at its end), and the backend uses them only at a place with no
  navigation data. There a creature walks only when its spawn point is 1.0 m or less from
  the terrain height (`gamedata::kTerrainTolerance`; the creature data file has the terrain
  height of each spawn point): if not, it does not leave its place, turns to its target,
  and attacks in its reach. Without the height files and with no navigation data no
  creature leaves its place.

### Instances (stage DG1)

- Stage DG1 ([Instances](instances.md)): the navigation data is the data of the main world: the tiles that the backend keeps are the
  tiles near the characters of the main world; a character in an instance needs none [agent].
- Stage QG9a: a space of the level of the main world (a story instance of `newworld_vitaeeterna`, `World::main_level`) uses the navigation
  data of the main world: its characters need tiles, and its creatures walk on them [agent; live: the boss Medea in its arena].
- Stage QG9a (finding DG1R-R6 of the review of stage DG1): the sight and the line tests (`sees`, `line_stop`, `line_ready`) take the space
  of their subject. In a space of a different level nothing stops them (the rule of no data); a wall of the main world does not block a
  heal or a shot there (case DG1R6) [review]. The ground and the slide of a space of a different level are open (stage DG2).

### Own levels (stage DG2)

- Each own level with navigation data or terrain heights in its level set has its own navigation: its tiles, its loader thread, its path
  searches (with the limits of a tick of its own), and its search table (8 MB) ([Instances](instances.md)). `World::navigation(space)` gives
  the navigation of the level of a space; each call of a subject (a creature, a caster, a spell, a projectile, a spawn of an encounter, the
  path command) takes the navigation of its space. A space of an own level with no set has a navigation with no data: no ground, no wall,
  no way [agent; case LV3].
- The navigation of a level keeps the tiles near the characters of the spaces of its level (`Navigation::level_data` with the level).
- The tools of a level (`tools/gamedata/level.sh`; `navigation.sh --level LEVEL`, `heights.sh --level LEVEL`): the collision of a region with
  no `region.chunks` and no `region.slicedata` is the collision of its capitals (the geometry of a dungeon is in `dungeon_art`) and of its
  distribution; the work files of a level are in `SOULWARDEN_NAVIGATION_WORK/levels/LEVEL` (its region has the name of a region of the main
  world). The terrain of a level has its own `mountainHeight` (terrain.json: 256 m in most levels, 2048 m in the main world): the tools put
  its samples into the scale of 2048 m (`nav_terrain.to_main_scale`), the scale of each tool file and of the backend [data; agent]. With the
  scale of the main world the flat terrain of Amrine was at 256 m, above the mine, and the data had no floor of the mine.
- Amrine: one region, 2,506 placements, 1.9 MB of navigation data; the floor of the start at 152.25 m (the probe of research DG0: 152.19);
  `tools/world path` in the mine gives a path of 13 corners and 85.9 m around the rock walls (a straight line of 52 m) [live].

## Commands

```sh
tools/world path NAME 8861.6 4243.4 139.5   # the path of the navigation between two places (a place: X Y Z or a character name)
tools/world path NAME 694.7 684.6 148.7     # stage DG2: in the navigation of the space of the character (an own level); --space N
python3 -B tools/research/nav_draw.py ~/.local/share/soulwarden/gamedata/navigation 8850 4230 8880 4260 OUT.png --path FILE   # a picture of the data
```

- `tools/gamedata/heights.sh` writes the terrain heights to the folder `heights` of the game
  data folder: one file for each of the 49 regions (136 MB, 2 minutes with 8 processes;
  the pak entries of the height maps are stored data: no Oodle source). `--region NAME`
  writes some regions, `--jobs N` sets the number of processes, and `--height X Y` prints
  the terrain height of a position from the files and writes nothing. The creature data
  tool reads the height maps itself: it does not need these files.

## Traps

- With no navigation data (stage 3.4b): a creature whose spawn point is more than 1.0 m
  from the terrain height does not leave its place, and a creature goes on the straight
  line, through a wall, on the terrain. With the navigation data (stage 3.4d) the Ancient
  Guardian Reaver at `8572.83 4241.20 166.60` and the Risen of the hut walk on their
  floors. `stays at its place` of `tools/world creatures` shows only when the world has
  the tile of the creature (a creature far from each character has no answer yet).
- No creature goes into the start settlement: the gates have navigation marks of the
  level designers. A wolf outside a gate stands and looks at a character inside. A fight
  inside the settlement needs a creature of a command inside it.
- `tools/world path` reads the tiles of the two places in the world thread: the line
  `load:` then has a largest tick of 30 ms to 200 ms. Measure the load in a run with no
  path command.
- A tile is read 4 ms to 16 ms after it is asked: a creature at the edge of the tiles of a
  character can wait some ticks. The tool window and `tools/world creatures` show the
  creatures of the whole world: a creature far from each character has no data.
- `tools/gamedata/navigation.sh` writes into the common game data folder by default (1.2
  GB): give it an output folder for a test, and give the run that folder with
  `SOULWARDEN_GAMEDATA_DIR` (with links to the other game data files and a real folder
  `heights`: the tool writes the heights into the folder beside its output). A link to the
  common folder `heights` makes the tool write into the common folder.
- The test program builds the navigation files of a case in its work folder
  (`TestNavRegion`: a region of the real size, flat ground where a case gives no floor).
