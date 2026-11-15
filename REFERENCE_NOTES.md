# Reference notes

## Selected reference version

Installation: `~/Library/Application Support/minecraft` (the only launcher directory found).

| Candidate | Installed (birth time) | Notes |
|---|---|---|
| **26.3** | 2026-11-03 16:33 | **Selected** — newest full release |
| 26.4-snapshot-7 | 2026-11-03 16:33 | Same install time; development snapshot |
| 26.2, 26.3-snapshot-1 | 2026-06-24 | Older |

26.3 and 26.4-snapshot-7 were installed at the same moment, so install time alone does not
decide. 26.3 is chosen because it is a stable release; snapshots change week to week. Switching
to the snapshot only affects which behaviours we compare against.

The installation is read for metadata only (version list, launcher profiles). Nothing in it is
modified, extracted, bundled, or loaded at runtime.

## How the reference is used

Voxcraft is an original game in the block-building sandbox genre. It implements the genre's
mechanics independently: mining, placement, crafting, survival, chunked procedural worlds,
fluids, signal circuits. It has its own identity:

- Own name, block and item roster, and terminology. For example, *turf* instead of a grass block,
  *azurite* as the top-tier ore, *glimmer wire*/*spark torch*/*relay*/*watcher* for the circuit
  family, and *kiln* for smelting.
- Original textures, models, sounds, UI art and creatures. No assets, code, names of signature
  characters or mobs, or trade dress are taken from the reference.
- Mechanics are designed from general genre knowledge and tuned by feel. Proprietary internals
  are not reverse-engineered or decompiled.

## Genre conventions adopted (independent implementations)

| Area | Convention in Voxcraft |
|---|---|
| World | 16×16 columns, 256 tall, sea level 63, 20 ticks/s simulation |
| Blocks | Unit cubes plus a few special shapes; hardness × tool tier → break time; light 0–15 |
| Lighting | Separate sky and block light, 0–15, decreasing by 1 per block (plus opacity) |
| Player | ~1.8 tall, ~0.6 wide, eye height ~1.62, jump clears 1 block, sprint/sneak |
| Fluids | Water and lava with levels, spreading over scheduled ticks; lava slower |
| Circuits | Signal 0–15 decaying per wire block; torches invert; relays delay/repeat |
| Day cycle | 24000-tick day |

These are common genre conventions. All numbers are tunables in data files or code constants,
not copied tables.
