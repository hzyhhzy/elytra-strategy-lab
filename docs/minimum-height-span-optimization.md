# Minimum Steady-State Height-Span Optimization

This note records the most reliable method currently used for the following
problem:

> In periodic steady-state Elytra flight, require nonnegative net height change
> per cycle and minimize the difference between the highest and lowest points
> reached during that cycle.

The method was developed against the Java Edition `26.2` two-dimensional tick
model used by this repository. Positive strategy angle means nose-up. Final
scores always use the Java-compatible float constants and Minecraft sine-table
lookup; continuous trigonometry is used only as an optimization relaxation.

## Current result

The current best Java-exact feasible result is stored in
`results/periodic-vx025-no-drop`.

| Metric | Value |
|---|---:|
| Period | `162 tick` |
| Optimization dive/pull split before phase rotation | `101 / 61 tick` |
| Height span | `25.560603171784 blocks` |
| Net height change | `+4.3136613531e-8 blocks/cycle` |
| Horizontal distance | `131.701622067900 blocks/cycle` |
| Average horizontal speed | `16.259459514556 blocks/s` |
| Canonical start velocity | `(0.332244027135, 0.079995606319)` blocks/tick |
| Velocity closure error | `5.13e-16` |
| Pitch range | `[-89.537500, +55.658585] degrees` |

The published waveform is rotated so that the highest steady-state point is
the cycle origin. Rotation does not change a periodic policy or its score.

## Why the obvious local optimizers stall

Two separate nonsmooth effects matter here:

1. The objective `max(y_t) - min(y_t)` changes derivative whenever the active
   highest or lowest tick changes.
2. Minecraft's sine lookup makes force coefficients piecewise constant in the
   input angle, with table cells about `0.0055 degrees` wide. A finite
   difference smaller than a cell reports a zero gradient; a larger one mixes
   several discrete jumps.

Plain finite-difference L-BFGS-B therefore has unreliable gradients. A strict
one-frame coordinate search also stalls too early near `dy = 0`: a useful move
may temporarily lose height and only become feasible when paired with a second
move that restores it.

## Reliable optimization pipeline

### 1. Keep two evaluators

- **Java-exact evaluator:** float constants, Minecraft sine/cosine lookup,
  branch conditions, drag, and tick ordering matching the game. It is the only
  evaluator allowed to accept a final result.
- **Continuous relaxation:** the same update equations with continuous sine
  and cosine. It supplies useful local derivatives but never supplies the
  published score.

This separation is essential. Replacing the game model with a differentiable
approximation during final validation can make a slightly height-losing cycle
look feasible.

### 2. Solve the periodic velocity fixed point

For a waveform `a`, let one cycle map velocity `v` to `F(v, a)`. The steady
cycle starts from `v*` satisfying:

```text
v* = F(v*, a)
```

The map is contractive for the candidates studied here, so burn-in locates the
fixed point quickly. The gradient of that fixed point is computed implicitly:

```text
dv*/da = (I - dF/dv)^-1 dF/da
```

This avoids differentiating through hundreds of burn-in cycles. Local
Jacobians are propagated through one cycle, then a 2x2 linear system gives the
sensitivity of the periodic start velocity to every frame angle. Numerical
finite-difference checks agreed with the implicit gradient to roughly
`1e-9`--`1e-10` away from branch boundaries.

### 3. Use an epigraph for the height range

Do not optimize a single currently active maximum and minimum. Introduce upper
and lower envelope variables `U` and `L`:

```text
minimize    U - L

subject to  L <= y_t <= U       for every tick t
            dy_cycle = 0
            -90 <= a_t <= 0     during the dive phase
              0 <= a_t <= 90    during the pull-up phase
```

At the optimum, the nonnegative-height constraint is active, so solving
`dy_cycle = 0` is more stable than a large soft penalty. SLSQP works well on
this formulation when supplied with the implicit analytic Jacobian. Multiple
equal-height extrema are handled simultaneously by the envelope constraints.

