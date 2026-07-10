# Solver Method

This note describes the search route that led to the current periodic strategies. It is intentionally separate from the main README so the project page can stay readable.

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

Three current objectives are retained:

- maximize average horizontal speed subject to nonnegative height change,
- maximize average climb rate,
- minimize the steady-state height span subject to nonnegative height change (equivalently, minimum start height with the optimized initial speed).

The final candidates are in:

- `results/fastest-horizontal-speed`
- `results/fastest-horizontal-speed-smooth`
- `results/lbfgsb-max-climb-raw`
- `results/fastest-climb-rate`
- `results/periodic-vx025-no-drop`

The relevant sources are:

- `solvers/segmented_sampled_optimize.cpp`
- `solvers/audit_segmented_local.cpp`

## 7. Java-exact coordinated frame refinement

The strongest recent candidates are not limited to the historical segmented
Bezier family. Per-frame candidates are first generated in a smooth relaxation,
then accepted and refined only with the Java-exact float lookup model.

For an active constraint such as `dy >= 0`, a one-frame coordinate move often
cannot improve the objective: a speed-improving move may lose a tiny amount of
height and needs another frame to compensate. The final refinement therefore
enumerates Java-quantized per-frame moves and uses small mixed-integer programs
to select coordinated groups of frames. Every selected group is re-evaluated in
the full nonlinear periodic simulator before it is accepted.

No smoothness penalty is used for the optimum where per-frame jitter is allowed. Endpoint moves such as
`0/90-degree` duty-cycle control are explicitly allowed because rapid
alternation can be a real discrete-time optimum rather than numerical noise.

## 8. Jump-preserving no-chatter variants

The practical variants remove rapid back-and-forth pitch reversals without
requiring the whole waveform to be continuous.

For maximum climb, the current no-chatter result no longer uses a forced
segment family. All `254` frame angles are optimized by L-BFGS-B. A robust
reversal loss penalizes alternating finite differences without penalizing a
single real jump, and Java-exact coordinate refinement accepts only waveforms
with at most four circular pitch-direction changes above `0.001 degrees/tick`.
It reaches `1.552981247 blocks/s`.

For horizontal speed, the pipeline is:

1. total-variation trend filtering to collapse high-frequency alternation;
2. low-frequency cubic correction while retaining the physical objective and,
   for horizontal flight, the `dy >= 0` constraint;
3. short monotone bridges for any residual staircase chatter.

Real phase changes remain instantaneous. The resulting tradeoffs are:

- horizontal: `33.022449116` to `33.011007670 blocks/s` (`0.03465%` loss);
- climb: `1.561550761` to `1.552981247 blocks/s` (`0.5488%` loss).

## 9. Nonperiodic from-rest searches

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

## 10. Constrained height-span search

The minimum steady-state height-span problem needs a more careful constrained
pipeline than the original exploratory searches. Its current method and the
lessons learned from finite-difference, active-extremum, quantization, period,
and multi-phase searches are documented separately in
[minimum-height-span-optimization.md](minimum-height-span-optimization.md).

## 11. Replotting results

Run:

```powershell
python scripts/refresh_latest_strategy_metadata.py
python scripts/plot_latest_three_tasks.py
```

This regenerates the Chinese and English images under `docs/images` from the result CSV files.
