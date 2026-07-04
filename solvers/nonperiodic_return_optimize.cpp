#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int TICKS_PER_SECOND = 20;
constexpr int NEG_CONTROLS = 8;
constexpr int POS_CONTROLS = 8;
constexpr double PI = 3.141592653589793238462643383279502884;
constexpr double GRAVITY = -0.08;
constexpr double LIFT = 0.06;
constexpr double FALL_TO_GLIDE = -0.1;
constexpr double PITCH_UP_X = 0.04;
constexpr double PITCH_UP_Y = 3.2;
constexpr double HORIZONTAL_ALIGN = 0.1;
constexpr double HORIZONTAL_DRAG = 0.9900000095367432;
constexpr double VERTICAL_DRAG = 0.9800000190734863;
constexpr float DEG_TO_RAD = static_cast<float>(PI / 180.0);
constexpr float RAD_TO_INDEX = 10430.378f;

std::array<float, 65536> sinTable;

double clamp(double value, double lo, double hi) {
  return std::max(lo, std::min(hi, value));
}

float mthSin(double value) {
  const float v = static_cast<float>(static_cast<float>(value) * RAD_TO_INDEX);
  return sinTable[static_cast<int>(std::trunc(v)) & 65535];
}

float mthCos(double value) {
  const float v = static_cast<float>(static_cast<float>(value) * RAD_TO_INDEX + 16384.0f);
  return sinTable[static_cast<int>(std::trunc(v)) & 65535];
}

struct State {
  double x = 0.0;
  double y = 0.0;
  double vx = 0.0;
  double vy = 0.0;
};

struct TickParams {
  double pitchRad = 0.0;
  double sinPitch = 0.0;
  double horizontalLookLength = 0.0;
  double lift = 0.0;
  double lookSign = 1.0;
};

struct Eval {
  bool returned = false;
  double drop = std::numeric_limits<double>::infinity();
  int minTick = 0;
  int returnTick = -1;
  double xAtReturn = 0.0;
  double yAtReturn = 0.0;
  double vxAtReturn = 0.0;
  double vyAtReturn = 0.0;
  double maxY = -std::numeric_limits<double>::infinity();
  double finalX = 0.0;
  double finalY = 0.0;
  double finalVx = 0.0;
  double finalVy = 0.0;
};

struct Params {
  int n1 = 0;
  int n12 = 0;
  int n2 = 128;
  int n3 = 14;
  int n34 = 5;
  int nPosHold = 0;
  int n4 = 271;
  int n5 = 40;
  double negHoldAngle = -88.619359825549679;
  std::array<double, NEG_CONTROLS> neg = {
      -0.01,
      -0.01,
      -0.01,
      -23.154733257869417,
      -15.716026296569407,
      -63.858448303099955,
      -9.9305515721390254,
      -49.105993352669849,
  };
  std::array<double, POS_CONTROLS> pos = {
      60.001952855162841,
      22.239818113109809,
      8.794913186308051,
      5.5590291648426806,
      0.065126229132976804,
      17.541921762239856,
      60.365737263388233,
      0.0,
  };
};

struct Candidate {
  Params params;
  Eval eval;
};

double targetY = 0.0;

double score(const Eval& e) {
  if (e.returned) {
    return e.drop + 1e-5 * static_cast<double>(e.returnTick) + 1e-8 * std::max(0.0, e.yAtReturn - targetY);
  }
  const double missing = std::max(0.0, targetY - e.maxY);
  return 1e9 + e.drop + 1000.0 * missing + 10.0 * std::max(0.0, -e.finalVy);
}

bool betterEval(const Eval& a, const Eval& b) {
  const double sa = score(a);
  const double sb = score(b);
  if (sa != sb) return sa < sb;
  if (a.returned != b.returned) return a.returned;
  return a.yAtReturn > b.yAtReturn;
}

int totalTicks(const Params& p) {
  return p.n1 + p.n12 + p.n2 + p.n3 + p.n34 + p.nPosHold + p.n4 + p.n5;
}

