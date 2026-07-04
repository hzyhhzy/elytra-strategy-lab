# Elytra Optima Fabric Mod

Client-side Fabric mod for Minecraft Java `26.2`.

Elytra Optima applies precomputed pitch-only flight strategies while the player is already using Elytra flight. It does not change Minecraft's physics, velocity, durability, networking, or inventory logic.

Metadata:

- Mod id: `elytra_optima`
- Author: `hzyhhzy`, `Codex`
- Icon: `assets/elytra_optima/icon.png`, with duplicate root `icon.png` for launcher compatibility

## Version Pins

- Minecraft: `26.2`
- Fabric Loader: `0.19.3`
- Fabric API: `0.154.0+26.2`
- Gradle: `9.5.0`
- Java: `25`

The build uses a no-remap Java compile path against the readable `26.2` client jar downloaded by Fabric Loom, because Yarn metadata for `26.2` returns an empty list and Fabric Loom could not find official Mojang mappings for `26.2`.

## Controls

- `H`: toggle Elytra Optima.
- `J`: switch strategy.

When enabled before flight starts, Elytra Optima starts the selected cycle from tick 0 at the moment Elytra flight begins. If the player stops fall-flying, the cycle counter resets. If the game is paused, the strategy clock is paused too.

## Built-In Strategies

- `gain2_min_drop`: `Min-start height (>35 m)` / `最小高度起步（>35m）`; uses the `217 tick` minimum-start-height sequence as a repeating cycle.
- `max_climb`: `Fastest climb (20 m/cycle, start >75 m)` / `最大提升速度（20m/cycle，起步高度>75m）`; `254 tick`, climb rate about `+1.547 b/s`, cycle height change about `+19.653`.
- `hard_speed`: `Fastest horizontal (33 m/s, start >142 m)` / `最快水平速度（33m/s，起步高度>142m）`; `357 tick`, steady-state horizontal speed about `32.993 b/s`, cycle height change about `+0.000061`.

Every time Elytra Optima is turned on, it starts on `gain2_min_drop`. Pressing the strategy key cycles:

```text
gain2_min_drop -> max_climb -> hard_speed -> gain2_min_drop
```

The strategy angle convention is the simulator convention: positive means nose-up. Minecraft pitch is the opposite sign, so the mod applies:

```text
player_pitch = -strategy_angle
```

## Build

From this directory in the repository:

```powershell
$env:JAVA_HOME = (Resolve-Path ..\..\..\.tools\jdk25).Path
& ..\..\..\.tools\gradle-9.5.0\bin\gradle.bat --no-daemon build
```

The jar will be under `build/libs/` as `elytra-optima-1.0.0.jar`.
