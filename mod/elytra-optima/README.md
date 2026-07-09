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
- `vx025_no_drop`: `Initial-speed no-drop (26 m span)` / `有初速不掉高（落差26m）`; `170 tick`, periodic strategy with initial horizontal speed, cycle height change about `+0.00014`.
- `periodic_gain1`: `Height +1 (28 m span)` / `高度+1（落差28m）`; `179 tick`, periodic strategy with cycle height change about `+1.003`.
- `smooth_max_climb`: `Smooth fastest climb (20 m/cycle, start >75 m)` / `平滑最大提升速度（20m/cycle，起步高度>75m）`; `254 tick`, climb rate about `+1.547 b/s`, cycle height change about `+19.653`.
- `jagged_max_climb_255`: `Jittery fastest climb (20 m/cycle, start >75 m)` / `抖动最大提升速度（20m/cycle，起步高度>75m）`; `255 tick`, raw per-frame L-BFGS-B climb reference, climb rate about `+1.562 b/s`.
- `hard_speed`: `Fastest horizontal (33 m/s, start >142 m)` / `最快水平速度（33m/s，起步高度>142m）`; `357 tick`, steady-state horizontal speed about `32.993 b/s`, cycle height change about `+0.000061`.

All built-in strategies are stored as per-frame CSV resources under `src/main/resources/assets/elytra_optima/strategies/`, with `tick,angle` columns. The `angle` column uses the simulator convention: positive means nose-up.

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
