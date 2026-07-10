# Solvers

These are the representative optimizer sources used during the search.

## Periodic segmented search

```powershell
cl /nologo /O2 /std:c++20 /EHsc /Fe:segmented_sampled_optimize.exe segmented_sampled_optimize.cpp
.\segmented_sampled_optimize.exe ..\scratch\speed speed 10 0 60000 180 3800 0x5A9E1ED
.\segmented_sampled_optimize.exe ..\scratch\climb climb 10 0 60000 180 3800 0x5A9E1ED
```

The historical segmented results and current framewise results are under `../results/`. The current headline variants are:

- `../results/fastest-horizontal-speed`
- `../results/fastest-horizontal-speed-smooth`
- `../results/lbfgsb-max-climb-raw`
- `../results/fastest-climb-rate`
- `../results/periodic-vx025-no-drop`

Because the optimizer is stochastic, long reruns may find slightly different or better candidates.

## Per-frame max-climb candidate generation

This reproduces the continuous-trigonometry per-frame candidate that seeded the later Java-exact search.

It uses direct per-frame L-BFGS-B optimization after burn-in to periodic steady state. The first `165` ticks are bounded to nose-down or level pitch, and the remaining `90` ticks are bounded to nose-up or level pitch. No smoothness penalty is applied, so the result has severe pitch jitter.

```powershell
python lbfgsb_max_climb.py --period 255 --split 165
```

The script writes its rerun outputs to `../results/lbfgsb-max-climb-lbfgsb-run`. The deployable `1.561550761 blocks/s` result is the Java-exact refinement, not the continuous `1.562324772` seed.

## Java-exact coordinated refinement

`milp_exact_objective_refine.py` enumerates quantized per-frame moves and uses a small mixed-integer program to coordinate moves that trade objective gain against the active height constraint. Endpoint moves are included so the search can discover `0/90-degree` duty-cycle control.

```powershell
python milp_exact_objective_refine.py --source ..\results\fastest-horizontal-speed\waveform.csv --split 271 --mode horizontal --out-dir ..\scratch\horizontal-exact --include-endpoints
python milp_exact_objective_refine.py --source ..\results\lbfgsb-max-climb-raw\waveform.csv --split 165 --mode climb --out-dir ..\scratch\climb-exact --include-endpoints --bound-margin 0.01
```

`structural_period_exact.py` generates one-frame insertion/deletion continuations for neighboring periods. Both scripts use `min_height_span_periodic.py` as their Java-exact periodic evaluator.

## Jump-preserving no-chatter variants

The current no-chatter climb result is reproduced by `no_chatter_reversal_regularized_climb.py`. It optimizes every frame with L-BFGS-B, uses a jump-preserving reversal loss, limits significant circular direction changes to four, and finishes with Java-exact coordinate refinement. The older segmented candidate and its C++ tools remain for comparison.

```powershell
python no_chatter_reversal_regularized_climb.py --sources smooth --smooth-source ..\results\fastest-climb-rate\seed-segmented-1.547442102.csv --lags 1,2,3,4,5,6,7,8 --lambdas 0.015,0.005,0 --max-direction-changes 4 --direction-threshold 0.001 --coordinate-steps 1,0.3,0.1,0.03,0.01,0.003,0.001 --coordinate-sweeps 6 --out-dir ..\scratch\climb-no-chatter
```

For horizontal speed, `optimize_smooth_correction.py` applies total-variation trend filtering, then optimizes only a low-frequency cubic correction. It permits real phase jumps while preventing per-tick alternation from returning.

```powershell
python optimize_smooth_correction.py --source ..\results\fastest-horizontal-speed\waveform.csv --split 271 --mode horizontal --filter tv --tv-weight 23 --spacing 8 --out-dir ..\scratch\horizontal-smooth
```

Final checked-in waveforms and exact metrics are canonical under `../results/`.

## Historical periodic local audit

```powershell
cl /nologo /O2 /std:c++20 /EHsc /Fe:audit_segmented_local.exe audit_segmented_local.cpp
```

The current framewise `best_params.csv` files are metric manifests; the old segmented auditor applies only when supplied with archived segmented parameter files, not the current result manifests.

## Nonperiodic from-rest search

```powershell
cl /nologo /O2 /std:c++20 /EHsc /Fe:nonperiodic_return_optimize.exe nonperiodic_return_optimize.cpp
.\nonperiodic_return_optimize.exe ..\scratch\return-height 5000000 12 20260706 0
.\nonperiodic_return_optimize.exe ..\scratch\gain-two 5000000 12 20260706 2
```

The target height is the last argument.

## Exploratory parameterizations

- `fourier_optimize.cpp`: low-order periodic Fourier exploration.
- `bspline_optimize.cpp`: smooth B-spline exploration.
- `framewise_optimize.cpp`: direct per-frame refinement.

These were used to discover the broad shape of useful policies before the segmented Bezier family was chosen.
