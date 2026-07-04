#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
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
constexpr double DROP_LAMBDA = 10.0;
constexpr int MIN_PERIOD = 180;
constexpr int MAX_PERIOD = 720;
constexpr float DEG_TO_RAD = static_cast<float>(PI / 180.0);
constexpr float RAD_TO_INDEX = 10430.378f;

std::array<float, 65536> sinTable;

enum class ObjectiveMode {
  HorizontalSpeed,
  ClimbRate,
};

ObjectiveMode objectiveMode = ObjectiveMode::HorizontalSpeed;
double dropC = 10.0;
double diffL1Lambda = 0.0;

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

double softplus(double x) {
  if (x > 40.0) return x;
  if (x < -40.0) return std::exp(x);
  return std::log1p(std::exp(x));
}

struct State {
  double x = 0.0;
  double y = 0.0;
  double vx = 0.0;
  double vy = -0.1;
};

struct TickParams {
  double pitchRad = 0.0;
  double sinPitch = 0.0;
  double horizontalLookLength = 0.0;
  double lift = 0.0;
  double lookSign = 1.0;
};

struct Eval {
  double objective = -std::numeric_limits<double>::infinity();
  double dropPenalty = 0.0;
  double l1Penalty = 0.0;
  double dy = -std::numeric_limits<double>::infinity();
  double dx = 0.0;
  double climbRate = -std::numeric_limits<double>::infinity();
  double avgHorizontal = 0.0;
  double meanAbsDelta = 0.0;
  double rmsDelta = 0.0;
  double maxAbsDelta = 0.0;
  double minAngle = 0.0;
  double maxAngle = 0.0;
  int period = 0;
  int cyclesUsed = 0;
  bool converged = false;
};

struct Params {
  int n1 = 10;        // negative constant hold
  int n12 = 5;        // linear transition into negative curve
  int n2 = 242;       // negative Bezier curve
  int n3 = 16;        // zero hold after negative curve
  int n34 = 5;        // linear ramp from zero to positive angle
  int nPosHold = 4;   // positive angle hold before positive curve
  int n4 = 78;        // positive Bezier curve back to zero
  int n5 = 9;         // final zero hold
  double negHoldAngle = -82.0;
  std::array<double, NEG_CONTROLS> neg = {-31.5, -33.4, -35.2, -37.4, -40.2, -43.0, -46.0, -49.7};
  std::array<double, POS_CONTROLS> pos = {60.8, 58.5, 50.0, 39.0, 28.0, 17.0, 7.0, 0.0};
};

struct Candidate {
  Params params;
  Eval eval;
};

State horizon;

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

State horizonState() {
  State state;
  const TickParams p = makeTickParams(0.0);
  for (int i = 0; i < 3000; ++i) tick(state, p);
  return state;
}

int periodOf(const Params& p) {
  return p.n1 + p.n12 + p.n2 + p.n3 + p.n34 + p.nPosHold + p.n4 + p.n5;
}