void enforce(Params& p) {
  p.n1 = static_cast<int>(std::llround(clamp(p.n1, 0, 180)));
  p.n12 = static_cast<int>(std::llround(clamp(p.n12, 0, 80)));
  p.n2 = static_cast<int>(std::llround(clamp(p.n2, 1, 700)));
  p.n3 = static_cast<int>(std::llround(clamp(p.n3, 0, 240)));
  p.n34 = static_cast<int>(std::llround(clamp(p.n34, 0, 80)));
  p.nPosHold = static_cast<int>(std::llround(clamp(p.nPosHold, 0, 200)));
  p.n4 = static_cast<int>(std::llround(clamp(p.n4, 1, 520)));
  p.n5 = static_cast<int>(std::llround(clamp(p.n5, 0, 260)));
  p.negHoldAngle = clamp(p.negHoldAngle, -89.5, -0.01);
  for (double& value : p.neg) value = clamp(value, -89.5, -0.01);
  for (int i = 0; i < POS_CONTROLS - 1; ++i) p.pos[i] = clamp(p.pos[i], 0.0, 89.5);
  p.pos[POS_CONTROLS - 1] = 0.0;
}

TickParams makeTickParams(double angle) {
  const double clampedAngle = clamp(angle, -90.0, 90.0);
  const double mcPitchDeg = clamp(-clampedAngle, -90.0, 90.0);
  const float pitchRadF = static_cast<float>(static_cast<float>(mcPitchDeg) * DEG_TO_RAD);
  const double pitchRad = pitchRadF;
  const double lookH = mthCos(pitchRad);
  const double lookY = -mthSin(pitchRad);
  const double horizontalLookLength = std::abs(lookH);
  const double lookLength = std::hypot(horizontalLookLength, lookY);
  double lift = mthCos(pitchRad);
  lift = static_cast<float>(static_cast<float>(lift * lift) * std::min(1.0, lookLength / 0.4));
  return {pitchRad, mthSin(pitchRad), horizontalLookLength, lift, lookH >= 0.0 ? 1.0 : -1.0};
}

void tick(State& state, const TickParams& p) {
  const double oldHorizontalSpeed = std::abs(state.vx);
  double vx = state.vx;
  double vy = state.vy + GRAVITY + p.lift * LIFT;
  if (vy < 0.0 && p.horizontalLookLength > 0.0) {
    const double yAccel = vy * FALL_TO_GLIDE * p.lift;
    vy += yAccel;
    vx += p.lookSign * yAccel;
  }
  if (p.pitchRad < 0.0 && p.horizontalLookLength > 0.0) {
    const double climb = oldHorizontalSpeed * -p.sinPitch * PITCH_UP_X;
    vy += climb * PITCH_UP_Y;
    vx -= p.lookSign * climb;
  }
  if (p.horizontalLookLength > 0.0) {
    vx += (p.lookSign * oldHorizontalSpeed - vx) * HORIZONTAL_ALIGN;
  }
  state.vx = vx * HORIZONTAL_DRAG;
  state.vy = vy * VERTICAL_DRAG;
  state.x += state.vx;
  state.y += state.vy;
}

template <size_t N>
double controlPosition(int index) {
  if (N <= 1) return 0.0;
  const double u = static_cast<double>(index) / static_cast<double>(N - 1);
  return 0.5 - 0.5 * std::cos(PI * u);
}

template <size_t N>
double bezierValue(const std::array<double, N>& controls, double u) {
  std::array<double, N> work = controls;
  for (size_t level = N - 1; level > 0; --level) {
    for (size_t i = 0; i < level; ++i) {
      work[i] = work[i] * (1.0 - u) + work[i + 1] * u;
    }
  }
  return work[0];
}

template <size_t N>
double bezierX(double u) {
  std::array<double, N> xs{};
  for (int i = 0; i < static_cast<int>(N); ++i) xs[i] = controlPosition<N>(i);
  return bezierValue(xs, u);
}

