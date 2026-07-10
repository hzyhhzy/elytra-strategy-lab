# Elytra Optima

**Elytra Optima** is a client-side Fabric mod for Minecraft `26.2` that applies highly optimized Elytra pitch-control strategies while you are already gliding.

This mod only changes the player's pitch angle. It does **not** directly modify position, velocity, durability, physics constants, or movement packets.

The main point of this project is the strategy search itself. The mod is intentionally small and its features and compatibility are not as polished as a full utility mod yet. What it provides is a convenient way to try a set of unusually optimized Elytra flight profiles in-game. Other Elytra, movement, or automation mods are welcome to study and reuse the strategy data, simulator, solver code, and plots.

Source code, simulator, data, solver notes, and issue tracker:

https://github.com/hzyhhzy/elytra-strategy-lab

## Controls

- `H`: toggle Elytra Optima.
- `J`: cycle strategy.

Every time the mod is turned on, it starts from the minimum-start-height strategy. Strategy timing is based on Elytra flight time and pauses when the game is paused.

## Strategy Results

The current headline results cover three periodic objectives. Maximum climb and fastest horizontal flight each include both the unrestricted optimum, where per-frame jitter is allowed, and a jump-preserving no-chatter version.

### 1. Minimum start height with initial speed

This periodic strategy minimizes the steady-state height span while keeping the cycle height nonnegative. Viewed from the highest point of the cycle, it is the minimum start height for the optimized initial velocity.

- Period: `162 tick`
- Height span / minimum start height: `25.560603 m`
- Cycle height change: about `+4.31e-8 m`
- The published waveform is already smooth

![Minimum start height with initial speed](https://raw.githubusercontent.com/hzyhhzy/elytra-strategy-lab/main/docs/images/periodic-vx025-no-drop-en.png)

### 2. Maximum steady-state climb

The unrestricted per-frame result reaches `1.561550761 m/s` in the Java-exact model and contains strong pitch alternation. The no-chatter version retains real phase jumps but removes rapid back-and-forth motion.

- Jittery optimum: `1.561550761 m/s`, `+19.909772 m/cycle`
- No-chatter unrestricted per-frame version: `1.552981247 m/s`, `+19.722862 m/cycle`
- Periods: `255 tick` jittery / `254 tick` no-chatter

![Jittery maximum climb](https://raw.githubusercontent.com/hzyhhzy/elytra-strategy-lab/main/docs/images/lbfgsb-max-climb-raw-en.png)

![No-chatter maximum climb](https://raw.githubusercontent.com/hzyhhzy/elytra-strategy-lab/main/docs/images/fastest-climb-rate-en.png)

### 3. Fastest steady-state horizontal flight with nonnegative height

This objective maximizes average horizontal speed while keeping the cycle's net height change nonnegative. The no-chatter version is only `0.03465%` slower than the unrestricted optimum.

- Jittery optimum: `33.022449116 m/s`, dy about `+5.90e-8 m`
- No-chatter version: `33.011007670 m/s`, dy about `+0.000630 m`
- Period: `357 tick`

![Jittery fastest horizontal flight](https://raw.githubusercontent.com/hzyhhzy/elytra-strategy-lab/main/docs/images/fastest-horizontal-speed-en.png)

![No-chatter fastest horizontal flight](https://raw.githubusercontent.com/hzyhhzy/elytra-strategy-lab/main/docs/images/fastest-horizontal-speed-smooth-en.png)

## What This Mod Is, And Is Not

This is a pitch-only Elytra strategy mod. It is useful for testing and demonstrating optimized flight profiles, but it is not trying to be a complete Elytra automation suite.

Known scope limits:

- Client-side pitch control only.
- No pathfinding, obstacle avoidance, landing logic, or firework management.
- No direct velocity or position edits.
- Built for the pinned Minecraft/Fabric versions in the repository.

The value of the project is that the strategies were searched and verified with a reproducible simulator. The repository includes:

- the Elytra tick-model simulator,
- strategy CSV files,
- trajectory CSV files,
- solver source code,
- plots,
- a direct-open web simulator,
- and the Fabric mod source.

If you are building a more polished Elytra mod, route planner, bot, or movement experiment, the strategy data here should be a useful reference point.

## Links

- GitHub: https://github.com/hzyhhzy/elytra-strategy-lab
- Solver notes: https://github.com/hzyhhzy/elytra-strategy-lab/blob/main/docs/solver-method.md
