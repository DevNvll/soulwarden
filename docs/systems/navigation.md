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
  compute a place of a creature in a different function. With navigation data the path is
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

## Commands

```sh
tools/world path NAME 8861.6 4243.4 139.5   # the path of the navigation between two places (a place: X Y Z or a character name)
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