template <size_t N>
const std::vector<double>& bezierParameterMap(int count) {
  static std::array<std::vector<double>, 701> cache;
  count = static_cast<int>(clamp(count, 1, 700));
  std::vector<double>& params = cache[count];
  if (!params.empty()) return params;
  params.resize(count);
  if (count == 1) {
    params[0] = 1.0;
    return params;
  }
  for (int i = 0; i < count; ++i) {
    const double targetX = static_cast<double>(i) / static_cast<double>(count - 1);
    double lo = 0.0;
    double hi = 1.0;
    for (int step = 0; step < 40; ++step) {
      const double mid = 0.5 * (lo + hi);
      if (bezierX<N>(mid) < targetX) {
        lo = mid;
      } else {
        hi = mid;
      }
    }
    params[i] = 0.5 * (lo + hi);
  }
  return params;
}

template <size_t N>
double bezierControl(const std::array<double, N>& controls, int i, int count) {
  const std::vector<double>& params = bezierParameterMap<N>(count);
  return bezierValue(controls, params[static_cast<size_t>(i)]);
}

std::vector<double> buildAngles(Params p) {
  enforce(p);
  std::vector<double> angles;
  angles.reserve(totalTicks(p));
  for (int i = 0; i < p.n1; ++i) angles.push_back(p.negHoldAngle);
  for (int i = 0; i < p.n12; ++i) {
    const double u = static_cast<double>(i + 1) / static_cast<double>(std::max(1, p.n12));
    angles.push_back(p.negHoldAngle + (p.neg[0] - p.negHoldAngle) * u);
  }
  for (int i = 0; i < p.n2; ++i) angles.push_back(bezierControl(p.neg, i, p.n2));
  for (int i = 0; i < p.n3; ++i) angles.push_back(0.0);
  for (int i = 0; i < p.n34; ++i) {
    const double u = static_cast<double>(i + 1) / static_cast<double>(std::max(1, p.n34));
    angles.push_back(p.pos[0] * u);
  }
  for (int i = 0; i < p.nPosHold; ++i) angles.push_back(p.pos[0]);
  for (int i = 0; i < p.n4; ++i) angles.push_back(bezierControl(p.pos, i, p.n4));
  for (int i = 0; i < p.n5; ++i) angles.push_back(0.0);
  return angles;
}

Eval evaluateAngles(const std::vector<double>& angles) {
  Eval out;
  State state;
  double minY = 0.0;
  for (int i = 0; i < static_cast<int>(angles.size()); ++i) {
    const TickParams tp = makeTickParams(angles[static_cast<size_t>(i)]);
    tick(state, tp);
    if (state.y < minY) {
      minY = state.y;
      out.minTick = i + 1;
    }
    out.maxY = std::max(out.maxY, state.y);
    if (i > 0 && state.y >= targetY) {
      out.returned = true;
      out.returnTick = i + 1;
      out.xAtReturn = state.x;
      out.yAtReturn = state.y;
      out.vxAtReturn = state.vx;
      out.vyAtReturn = state.vy;
      break;
    }
  }
  out.drop = -minY;
  out.finalX = state.x;
  out.finalY = state.y;
  out.finalVx = state.vx;
  out.finalVy = state.vy;
  if (!out.returned) {
    out.xAtReturn = state.x;
    out.yAtReturn = state.y;
    out.vxAtReturn = state.vx;
    out.vyAtReturn = state.vy;
  }
  return out;
}

Eval evaluateParams(const Params& p) {
  return evaluateAngles(buildAngles(p));
}

Params maxClimbSeed() {
  Params p;
  p.n1 = 2;
  p.n12 = 5;
  p.n2 = 141;
  p.n3 = 15;
  p.n34 = 4;
  p.nPosHold = 0;
  p.n4 = 83;
  p.n5 = 4;
  p.negHoldAngle = -83.44910138799413;
  p.neg = {-21.507408848406726, -31.064412051040822, -17.75203519264177, -63.83111872689089,
           -5.024003861649917, -83.57492395202101, -19.020736082912922, -62.2599218701962};
  p.pos = {80.88135214090949, 59.766059051049844, 26.760085044938684, 33.961915704001306,
           7.911228156821989, 22.564579953207456, 10.025017993419707, 0.0};
  enforce(p);
  return p;
}