### 4. Search discrete structure explicitly

Period and dive/pull split are integer variables and cannot be discovered by a
single fixed-size local solve. The robust structural search is:

1. optimize one period/split fully;
2. generate neighboring periods by deleting each possible frame or inserting
   a duplicate/interpolated frame at each possible position;
3. screen those seeds cheaply;
4. project the best seeds back onto `dy = 0` with the epigraph solve;
5. retain several branches and continue outward in period.

This continuation found the useful `163 tick` basin and then the better
`162 tick` solution. Direct resampling alone often destroyed the short zero
holds and missed that branch. Periods `158..170` were checked around the final
basin; the best point was at `162 tick`, with `163 tick` nearly tied and both
shorter and longer periods worse.

### 5. Use multi-starts for topology, not only random frame noise

Useful starts should change the control shape at several scales:

- low-frequency phase noise with different control counts and amplitudes;
- nonlinear time warps of the dive and pull-up phases;
- blends with independently generated smooth profiles;
- different split positions;
- completely independent profiles for a smaller subset of runs.

Twenty-four such starts over splits `95..108` returned to the same basin or to
worse local optima. This is evidence of robustness, not a proof of global
optimality.

### 6. Project back to Minecraft's lookup table

The continuous optimum generally loses a few thousandths of a block per cycle
when evaluated with Java's table lookup. Trace a family of continuous solutions
with a small positive target `dy`, then choose the lowest Java-exact feasible
member. A fine target sweep is much more reliable than assuming a fixed safety
margin.

### 7. Refine the exact discrete solution jointly

After projection, use only Java-exact scores:

- one-frame coordinate moves;
- pair moves that trade height loss against height gain;
- triple moves for the same reason;
- a local mixed-integer model that chooses many table-step moves at once,
  limits one move per tick, and enforces a linearized `dy >= 0` constraint;
- exact simulation of every proposed combination before acceptance.

For the final candidate, pair/triple search and mixed-integer selections up to
128 simultaneous frames found no further accepted move. Large discrete steps
from `0.3` to `5 degrees` were also tested.

## Practical lessons

- **A coordinate-search stop is not a local-optimality certificate.** Near an
  active height constraint, improvements usually require coordinated moves.
- **Do not use a soft height penalty as the final constraint.** Penalty weights
  either allow a small loss or overwhelm the useful objective gradient.
- **Direct `max-min` gradients become unstable at tied extrema.** The epigraph
  form is slower per iteration but substantially more dependable.
- **Optimize in a smooth relaxation, validate in the game model.** Using one
  evaluator for both jobs gives either bad gradients or an inaccurate score.
- **Period search needs insertion/deletion continuation.** Merely resampling a
  waveform can erase the feature that identifies its basin.
- **Longer periods do not automatically reveal repeated shorter solutions.**
  The optimizer must be seeded or continued in a way that can preserve and
  discover repeated structure.
- **Phase rotation is free only after velocity closure.** Rotate the published
  waveform only after confirming the periodic fixed point; then the highest
  point can safely be chosen as frame 0.
- **More sign changes were not helpful here.** Explicit two-dive/two-pull
  candidates converged to height spans around `29 blocks` or worse, well above
  the single-dive solution.
- **Keep the exact constants.** In particular, Java drag values
  `0.9900000095367432` and `0.9800000190734863` measurably affect tight
  feasibility decisions.

## Confidence and limitations

The current solution is a strong local result. It survived analytic-gradient
checks, epigraph KKT refinement, neighboring period/split continuation,
multi-start topology searches, explicit multi-phase searches, Java-exact
pair/triple refinement, and mixed-integer multi-frame proposals. It is not a
mathematical global-optimality proof. A proof would require a global
branch-and-bound or interval/dynamic-programming bound over both the velocity
state and Minecraft sine-table control states, which is much more expensive
than the local methods used here.
