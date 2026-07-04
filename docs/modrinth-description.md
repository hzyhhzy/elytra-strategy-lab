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

The full project currently includes four optimized strategy results. The in-game mod exposes the most practical looping profiles, and the web simulator in the GitHub repository includes all four.

### 1. Minimum-start-height launch, gain at least 2 blocks

This strategy starts from rest and minimizes the initial height needed to complete a launch that ends at least 2 blocks higher than the starting height.

- Minimum initial height: about `35.12 m`
- Target: `217 tick`
- Horizontal distance at target: about `162.93 m`
- In-game role: default launch / minimum-start-height profile

![Minimum-start-height launch, gain at least 2 blocks](https://raw.githubusercontent.com/hzyhhzy/elytra-strategy-lab/main/docs/images/from-rest-gain-two-en.png)

### 2. Minimum-start-height launch, return to original height

This is the companion from-rest result for the `start +0` condition: it minimizes the starting height needed to return to the original height without net height loss.

- Minimum initial height: about `32.35 m`
- Return time: `208 tick`
- Horizontal distance at return: about `150.94 m`
- Useful as a baseline for low-height launch behavior

![Minimum-start-height launch, return to original height](https://raw.githubusercontent.com/hzyhhzy/elytra-strategy-lab/main/docs/images/from-rest-return-height-en.png)

### 3. Fastest steady-state climb

This periodic strategy is optimized for average height gain per second after reaching a repeated cycle. It is not just a hand-tuned curve; it comes from numerical search over the Elytra tick model and then segmented-curve refinement.

- Period: `254 tick`
- Height gain: about `19.65 m/cycle`
- Average climb rate: about `1.55 m/s`
- Average horizontal speed: about `22.73 m/s`
- Recommended start height: above about `75 m`

![Fastest steady-state climb](https://raw.githubusercontent.com/hzyhhzy/elytra-strategy-lab/main/docs/images/fastest-climb-rate-en.png)

### 4. Fastest steady-state horizontal flight with nonnegative height

This periodic strategy maximizes average horizontal speed while keeping the cycle's net height change nonnegative. In other words, it is a high-speed cruise profile that does not rely on losing height over the repeated cycle.

- Period: `357 tick`
- Average horizontal speed: about `32.99 m/s`
- Net cycle height change: about `+0.00006 m`
- Recommended start height: above about `142 m`

![Fastest steady-state horizontal flight](https://raw.githubusercontent.com/hzyhhzy/elytra-strategy-lab/main/docs/images/fastest-horizontal-speed-en.png)

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