double uniform(std::mt19937_64& rng, double lo, double hi) {
  return std::uniform_real_distribution<double>(lo, hi)(rng);
}

int uniformInt(std::mt19937_64& rng, int lo, int hi) {
  return std::uniform_int_distribution<int>(lo, hi)(rng);
}

Params randomSegmented(std::mt19937_64& rng) {
  Params p;
  p.n1 = uniformInt(rng, 0, 35);
  p.n12 = uniformInt(rng, 0, 18);
  p.n2 = uniformInt(rng, 50, 260);
  p.n3 = uniformInt(rng, 0, 60);
  p.n34 = uniformInt(rng, 0, 20);
  p.nPosHold = uniformInt(rng, 0, 30);
  p.n4 = uniformInt(rng, 30, 180);
  p.n5 = uniformInt(rng, 0, 160);
  p.negHoldAngle = uniform(rng, -89.5, -2.0);
  const double negStart = uniform(rng, -35.0, -0.01);
  const double negMid = uniform(rng, -75.0, -5.0);
  for (int i = 0; i < NEG_CONTROLS; ++i) {
    const double u = controlPosition<NEG_CONTROLS>(i);
    const double base = negStart * (1.0 - u) + negMid * u;
    p.neg[i] = base + uniform(rng, -28.0, 28.0);
  }
  const double posStart = uniform(rng, 15.0, 89.5);
  for (int i = 0; i < POS_CONTROLS; ++i) {
    const double u = controlPosition<POS_CONTROLS>(i);
    p.pos[i] = posStart * std::pow(1.0 - u, uniform(rng, 0.35, 2.2)) + uniform(rng, -24.0, 24.0);
  }
  p.pos[POS_CONTROLS - 1] = 0.0;
  enforce(p);
  return p;
}

Params mutate(Params p, std::mt19937_64& rng, double durationSigma, double angleSigma) {
  auto maybeDuration = [&](int& value) {
    if (uniform(rng, 0.0, 1.0) < 0.75) {
      value += static_cast<int>(std::llround(std::normal_distribution<double>(0.0, durationSigma)(rng)));
    }
  };
  maybeDuration(p.n1);
  maybeDuration(p.n12);
  maybeDuration(p.n2);
  maybeDuration(p.n3);
  maybeDuration(p.n34);
  maybeDuration(p.nPosHold);
  maybeDuration(p.n4);
  maybeDuration(p.n5);

  if (uniform(rng, 0.0, 1.0) < 0.80) {
    p.negHoldAngle += std::normal_distribution<double>(0.0, angleSigma)(rng);
  }
  if (uniform(rng, 0.0, 1.0) < 0.28) {
    const double shift = std::normal_distribution<double>(0.0, angleSigma)(rng);
    for (double& value : p.neg) value += shift;
  }
  if (uniform(rng, 0.0, 1.0) < 0.28) {
    const double shift = std::normal_distribution<double>(0.0, angleSigma)(rng);
    for (int i = 0; i < POS_CONTROLS - 1; ++i) p.pos[i] += shift;
  }
  for (double& value : p.neg) {
    if (uniform(rng, 0.0, 1.0) < 0.82) value += std::normal_distribution<double>(0.0, angleSigma)(rng);
  }
  for (int i = 0; i < POS_CONTROLS - 1; ++i) {
    if (uniform(rng, 0.0, 1.0) < 0.82) p.pos[i] += std::normal_distribution<double>(0.0, angleSigma)(rng);
  }
  enforce(p);
  return p;
}

void tryUpdate(Candidate& best, const Params& p) {
  const Eval e = evaluateParams(p);
  if (e.returned && betterEval(e, best.eval)) {
    best = {p, e};
  }
}

