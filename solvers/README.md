# Solvers

These are the representative optimizer sources used during the search.

## Periodic segmented search

```powershell
cl /nologo /O2 /std:c++20 /EHsc /Fe:segmented_sampled_optimize.exe segmented_sampled_optimize.cpp
.\segmented_sampled_optimize.exe ..\scratch\speed speed 10 0 60000 180 3800 0x5A9E1ED
.\segmented_sampled_optimize.exe ..\scratch\climb climb 10 0 60000 180 3800 0x5A9E1ED
```

The periodic results currently checked in are already under:

- `../results/fastest-horizontal-speed`
- `../results/fastest-climb-rate`

Because the optimizer is stochastic, long reruns may find slightly different or better candidates.

## Periodic local audit

```powershell
cl /nologo /O2 /std:c++20 /EHsc /Fe:audit_segmented_local.exe audit_segmented_local.cpp
.\audit_segmented_local.exe speed-hard ..\results\fastest-horizontal-speed\best_params.csv 10000 0xA11D17 ..\scratch\speed-audit
.\audit_segmented_local.exe climb ..\results\fastest-climb-rate\best_params.csv 10000 0xA11D17 ..\scratch\climb-audit
```

The periodic result folders also include the original `best_params.csv` used by the auditor.

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
