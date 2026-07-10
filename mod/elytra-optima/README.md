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

If Mod Menu is installed, Elytra Optima provides a config button on the mod details screen. The config screen can enable/disable strategies, choose the `J` key cycle order, and choose the default strategy selected when `H` turns the mod on.

## Built-In Strategies

- `start_plus_0`: `Start +0 (>32 m)` / `起步+0（>32m）`; `208 tick`, returns to the launch height from rest.
- `start_plus_2`: `Start +2 (>35 m)` / `起步+2（>35m）`; `217 tick`, reaches at least `+2` blocks from rest.
- `vx025_no_drop`: `Minimum start with initial speed (25.56 m)` / `有初速最低起步（25.56m）`; `162 tick`, periodic minimum-start-height strategy with height span about `25.560603 m` and cycle height change about `+0.0000000431`.
- `periodic_gain1`: `Height +1 (28 m span)` / `高度+1（落差28m）`; `179 tick`, periodic strategy with cycle height change about `+1.003`.
- `smooth_max_climb`: `No-chatter max climb (1.553 m/s, start >74 m)` / `无抖最大升速（1.553m/s，起步>74m）`; `254 tick`, unrestricted per-frame climb rate `1.552981 b/s`, with real phase jumps and four circular direction changes over the cycle.
- `jagged_max_climb_255`: `Max-climb optimum (per-frame jitter allowed; 1.562 m/s, start >75 m)` / `最大升速最优解（不禁止逐帧抖动；1.562m/s，起步>75m）`; `255 tick`, Java-exact unrestricted optimum with climb rate `1.561551 b/s`.
- `smooth_horizontal`: `No-chatter horizontal (33.011 m/s, start >135 m)` / `无抖最快水平（33.011m/s，起步>135m）`; `357 tick`, non-dropping horizontal strategy without rapid pitch reversals.
- `hard_speed`: `Horizontal optimum (per-frame jitter allowed; 33.022 m/s, start >135 m)` / `最快水平最优解（不禁止逐帧抖动；33.022m/s，起步>135m）`; `357 tick`, Java-exact unrestricted optimum with horizontal speed `33.022449 b/s` and cycle height change `+5.90e-8`.

All built-in strategies are stored as per-frame CSV resources under `src/main/resources/assets/elytra_optima/strategies/`, with `tick,angle` columns. The `angle` column uses the simulator convention: positive means nose-up.

All periodic resources are phase-rotated so tick `0` is the highest steady-state point. A maximum at the cycle endpoint is equivalent to tick `0` of the next cycle.

Every time Elytra Optima is turned on, it starts on the configured default strategy. On first launch, the mod writes `config/elytra-optima.json`:

```json
{
  "defaultStrategy": "start_plus_0",
  "cycleOrder": [
    "start_plus_0",
    "start_plus_2",
    "vx025_no_drop",
    "periodic_gain1",
    "smooth_max_climb",
    "jagged_max_climb_255",
    "smooth_horizontal",
    "hard_speed"
  ]
}
```

Edit `cycleOrder` to choose which strategies `J` cycles through and in what order. Edit `defaultStrategy` to choose the strategy selected whenever `H` turns the mod on. Invalid ids are ignored, and the default strategy is inserted into the cycle order if it is missing.

Minecraft pitch is the opposite sign, so the mod applies:

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