Candidate coordinatePolish(Candidate seed, int rounds, bool verbose) {
  Candidate best = seed;
  const std::vector<int> durationSteps = {30, 18, 10, 5, 2, 1};
  const std::vector<double> angleSteps = {10.0, 5.0, 2.0, 0.8, 0.25, 0.08};
  for (int round = 0; round < rounds; ++round) {
    for (size_t level = 0; level < durationSteps.size(); ++level) {
      bool improved = true;
      int passes = 0;
      while (improved && passes < 5) {
        improved = false;
        ++passes;
        const Candidate before = best;
        const int dStep = durationSteps[level];
        const double aStep = angleSteps[level];
        for (int field = 0; field < 8; ++field) {
          for (int sign : {-1, 1}) {
            Params p = best.params;
            int* durations[] = {&p.n1, &p.n12, &p.n2, &p.n3, &p.n34, &p.nPosHold, &p.n4, &p.n5};
            *durations[field] += sign * dStep;
            enforce(p);
            tryUpdate(best, p);
          }
        }
        for (double sign : {-1.0, 1.0}) {
          Params p = best.params;
          p.negHoldAngle += sign * aStep;
          enforce(p);
          tryUpdate(best, p);
        }
        for (int field = 0; field < NEG_CONTROLS; ++field) {
          for (double sign : {-1.0, 1.0}) {
            Params p = best.params;
            p.neg[field] += sign * aStep;
            enforce(p);
            tryUpdate(best, p);
          }
        }
        for (int field = 0; field < POS_CONTROLS - 1; ++field) {
          for (double sign : {-1.0, 1.0}) {
            Params p = best.params;
            p.pos[field] += sign * aStep;
            enforce(p);
            tryUpdate(best, p);
          }
        }
        improved = betterEval(best.eval, before.eval);
        if (verbose && improved) {
          std::cout << "polish round=" << round << " level=" << level << " pass=" << passes
                    << " drop=" << std::setprecision(12) << best.eval.drop
                    << " return=" << best.eval.returnTick
                    << " minTick=" << best.eval.minTick
                    << " total=" << totalTicks(best.params) << "\n";
        }
      }
    }
  }
  return best;
}

std::mutex globalMutex;
Candidate globalBest;
std::atomic<long long> evalCount{0};

Candidate snapshotBest() {
  std::lock_guard<std::mutex> lock(globalMutex);
  return globalBest;
}

void publishIfBetter(const Candidate& candidate, int workerId) {
  if (!candidate.eval.returned) return;
  std::lock_guard<std::mutex> lock(globalMutex);
  if (betterEval(candidate.eval, globalBest.eval)) {
    globalBest = candidate;
    std::cout << "worker=" << workerId
              << " evals=" << evalCount.load()
              << " drop=" << std::setprecision(12) << globalBest.eval.drop
              << " return=" << globalBest.eval.returnTick
              << " minTick=" << globalBest.eval.minTick
              << " total=" << totalTicks(globalBest.params)
              << " x=" << globalBest.eval.xAtReturn
              << " y=" << globalBest.eval.yAtReturn << "\n";
  }
}

void workerMain(int workerId, long long iterations, uint64_t seed) {
  std::mt19937_64 rng(seed + 0x9e3779b97f4a7c15ULL * static_cast<uint64_t>(workerId + 1));
  Candidate local = snapshotBest();
  Candidate current = local;
  for (long long i = 1; i <= iterations; ++i) {
    const double frac = 1.0 - static_cast<double>(i) / static_cast<double>(std::max<long long>(1, iterations));
    const double durationSigma = 0.75 + 55.0 * std::pow(std::max(0.0, frac), 1.45);
    const double angleSigma = 0.05 + 18.0 * std::pow(std::max(0.0, frac), 1.25);
    Params base;
    const double pick = uniform(rng, 0.0, 1.0);
    if (pick < 0.58) {
      base = snapshotBest().params;
    } else if (pick < 0.92) {
      base = current.params;
    } else {
      base = randomSegmented(rng);
    }
    Params p = mutate(base, rng, durationSigma, angleSigma);
    Eval e = evaluateParams(p);
    evalCount.fetch_add(1, std::memory_order_relaxed);
    if (e.returned) {
      Candidate cand{p, e};
      const double temp = 0.10 * frac * frac;
      bool accept = betterEval(e, current.eval);
      if (!accept && temp > 1e-12) {
        const double delta = score(e) - score(current.eval);
        accept = uniform(rng, 0.0, 1.0) < std::exp(-delta / temp);
      }
      if (accept) current = cand;
      if (betterEval(e, local.eval)) local = cand;
      publishIfBetter(cand, workerId);
    }
    if ((i % 25000) == 0) {
      Candidate polished = coordinatePolish(local, 1, false);
      if (betterEval(polished.eval, local.eval)) local = polished;
      publishIfBetter(local, workerId);
      if (uniform(rng, 0.0, 1.0) < 0.35) current = snapshotBest();
    }
  }
  Candidate polished = coordinatePolish(local, 2, false);
  publishIfBetter(polished, workerId);
}