void enforce(Params& p) {
  p.n1 = static_cast<int>(std::llround(clamp(p.n1, 0, 120)));
  p.n12 = static_cast<int>(std::llround(clamp(p.n12, 0, 35)));
  p.n2 = static_cast<int>(std::llround(clamp(p.n2, 60, 460)));
  p.n3 = static_cast<int>(std::llround(clamp(p.n3, 0, 120)));
  p.n34 = static_cast<int>(std::llround(clamp(p.n34, 0, 35)));
  p.nPosHold = static_cast<int>(std::llround(clamp(p.nPosHold, 0, 80)));
  p.n4 = static_cast<int>(std::llround(clamp(p.n4, 40, 280)));
  p.n5 = static_cast<int>(std::llround(clamp(p.n5, 0, 120)));

  auto reduceOne = [&](int& value, int minValue) {
    if (value > minValue) {
      --value;
      return true;
    }
    return false;
  };
  auto increaseOne = [&](int& value, int maxValue) {
    if (value < maxValue) {
      ++value;
      return true;
    }
    return false;
  };
  while (periodOf(p) > MAX_PERIOD) {
    if (reduceOne(p.n2, 60) || reduceOne(p.n4, 40) || reduceOne(p.n3, 0) || reduceOne(p.n5, 0) ||
        reduceOne(p.n5, 0) || reduceOne(p.nPosHold, 0) || reduceOne(p.n12, 0) || reduceOne(p.n34, 0) ||
        reduceOne(p.n1, 0)) {
      continue;
    }
    break;
  }
  while (periodOf(p) < MIN_PERIOD) {
    if (increaseOne(p.n2, 460) || increaseOne(p.n4, 280) || increaseOne(p.n3, 120) ||
        increaseOne(p.n5, 120) || increaseOne(p.nPosHold, 80) || increaseOne(p.n12, 35) ||
        increaseOne(p.n34, 35) || increaseOne(p.n1, 120)) {
      continue;
    }
    break;
  }

  p.negHoldAngle = clamp(p.negHoldAngle, -89.5, -0.1);
  for (double& v : p.neg) v = clamp(v, -89.5, -0.1);
  for (int i = 0; i < POS_CONTROLS - 1; ++i) p.pos[i] = clamp(p.pos[i], 0.0, 89.5);
  p.pos[POS_CONTROLS - 1] = 0.0;
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
    for (size_t j = 0; j < level; ++j) {
      work[j] = work[j] * (1.0 - u) + work[j + 1] * u;
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
  static std::array<std::vector<double>, MAX_PERIOD + 1> cache;
  count = static_cast<int>(clamp(count, 1, MAX_PERIOD));
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
    for (int step = 0; step < 32; ++step) {
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
  angles.reserve(periodOf(p));
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
  out.period = static_cast<int>(angles.size());
  out.minAngle = *std::min_element(angles.begin(), angles.end());
  out.maxAngle = *std::max_element(angles.begin(), angles.end());

  double absDelta = 0.0;
  double delta2 = 0.0;
  for (int t = 0; t < out.period; ++t) {
    const double d = angles[(t + 1) % out.period] - angles[t];
    absDelta += std::abs(d);
    delta2 += d * d;
    out.maxAbsDelta = std::max(out.maxAbsDelta, std::abs(d));
  }
  out.meanAbsDelta = absDelta / static_cast<double>(out.period);
  out.rmsDelta = std::sqrt(delta2 / static_cast<double>(out.period));

  std::vector<TickParams> params;
  params.reserve(angles.size());
  for (double angle : angles) params.push_back(makeTickParams(angle));

  State state{0.0, 0.0, horizon.vx, horizon.vy};
  for (int cycle = 0; cycle < 280; ++cycle) {
    const State before = state;
    for (const TickParams& tp : params) tick(state, tp);
    out.dx = state.x - before.x;
    out.dy = state.y - before.y;
    const double dvx = state.vx - before.vx;
    const double dvy = state.vy - before.vy;
    out.cyclesUsed = cycle + 1;
    out.converged = cycle > 8 && std::max(std::abs(dvx), std::abs(dvy)) < 1e-14;
    if (out.converged) break;
  }

  const double seconds = static_cast<double>(out.period) / TICKS_PER_SECOND;
  out.climbRate = out.dy / seconds;
  out.avgHorizontal = out.dx / seconds;
  out.dropPenalty = DROP_LAMBDA * softplus(-out.dy * dropC);
  out.l1Penalty = diffL1Lambda * out.meanAbsDelta;
  if (objectiveMode == ObjectiveMode::HorizontalSpeed) {
    out.objective = out.avgHorizontal - out.dropPenalty - out.l1Penalty;
  } else {
    out.objective = out.climbRate - out.l1Penalty;
  }
  if (!out.converged) out.objective -= 1000.0;
  return out;
}

Eval evaluateParams(const Params& p) {
  return evaluateAngles(buildAngles(p));
}

bool betterEval(const Eval& a, const Eval& b) {
  if (a.objective != b.objective) return a.objective > b.objective;
  if (objectiveMode == ObjectiveMode::HorizontalSpeed && a.avgHorizontal != b.avgHorizontal) {
    return a.avgHorizontal > b.avgHorizontal;
  }
  if (objectiveMode == ObjectiveMode::ClimbRate && a.climbRate != b.climbRate) {
    return a.climbRate > b.climbRate;
  }
  return a.dy > b.dy;
}

bool betterCandidate(const Candidate& a, const Candidate& b) {
  return betterEval(a.eval, b.eval);
}

double uniform(std::mt19937_64& rng, double lo, double hi) {
  return std::uniform_real_distribution<double>(lo, hi)(rng);
}

int uniformInt(std::mt19937_64& rng, int lo, int hi) {
  return std::uniform_int_distribution<int>(lo, hi)(rng);
}

Params fromEndpoints(double ns, double ne, double ps, double curvePow) {
  Params p;
  p.negHoldAngle = std::max(-89.5, ns - 45.0);
  for (int i = 0; i < NEG_CONTROLS; ++i) {
    const double u = controlPosition<NEG_CONTROLS>(i);
    p.neg[i] = ns + (ne - ns) * std::pow(u, curvePow);
  }
  for (int i = 0; i < POS_CONTROLS; ++i) {
    const double u = controlPosition<POS_CONTROLS>(i);
    p.pos[i] = ps * std::pow(1.0 - u, curvePow);
  }
  p.pos[POS_CONTROLS - 1] = 0.0;
  enforce(p);
  return p;
}

Params seededParams(int index) {
  const int n1s[] = {0, 4, 8, 12, 22, 40};
  const int n12s[] = {0, 3, 5, 8, 13};
  const int n2s[] = {170, 205, 235, 245, 270, 320};
  const int n3s[] = {0, 8, 15, 24, 40};
  const int n34s[] = {0, 3, 5, 8, 13};
  const int nPosHolds[] = {0, 3, 6, 12, 24};
  const int n4s[] = {64, 78, 84, 98, 125, 170};
  const int n5s[] = {0, 4, 10, 20, 40};
  const double holdOffsets[] = {20.0, 35.0, 50.0, 65.0};
  const double negStarts[] = {-10.0, -18.0, -28.0, -34.0, -42.0};
  const double negEnds[] = {-38.0, -45.0, -52.0, -62.0};
  const double posStarts[] = {45.0, 58.0, 64.0, 72.0, 84.0};
  const double powers[] = {0.55, 0.8, 1.0, 1.25, 1.7};

  Params p = fromEndpoints(negStarts[(index / 5250) % 5], negEnds[(index / 26250) % 4],
                           posStarts[(index / 105000) % 5], powers[(index / 525000) % 5]);
  p.n1 = n1s[index % 6];
  p.n12 = n12s[(index / 6) % 5];
  p.n2 = n2s[(index / 30) % 6];
  p.n3 = n3s[(index / 180) % 5];
  p.n34 = n34s[(index / 900) % 5];
  p.nPosHold = nPosHolds[(index / 4500) % 5];
  p.n4 = n4s[(index / 22500) % 6];
  p.n5 = n5s[(index / 135000) % 5];
  p.negHoldAngle = p.neg[0] - holdOffsets[(index / 675000) % 4];
  enforce(p);
  return p;
}

Params randomParams(std::mt19937_64& rng) {
  Params p;
  p.n1 = uniformInt(rng, 0, 80);
  p.n12 = uniformInt(rng, 0, 18);
  p.n2 = uniformInt(rng, 100, 380);
  p.n3 = uniformInt(rng, 0, 85);
  p.n34 = uniformInt(rng, 0, 18);
  p.nPosHold = uniformInt(rng, 0, 45);
  p.n4 = uniformInt(rng, 45, 220);
  p.n5 = uniformInt(rng, 0, 80);

  const double negStart = uniform(rng, -55.0, -2.0);
  const double negEnd = uniform(rng, -82.0, std::min(-18.0, negStart - 2.0));
  p.negHoldAngle = uniform(rng, -89.5, negStart);
  for (int i = 0; i < NEG_CONTROLS; ++i) {
    const double u = controlPosition<NEG_CONTROLS>(i);
    const double shaped = std::pow(u, uniform(rng, 0.55, 1.8));
    p.neg[i] = negStart + (negEnd - negStart) * shaped + uniform(rng, -5.0, 5.0);
  }
  const double posStart = uniform(rng, 20.0, 89.0);
  for (int i = 0; i < POS_CONTROLS; ++i) {
    const double u = controlPosition<POS_CONTROLS>(i);
    p.pos[i] = posStart * std::pow(1.0 - u, uniform(rng, 0.55, 1.8)) + uniform(rng, -4.0, 4.0);
  }
  p.pos[POS_CONTROLS - 1] = 0.0;
  enforce(p);
  return p;
}

Params mutate(Params p, std::mt19937_64& rng, double scale) {
  const double durSigma = 1.0 + 62.0 * scale;
  const double angleSigma = 0.15 + 24.0 * scale;
  auto maybeDuration = [&](int& v) {
    if (uniform(rng, 0.0, 1.0) < 0.78) {
      v += static_cast<int>(std::llround(std::normal_distribution<double>(0.0, durSigma)(rng)));
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
  if (uniform(rng, 0.0, 1.0) < 0.85) p.negHoldAngle += std::normal_distribution<double>(0.0, angleSigma)(rng);
  if (uniform(rng, 0.0, 1.0) < 0.35) {
    const double shift = std::normal_distribution<double>(0.0, angleSigma)(rng);
    for (double& v : p.neg) v += shift;
  }
  if (uniform(rng, 0.0, 1.0) < 0.35) {
    const double shift = std::normal_distribution<double>(0.0, angleSigma)(rng);
    for (int i = 0; i < POS_CONTROLS - 1; ++i) p.pos[i] += shift;
  }
  for (double& v : p.neg) {
    if (uniform(rng, 0.0, 1.0) < 0.82) v += std::normal_distribution<double>(0.0, angleSigma)(rng);
  }
  for (int i = 0; i < POS_CONTROLS - 1; ++i) {
    if (uniform(rng, 0.0, 1.0) < 0.82) p.pos[i] += std::normal_distribution<double>(0.0, angleSigma)(rng);
  }
  enforce(p);
  return p;
}

void tryUpdate(Candidate& best, const Params& trialParams) {
  Candidate trial{trialParams, evaluateParams(trialParams)};
  if (betterCandidate(trial, best)) best = std::move(trial);
}

Candidate coordinatePolish(Candidate seed) {
  Candidate best = seed;
  const std::vector<int> durationSteps = {30, 18, 10, 5, 2, 1};
  const std::vector<double> angleSteps = {10.0, 5.0, 2.0, 0.8, 0.3, 0.1};
  for (size_t level = 0; level < durationSteps.size(); ++level) {
    bool improved = true;
    int passes = 0;
    while (improved && passes < 5) {
      improved = false;
      ++passes;
      const Eval before = best.eval;
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
      improved = betterEval(best.eval, before);
    }
  }
  return best;
}

Candidate anneal(Candidate start, std::mt19937_64& rng, int iterations) {
  Candidate current = start;
  Candidate best = start;
  for (int i = 0; i < iterations; ++i) {
    const double t = 1.0 - static_cast<double>(i) / std::max(1, iterations - 1);
    const double scale = 0.01 + 0.95 * t * t;
    Candidate trial{mutate(current.params, rng, scale), {}};
    trial.eval = evaluateParams(trial.params);
    const double temperature = (objectiveMode == ObjectiveMode::HorizontalSpeed ? 0.28 : 0.015) * t + 0.0008;
    const double delta = trial.eval.objective - current.eval.objective;
    if (delta > 0.0 || std::exp(delta / temperature) > uniform(rng, 0.0, 1.0)) current = trial;
    if (betterCandidate(trial, best)) best = trial;
    if ((i + 1) % 500 == 0) current = best;
  }
  return coordinatePolish(best);
}

std::string modeName() {
  return objectiveMode == ObjectiveMode::HorizontalSpeed ? "horizontal_speed" : "climb_rate";
}

void writeWaveform(const std::string& path, const std::vector<double>& angles, const Params& p) {
  std::ofstream out(path);
  out << std::setprecision(17);
  out << "tick,angle,segment\n";
  int t = 0;
  auto write = [&](int count, const std::string& name) {
    for (int i = 0; i < count; ++i, ++t) out << t << ',' << angles[t] << ',' << name << '\n';
  };
  write(p.n1, "negative_constant");
  write(p.n12, "negative_transition_linear");
  write(p.n2, "negative_bezier");
  write(p.n3, "hold_0_after_negative");
  write(p.n34, "positive_ramp_linear");
  write(p.nPosHold, "positive_hold");
  write(p.n4, "positive_bezier_to_0");
  write(p.n5, "hold_0_end");
}

void writeSimpleWaveform(const std::string& path, const std::vector<double>& angles) {
  std::ofstream out(path);
  out << std::setprecision(17);
  out << "tick,angle\n";
  for (size_t i = 0; i < angles.size(); ++i) out << i << ',' << angles[i] << '\n';
}

void writeTrajectory(const std::string& path, const std::vector<double>& angles) {
  std::vector<TickParams> params;
  params.reserve(angles.size());
  for (double angle : angles) params.push_back(makeTickParams(angle));

  State state{0.0, 0.0, horizon.vx, horizon.vy};
  for (int cycle = 0; cycle < 280; ++cycle) {
    const State before = state;
    for (const TickParams& tp : params) tick(state, tp);
    const double dvx = state.vx - before.vx;
    const double dvy = state.vy - before.vy;
    if (cycle > 8 && std::max(std::abs(dvx), std::abs(dvy)) < 1e-14) break;
  }

  const double x0 = state.x;
  const double y0 = state.y;
  std::ofstream out(path);
  out << std::setprecision(17);
  out << "tick,x,y,vx,vy,angle\n";
  out << 0 << ',' << 0.0 << ',' << 0.0 << ',' << state.vx << ',' << state.vy << ',' << angles[0] << '\n';
  for (size_t t = 0; t < angles.size(); ++t) {
    tick(state, params[t]);
    out << t + 1 << ',' << state.x - x0 << ',' << state.y - y0 << ',' << state.vx << ',' << state.vy << ','
        << angles[(t + 1) % angles.size()] << '\n';
  }
}

void writeParams(const std::string& path, const Candidate& c) {
  const Params& p = c.params;
  const Eval& e = c.eval;
  std::ofstream out(path);
  out << std::setprecision(17);
  out << "field,value\n";
  out << "objectiveMode," << modeName() << '\n';
  out << "objective," << e.objective << '\n';
  out << "dropC," << dropC << '\n';
  out << "diffL1Lambda," << diffL1Lambda << '\n';
  out << "period," << e.period << '\n';
  out << "n1_negative_constant," << p.n1 << '\n';
  out << "n12_negative_transition_linear," << p.n12 << '\n';
  out << "n2_negative_bezier," << p.n2 << '\n';
  out << "n3_hold_zero_after_negative," << p.n3 << '\n';
  out << "n34_positive_ramp_linear," << p.n34 << '\n';
  out << "nPosHold_positive_hold," << p.nPosHold << '\n';
  out << "n4_positive_bezier_to_zero," << p.n4 << '\n';
  out << "n5_hold_zero_end," << p.n5 << '\n';
  out << "negative_constant_angle," << p.negHoldAngle << '\n';
  for (int i = 0; i < NEG_CONTROLS; ++i) out << "neg_control_" << i << ',' << p.neg[i] << '\n';
  for (int i = 0; i < POS_CONTROLS; ++i) out << "pos_control_" << i << ',' << p.pos[i] << '\n';
  out << "avgHorizontal," << e.avgHorizontal << '\n';
  out << "dy," << e.dy << '\n';
  out << "dx," << e.dx << '\n';
  out << "climbRate," << e.climbRate << '\n';
  out << "dropPenalty," << e.dropPenalty << '\n';
  out << "l1Penalty," << e.l1Penalty << '\n';
  out << "meanAbsDelta," << e.meanAbsDelta << '\n';
  out << "rmsDelta," << e.rmsDelta << '\n';
  out << "maxAbsDelta," << e.maxAbsDelta << '\n';
  out << "minAngle," << e.minAngle << '\n';
  out << "maxAngle," << e.maxAngle << '\n';
  out << "cyclesUsed," << e.cyclesUsed << '\n';
  out << "converged," << (e.converged ? 1 : 0) << '\n';
}

void printCandidate(const Candidate& c, const std::string& prefix) {
  const Params& p = c.params;
  const Eval& e = c.eval;
  std::cout << std::setprecision(12) << prefix << " obj=" << e.objective << " avgH=" << e.avgHorizontal
            << " climb=" << e.climbRate << " dy=" << e.dy << " period=" << e.period << " n=(" << p.n1 << ','
            << p.n12 << ',' << p.n2 << ',' << p.n3 << ',' << p.n34 << ',' << p.nPosHold << ',' << p.n4 << ','
            << p.n5 << ") meanDelta=" << e.meanAbsDelta
            << " maxDelta=" << e.maxAbsDelta << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  const std::string outDir = argc >= 2 ? argv[1] : "analysis/segmented_bezier";
  const std::string modeArg = argc >= 3 ? argv[2] : "speed";
  objectiveMode = modeArg == "climb" ? ObjectiveMode::ClimbRate : ObjectiveMode::HorizontalSpeed;
  dropC = argc >= 4 ? std::stod(argv[3]) : 10.0;
  diffL1Lambda = argc >= 5 ? std::stod(argv[4]) : 0.0;
  const int randomStarts = argc >= 6 ? std::stoi(argv[5]) : 60000;
  const int topCount = argc >= 7 ? std::stoi(argv[6]) : 180;
  const int annealIters = argc >= 8 ? std::stoi(argv[7]) : 3800;
  const uint64_t rngSeed = argc >= 9 ? std::stoull(argv[8], nullptr, 0) : 0x5A9E1EDULL;

  for (int i = 0; i < 65536; ++i) {
    sinTable[i] = static_cast<float>(std::sin(static_cast<double>(i) * PI * 2.0 / 65536.0));
  }
  horizon = horizonState();
  std::filesystem::create_directories(outDir);
  const auto started = std::chrono::steady_clock::now();

  std::mt19937_64 rng(rngSeed);
  std::vector<Candidate> pool;
  pool.reserve(static_cast<size_t>(randomStarts) + 6000);
  for (int i = 0; i < 6000; ++i) {
    Params p = seededParams(i);
    pool.push_back({p, evaluateParams(p)});
  }
  for (int i = 0; i < randomStarts; ++i) {
    Params p = randomParams(rng);
    pool.push_back({p, evaluateParams(p)});
  }
  std::sort(pool.begin(), pool.end(), betterCandidate);
  if (static_cast<int>(pool.size()) > topCount) pool.resize(topCount);
  printCandidate(pool.front(), "initial_best");

  Candidate best = pool.front();
  for (int i = 0; i < static_cast<int>(pool.size()); ++i) {
    Candidate improved = anneal(pool[i], rng, annealIters);
    if (betterCandidate(improved, best)) {
      best = improved;
      printCandidate(best, "best");
    }
  }
  best = coordinatePolish(best);
  printCandidate(best, "final");

  const std::vector<double> angles = buildAngles(best.params);
  writeWaveform(outDir + "/best_waveform.csv", angles, best.params);
  writeSimpleWaveform(outDir + "/best_waveform_simple.csv", angles);
  writeTrajectory(outDir + "/best_trajectory.csv", angles);
  writeParams(outDir + "/best_params.csv", best);

  const auto ended = std::chrono::steady_clock::now();
  std::cout << std::setprecision(17);
  std::cout << "{\n";
  std::cout << "  \"objectiveMode\": \"" << modeName() << "\",\n";
  std::cout << "  \"dropC\": " << dropC << ",\n";
  std::cout << "  \"diffL1Lambda\": " << diffL1Lambda << ",\n";
  std::cout << "  \"rngSeed\": " << rngSeed << ",\n";
  std::cout << "  \"period\": " << best.eval.period << ",\n";
  std::cout << "  \"durations\": [" << best.params.n1 << ", " << best.params.n12 << ", " << best.params.n2
            << ", " << best.params.n3 << ", " << best.params.n34 << ", " << best.params.nPosHold << ", "
            << best.params.n4 << ", " << best.params.n5 << "],\n";
  std::cout << "  \"negativeConstantAngle\": " << best.params.negHoldAngle << ",\n";
  std::cout << "  \"objective\": " << best.eval.objective << ",\n";
  std::cout << "  \"avgHorizontal\": " << best.eval.avgHorizontal << ",\n";
  std::cout << "  \"climbRate\": " << best.eval.climbRate << ",\n";
  std::cout << "  \"dy\": " << best.eval.dy << ",\n";
  std::cout << "  \"dx\": " << best.eval.dx << ",\n";
  std::cout << "  \"dropPenalty\": " << best.eval.dropPenalty << ",\n";
  std::cout << "  \"l1Penalty\": " << best.eval.l1Penalty << ",\n";
  std::cout << "  \"meanAbsDelta\": " << best.eval.meanAbsDelta << ",\n";
  std::cout << "  \"maxAbsDelta\": " << best.eval.maxAbsDelta << ",\n";
  std::cout << "  \"elapsedSeconds\": " << std::chrono::duration<double>(ended - started).count() << "\n";
  std::cout << "}\n";
  return 0;
}
