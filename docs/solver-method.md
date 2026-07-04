# Solver Method

This note describes the search route that led to the current segmented strategies. It is intentionally separate from the main README so the project page can stay readable.

## 1. Exact simulator first

All optimizers use the same 20 tick/s two-dimensional Elytra simulator:

- horizontal position and velocity
- vertical position and velocity
- fixed yaw
- Minecraft-style float sine/cosine lookup
- the Elytra constants used in Java Edition physics

This made the objective deterministic and fast enough for large local searches.

## 2. Fixed-pitch and two-stage baselines

The first sanity checks were simple:

- fixed pitch until terminal balance
- two-stage cycles: dive for `n` ticks, pull up for `m` ticks
- heatmaps over stage durations for fixed pitch pairs

These runs were useful for debugging the simulator and for discovering that good policies had a recognizable dive-then-pull-up shape, but they were too rigid.

## 3. Fourier parameterizations

The next representation was a periodic Fourier series:

```text
angle(t) = mean + sum_k a_k cos(2*pi*k*t/T) + b_k sin(2*pi*k*t/T)
```

Orders 3, 5, 7, 9, 20, and higher were tried. Low order gave smooth candidates; high order could improve scores but tended to create jitter and large frame-to-frame angle jumps.

Adding first-difference L1 regularization reduced some jitter, but the representation still spent too much effort describing local discontinuities.

## 4. B-spline and framewise refinement

B-spline curves were then tried to get smoother periodic controls with fewer degrees of freedom. Framewise optimization was also used as a local refiner, sometimes with L1 penalties on angle differences.

These methods were good at revealing the shape of high-scoring strategies:

- an early dive or near-vertical negative pitch,
- a long negative segment that gradually becomes more aggressive,
- a short near-zero transition,
- a strong positive pull-up,
- then a decay back toward level flight.

## 5. Human-observed segmented family

After observing the optimized curves, the control was made explicit as eight segments:

1. negative-angle hold
2. linear transition into the negative curve
3. negative Bezier curve
4. zero hold
5. linear ramp to positive angle
6. positive hold
7. positive Bezier curve to zero
8. final zero hold

The Bezier x coordinates are fixed with cosine endpoint-dense spacing:

```text
x_i = 0.5 - 0.5 * cos(pi * i / (controlCount - 1))
```

The optimizer controls durations, the negative hold angle, and the y coordinates of the Bezier controls. The final chosen family uses 8 controls for each Bezier segment and allows nonmonotone control values.

## 6. Periodic objective searches

The periodic optimizer simulates multiple cycles until the velocity state becomes periodic enough, then scores a single steady-state cycle.

Two main objectives were retained:

- maximize average horizontal speed subject to nonnegative height change,
- maximize average climb rate.

The final candidates are in:

- `results/fastest-horizontal-speed`
- `results/fastest-climb-rate`

The relevant sources are:

- `solvers/segmented_sampled_optimize.cpp`
- `solvers/audit_segmented_local.cpp`

## 7. Nonperiodic from-rest searches

For launch-from-rest problems, the initial state is:

```text
x = 0, y = 0, vx = 0, vy = 0
```

The goal is to minimize the maximum drop before first reaching a target height:

- target `y >= 0`: return to original height,
- target `y >= 2`: gain at least 2 blocks.

The same segmented Bezier family is used, but the run is nonperiodic and stops at the first target-crossing tick.

The final candidates are in:

- `results/from-rest-return-height`
- `results/from-rest-gain-two`

The relevant source is:

- `solvers/nonperiodic_return_optimize.cpp`

## 8. Replotting results

Run:

```powershell
python scripts/plot_quadrants.py
```

This regenerates the four images under `docs/images` from the result CSV files.