void writeJson(std::ostream& out, const Candidate& c) {
  const Params& p = c.params;
  const Eval& e = c.eval;
  out << std::setprecision(17);
  out << "{\n";
  out << "  \"format\": \"elytra-nonperiodic-return-v1\",\n";
  out << "  \"description\": \"From zero initial velocity, minimize maximum drop before reaching the target relative height.\",\n";
  out << "  \"tickRate\": 20,\n";
  out << "  \"angleUnit\": \"degrees\",\n";
  out << "  \"angleConvention\": \"positive angle means nose-up; negative angle means nose-down\",\n";
  out << "  \"minecraftPitchConvention\": \"minecraft_pitch_degrees = -angle_degrees\",\n";
  out << "  \"initialState\": {\"x\": 0, \"y\": 0, \"vx\": 0, \"vy\": 0},\n";
  out << "  \"target\": \"first tick with y >= targetRelativeY after launch\",\n";
  out << "  \"targetRelativeY\": " << targetY << ",\n";
  out << "  \"minimumInitialHeightBlocks\": " << e.drop << ",\n";
  out << "  \"returnTick\": " << e.returnTick << ",\n";
  out << "  \"returnTimeSeconds\": " << static_cast<double>(e.returnTick) / TICKS_PER_SECOND << ",\n";
  out << "  \"minTick\": " << e.minTick << ",\n";
  out << "  \"periodic\": false,\n";
  out << "  \"controlFormat\": \"segmented-bezier-v1-compatible\",\n";
  out << "  \"controlPointSpacing\": {\"type\": \"cosine_endpoint_dense\", \"formula\": \"x_i = 0.5 - 0.5 * cos(pi * i / (controlCount - 1))\"},\n";
  out << "  \"segments\": {\n";
  out << "    \"negativeConstantTicks\": " << p.n1 << ",\n";
  out << "    \"negativeTransitionLinearTicks\": " << p.n12 << ",\n";
  out << "    \"negativeBezierTicks\": " << p.n2 << ",\n";
  out << "    \"holdZeroAfterNegativeTicks\": " << p.n3 << ",\n";
  out << "    \"positiveRampLinearTicks\": " << p.n34 << ",\n";
  out << "    \"positiveHoldTicks\": " << p.nPosHold << ",\n";
  out << "    \"positiveBezierToZeroTicks\": " << p.n4 << ",\n";
  out << "    \"holdZeroEndTicks\": " << p.n5 << "\n";
  out << "  },\n";
  out << "  \"negativeConstantAngle\": " << p.negHoldAngle << ",\n";
  out << "  \"negativeBezierControls\": [";
  for (int i = 0; i < NEG_CONTROLS; ++i) {
    if (i) out << ", ";
    out << p.neg[i];
  }
  out << "],\n";
  out << "  \"positiveBezierControls\": [";
  for (int i = 0; i < POS_CONTROLS; ++i) {
    if (i) out << ", ";
    out << p.pos[i];
  }
  out << "],\n";
  out << "  \"returnState\": {\"x\": " << e.xAtReturn << ", \"y\": " << e.yAtReturn
      << ", \"vx\": " << e.vxAtReturn << ", \"vy\": " << e.vyAtReturn << "}\n";
  out << "}\n";
}

