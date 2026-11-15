# Voxcraft world format (format 1)

All integers are little-endian. `var` = unsigned LEB128 varint, `svar` = zigzag varint,
`str` = `var` length + UTF-8 bytes. The format is identical on every platform and architecture;
`storage_E_cross_platform_golden` pins the exact bytes of a known record.

## Directory layout

```
<world>/
  world.json          metadata + generator configuration (human readable)
  player.dat          player state blob (game layer)
  region/r.<rx>.<rz>.vxr   region containers, 32x32 chunks each; only regions holding records exist
  save.journal        present only while a save is being applied (see "Saving")
  dim/<name>/         other dimensions (e.g. dim/hollow): same layout (world.json + region/), own
                      generator settings (dimension = "<name>"); player.dat stays in the main folder
```

## world.json

```json
{ "format": 1, "name": "...",
  "generator": { "seed": "123", "version": 1, "dimension": "overworld", "preset": "default",
                 "terrain_scale": 1, "caves": true, "ores": true, "trees": true, "structures": true },
  "tick": "0", "day_time": 1000, "raining": false, "weather_ticks": 12000, "populated_villages": [[rx, rz], ...], "spawn": [x, y, z], "game_mode": "survival",
  "created": 0, "last_played": 0 }
```

`generator.dimension` is `overworld` or `hollow`. The Hollow requires generator version ≥ 2.
`seed` and `tick` are strings because 64-bit integers do not fit a JSON double. `generator.version`
selects the generator implementation; a world whose version the game does not know is refused,
never regenerated with a newer generator.

## What is stored

Only chunks with persistent data have a record. A chunk has persistent data if any of:

- a block differs from the regenerated baseline (`Chunk::delta` non-empty),
- it has block entities (containers, kilns),
- it has pending scheduled ticks,
- it has saved entities.

Unmodified chunks, chunks whose edits were all reverted, light, heightmaps, meshes and biomes are
never stored; they are regenerated/recomputed. When a chunk's record becomes empty, the record is
deleted, and a region file with no records is deleted.

## Region file (`.vxr`)

```
u32 magic "VXRG" (0x47525856)   u16 format (1)   u16 reserved
i32 rx   i32 rz
var count
repeat count:  u16 local (= (cz & 31) << 5 | (cx & 31))   var len   u8[len] record   u32 crc32(record)
u32 crc32(everything above)
```

A region with a bad file checksum or record checksum is not loaded: a `.corrupt` copy is kept next
to it and the error is logged; nothing is deleted.

## Chunk record

```
u8 encoding     0 = raw, 1 = LZ4 block (followed by var rawSize, then compressed bytes)
-- raw body --
u8 kind         1 = sparse, 2 = full (legacy / migration input)
var nBlocks, str[nBlocks]   block palette, "name[prop=v,...]" (state ids are never stored)
var nItems,  str[nItems]    item palette
sparse:  var n; repeat n: var indexDelta, var paletteIndex     (index = y<<8 | z<<4 | x, ascending)
full:    16 sections: u8 mode; mode 0: var paletteIndex (uniform) | mode 1: 4096 x var paletteIndex
var nBlockEntities; repeat: u16 index, u8 kind, 4 x svar data, var slots, var nonEmpty,
                            repeat nonEmpty: var slot, var itemPalette, u8 count, var damage
var nTicks; repeat: u16 index, var blockPalette, svar (due - worldTick), u8 priority
var nEntities; repeat: str type, var len, u8[len] payload
                            ("mob" payload v3: u8 3, str type, f64 x3 pos, f32 yaw, f32 health, var age,
                             u8 hasHome, f32 x3 home, u32 seed, svar growTicks, var loveTicks, var breedCooldown;
                             v1/v2 are prefixes of this layout and still load;
                             "cart" payload v1: u8 1, f64 x3 pos, f32 x3 vel, f32 yaw)
```

Records are compressed only when larger than 96 bytes and only if compression saves space.
Unknown block names decode as air with a warning (a removed block never corrupts the rest).

## Saving (journaled, multi-file atomic)

1. Every changed file is written in full to `<name>.new` and fsynced.
2. **Commit point:** `save.journal` (list of rename/delete ops + CRC) is written atomically.
3. Ops are applied (`rename .new -> final`, deletes), directories fsynced, journal removed.

On open: a valid journal is rolled forward; a corrupt journal is set aside; leftover `.new`/`.tmp`
files without a journal are discarded (the previous save is intact). Verified by
`storage_F_crash_recovery` at all three fault points.

Commits are serialised and sequence-numbered: a background autosave that reaches the commit after a
newer save has already been committed is dropped, never written over newer data
(`storage_stale_async_save_never_overwrites_newer_data`).

## Maintenance

`vxworld info|verify|optimize <dir> [--apply]` — `optimize` re-derives every record against the
regenerated baseline, converts full records to sparse and removes records equal to the baseline.
It is a dry run unless `--apply`, and `--apply` writes a timestamped backup first. A record that
cannot be decoded is left untouched.