void writeOutputs(const std::filesystem::path& outDir, const Candidate& best) {
  std::filesystem::create_directories(outDir);
  {
    std::ofstream out(outDir / "best_strategy.json");
    writeJson(out, best);
  }

  const std::vector<double> angles = buildAngles(best.params);
  {
    std::ofstream out(outDir / "best_waveform.csv");
    out << std::setprecision(17);
    out << "tick,angle\n";
    for (size_t i = 0; i < angles.size(); ++i) {
      out << i << ',' << angles[i] << '\n';
      if (static_cast<int>(i + 1) >= best.eval.returnTick) break;
    }
  }
  {
    std::ofstream out(outDir / "best_trajectory.csv");
    out << std::setprecision(17);
    out << "tick,angle,x,y,vx,vy,min_y\n";
    State state;
    double minY = 0.0;
    for (size_t i = 0; i < angles.size(); ++i) {
      const TickParams tp = makeTickParams(angles[i]);
      tick(state, tp);
      minY = std::min(minY, state.y);
      out << (i + 1) << ',' << angles[i] << ',' << state.x << ',' << state.y << ','
          << state.vx << ',' << state.vy << ',' << minY << '\n';
      if (static_cast<int>(i + 1) >= best.eval.returnTick) break;
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 0; i < 65536; ++i) {
    sinTable[static_cast<size_t>(i)] = static_cast<float>(std::sin(static_cast<double>(i) * 2.0 * PI / 65536.0));
  }

  const std::filesystem::path outDir = argc > 1 ? argv[1] : "analysis/nonperiodic_return_from_rest_cpp";
  const long long iterationsPerThread = argc > 2 ? std::stoll(argv[2]) : 300000;
  int threads = argc > 3 ? std::stoi(argv[3]) : static_cast<int>(std::thread::hardware_concurrency());
  const uint64_t seed = argc > 4 ? static_cast<uint64_t>(std::stoull(argv[4])) : 20260704ULL;
  targetY = argc > 5 ? std::stod(argv[5]) : 0.0;
  threads = std::max(1, threads);

  Candidate best{Params{}, evaluateParams(Params{})};
  Candidate climb{maxClimbSeed(), evaluateParams(maxClimbSeed())};
  if (betterEval(climb.eval, best.eval)) best = climb;
  best = coordinatePolish(best, 2, true);
  globalBest = best;

  std::cout << "initial drop=" << std::setprecision(12) << globalBest.eval.drop
            << " return=" << globalBest.eval.returnTick
            << " minTick=" << globalBest.eval.minTick
            << " total=" << totalTicks(globalBest.params)
            << " targetY=" << targetY
            << " threads=" << threads
            << " iterationsPerThread=" << iterationsPerThread << "\n";

  std::vector<std::thread> workers;
  workers.reserve(static_cast<size_t>(threads));
  for (int i = 0; i < threads; ++i) {
    workers.emplace_back(workerMain, i, iterationsPerThread, seed);
  }
  for (std::thread& worker : workers) worker.join();

  Candidate finalBest = coordinatePolish(snapshotBest(), 3, true);
  {
    std::lock_guard<std::mutex> lock(globalMutex);
    if (betterEval(finalBest.eval, globalBest.eval)) globalBest = finalBest;
    finalBest = globalBest;
  }
  writeOutputs(outDir, finalBest);

  std::cout << "final drop=" << std::setprecision(15) << finalBest.eval.drop
            << " return=" << finalBest.eval.returnTick
            << " seconds=" << static_cast<double>(finalBest.eval.returnTick) / TICKS_PER_SECOND
            << " minTick=" << finalBest.eval.minTick
            << " targetY=" << targetY
            << " x=" << finalBest.eval.xAtReturn
            << " y=" << finalBest.eval.yAtReturn
            << " vx=" << finalBest.eval.vxAtReturn
            << " vy=" << finalBest.eval.vyAtReturn
            << " evals=" << evalCount.load() << "\n";
  std::cout << "wrote " << std::filesystem::absolute(outDir).string() << "\n";
  return 0;
}
