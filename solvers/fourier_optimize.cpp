#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

namespace {

constexpr int TICKS_PER_SECOND = 20;
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

constexpr double INIT_DOWN_ANGLE = -32.7280;
constexpr double INIT_UP_ANGLE = 34.5575;
constexpr double INIT_DUTY = 162.0 / 237.0;

const std::vector<double> ORDER5_SEED = {
    -13.115782000000067,
    23.773692929372302,
    -30.041692994152871,
    -14.547134603402395,
    -13.553563968133735,
    -2.1044373715155809,
    7.4088818591884538,
    9.0714284344731677,
    -2.8379961649766106,
    0.4341180409220704,
    -8.2469726637891334,
};
constexpr int ORDER5_SEED_PERIOD = 248;

std::array<float, 65536> sinTable;

float mthSin(double value) {
  const float v = static_cast<float>(static_cast<float>(value) * RAD_TO_INDEX);
  return sinTable[static_cast<int>(std::trunc(v)) & 65535];
}

float mthCos(double value) {
  const float v = static_cast<float>(static_cast<float>(value) * RAD_TO_INDEX + 16384.0f);
  return sinTable[static_cast<int>(std::trunc(v)) & 65535];
}

double clamp(double value, double lo, double hi) {
  return std::max(lo, std::min(hi, value));
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
  double dy = -std::numeric_limits<double>::infinity();
  double dx = 0.0;
  double climbRate = -std::numeric_limits<double>::infinity();
  double avgHorizontal = 0.0;
  double endVx = 0.0;
  double endVy = 0.0;
  double dvx = 0.0;
  double dvy = 0.0;
  int cyclesUsed = 0;
  bool converged = false;
};

struct Solution {
  int order = 0;
  int period = 0;
  std::vector<double> coeffs;
  Eval eval;
  double minRawAngle = 0.0;
  double maxRawAngle = 0.0;
  double minClampedAngle = 0.0;
  double maxClampedAngle = 0.0;
};

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

void tickWithAngle(State& state, double angle) {
  const TickParams p = makeTickParams(angle);
  tick(state, p);
}

State horizonState() {
  State state;
  const TickParams p = makeTickParams(0.0);
  for (int i = 0; i < 3000; ++i) tick(state, p);
  return state;
}

State horizon;

double rawAngleAt(const std::vector<double>& c, int order, int tick, int period) {
  const double phase = 2.0 * PI * static_cast<double>(tick) / static_cast<double>(period);
  double angle = c[0];
  for (int k = 1; k <= order; ++k) {
    angle += c[2 * k - 1] * std::cos(k * phase) + c[2 * k] * std::sin(k * phase);
  }
  return angle;
}

std::vector<TickParams> makePeriodParams(const std::vector<double>& coeffs, int order, int period) {
  std::vector<TickParams> params;
  params.reserve(period);
  for (int t = 0; t < period; ++t) {
    params.push_back(makeTickParams(rawAngleAt(coeffs, order, t, period)));
  }
  return params;
}

Eval evaluate(const std::vector<double>& coeffs, int order, int period, int maxCycles = 500) {
  const std::vector<TickParams> params = makePeriodParams(coeffs, order, period);
  State state{0.0, 0.0, horizon.vx, horizon.vy};
  Eval out;

  for (int cycle = 0; cycle < maxCycles; ++cycle) {
    const State before = state;
    for (const TickParams& p : params) tick(state, p);
    out.dx = state.x - before.x;
    out.dy = state.y - before.y;
    out.dvx = state.vx - before.vx;
    out.dvy = state.vy - before.vy;
    out.cyclesUsed = cycle + 1;
    out.converged = cycle > 8 && std::max(std::abs(out.dvx), std::abs(out.dvy)) < 1e-14;
    if (out.converged) break;
  }

  const double seconds = static_cast<double>(period) / TICKS_PER_SECOND;
  out.climbRate = out.dy / seconds;
  out.avgHorizontal = out.dx / seconds;
  out.endVx = state.vx;
  out.endVy = state.vy;
  out.objective = out.climbRate;

  if (!out.converged) {
    out.objective -= 1000.0 * (std::abs(out.dvx) + std::abs(out.dvy));
  }
  return out;
}

bool better(const Eval& a, const Eval& b) {
  if (a.objective != b.objective) return a.objective > b.objective;
  return a.avgHorizontal > b.avgHorizontal;
}

std::vector<double> stepInitialCoeffs(int order, int period) {
  std::vector<double> samples(period);
  const int downTicks = std::max(1, std::min(period - 1, static_cast<int>(std::round(period * INIT_DUTY))));
  for (int t = 0; t < period; ++t) {
    samples[t] = t < downTicks ? INIT_DOWN_ANGLE : INIT_UP_ANGLE;
  }

  std::vector<double> coeffs(2 * order + 1, 0.0);
  coeffs[0] = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(period);
  for (int k = 1; k <= order; ++k) {
    double cosSum = 0.0;
    double sinSum = 0.0;
    for (int t = 0; t < period; ++t) {
      const double phase = 2.0 * PI * static_cast<double>(k) * static_cast<double>(t) / static_cast<double>(period);
      cosSum += samples[t] * std::cos(phase);
      sinSum += samples[t] * std::sin(phase);
    }
    coeffs[2 * k - 1] = 2.0 * cosSum / static_cast<double>(period);
    coeffs[2 * k] = 2.0 * sinSum / static_cast<double>(period);
  }
  return coeffs;
}

std::vector<double> raiseOrderSeed(const std::vector<double>& seed, int order) {
  std::vector<double> coeffs(2 * order + 1, 0.0);
  for (size_t i = 0; i < std::min(seed.size(), coeffs.size()); ++i) coeffs[i] = seed[i];
  return coeffs;
}

void clampCoeffs(std::vector<double>& coeffs) {
  coeffs[0] = clamp(coeffs[0], -90.0, 90.0);
  for (size_t i = 1; i < coeffs.size(); ++i) coeffs[i] = clamp(coeffs[i], -180.0, 180.0);
}

Eval localOptimizeCoeffs(std::vector<double>& coeffs, int order, int period, bool polish) {
  clampCoeffs(coeffs);
  Eval best = evaluate(coeffs, order, period);
  const std::vector<double> steps = polish
      ? std::vector<double>{4.0, 2.0, 1.0, 0.5, 0.25, 0.1, 0.04, 0.015}
      : std::vector<double>{6.0, 3.0, 1.25, 0.5, 0.2};

  for (double step : steps) {
    bool improved = true;
    int passes = 0;
    while (improved && passes < 6) {
      improved = false;
      ++passes;
      for (size_t i = 0; i < coeffs.size(); ++i) {
        std::vector<double> bestLocalCoeffs = coeffs;
        Eval bestLocalEval = best;
        for (double sign : {-1.0, 1.0}) {
          std::vector<double> trial = coeffs;
          trial[i] += sign * step;
          clampCoeffs(trial);
          const Eval e = evaluate(trial, order, period);
          if (better(e, bestLocalEval)) {
            bestLocalEval = e;
            bestLocalCoeffs = trial;
          }
        }
        if (better(bestLocalEval, best)) {
          coeffs = bestLocalCoeffs;
          best = bestLocalEval;
          improved = true;
        }
      }
    }
  }
  return best;
}

Solution makeSolution(int order, int period, std::vector<double> coeffs, bool polish) {
  Eval e = localOptimizeCoeffs(coeffs, order, period, polish);
  Solution s;
  s.order = order;
  s.period = period;
  s.coeffs = coeffs;
  s.eval = e;

  s.minRawAngle = std::numeric_limits<double>::infinity();
  s.maxRawAngle = -std::numeric_limits<double>::infinity();
  s.minClampedAngle = std::numeric_limits<double>::infinity();
  s.maxClampedAngle = -std::numeric_limits<double>::infinity();
  for (int t = 0; t < period; ++t) {
    const double raw = rawAngleAt(coeffs, order, t, period);
    const double clipped = clamp(raw, -90.0, 90.0);
    s.minRawAngle = std::min(s.minRawAngle, raw);
    s.maxRawAngle = std::max(s.maxRawAngle, raw);
    s.minClampedAngle = std::min(s.minClampedAngle, clipped);
    s.maxClampedAngle = std::max(s.maxClampedAngle, clipped);
  }
  return s;
}

Solution optimizeOrderFromStep(int order) {
  bool hasBest = false;
  Solution best;

  for (int period = 120; period <= 360; period += 10) {
    Solution s = makeSolution(order, period, stepInitialCoeffs(order, period), false);
    if (!hasBest || better(s.eval, best.eval)) {
      best = s;
      hasBest = true;
    }
  }

  Solution fineBest = best;
  for (int period = std::max(60, best.period - 24); period <= best.period + 24; ++period) {
    Solution fromStep = makeSolution(order, period, stepInitialCoeffs(order, period), true);
    if (better(fromStep.eval, fineBest.eval)) fineBest = fromStep;

    Solution fromBest = makeSolution(order, period, best.coeffs, true);
    if (better(fromBest.eval, fineBest.eval)) fineBest = fromBest;
  }

  // One last polish pass at the winning period.
  return makeSolution(order, fineBest.period, fineBest.coeffs, true);
}

Solution optimizeRaisedOrder(int order, const std::vector<double>& seed, int seedPeriod) {
  bool hasBest = false;
  Solution best;

  for (int period = std::max(80, seedPeriod - 24); period <= seedPeriod + 32; period += 4) {
    Solution s = makeSolution(order, period, raiseOrderSeed(seed, order), true);
    if (!hasBest || better(s.eval, best.eval)) {
      best = s;
      hasBest = true;
    }
  }

  Solution fineBest = best;
  for (int period = std::max(60, best.period - 8); period <= best.period + 8; ++period) {
    Solution fromBest = makeSolution(order, period, best.coeffs, true);
    if (better(fromBest.eval, fineBest.eval)) fineBest = fromBest;
  }

  return makeSolution(order, fineBest.period, fineBest.coeffs, true);
}

void writeTrajectory(const Solution& s) {
  const std::string path = "analysis/fourier_order" + std::to_string(s.order) + "_trajectory.csv";
  std::ofstream out(path);
  out << std::setprecision(17);
  out << "tick,x,y,vx,vy,angle\n";

  const std::vector<TickParams> params = makePeriodParams(s.coeffs, s.order, s.period);
  State state{0.0, 0.0, horizon.vx, horizon.vy};
  for (int cycle = 0; cycle < 600; ++cycle) {
    const State before = state;
    for (const TickParams& p : params) tick(state, p);
    const double dvx = state.vx - before.vx;
    const double dvy = state.vy - before.vy;
    if (cycle > 8 && std::max(std::abs(dvx), std::abs(dvy)) < 1e-14) {
      state = before;
      break;
    }
  }

  const double x0 = state.x;
  const double y0 = state.y;
  out << 0 << ',' << 0.0 << ',' << 0.0 << ',' << state.vx << ',' << state.vy << ','
      << clamp(rawAngleAt(s.coeffs, s.order, 0, s.period), -90.0, 90.0) << '\n';
  for (int t = 0; t < s.period; ++t) {
    tick(state, params[t]);
    const int nextTick = t + 1;
    const double angle = clamp(rawAngleAt(s.coeffs, s.order, nextTick % s.period, s.period), -90.0, 90.0);
    out << nextTick << ',' << state.x - x0 << ',' << state.y - y0 << ',' << state.vx << ','
        << state.vy << ',' << angle << '\n';
  }
}

void writeWaveform(const Solution& s) {
  const std::string path = "analysis/fourier_order" + std::to_string(s.order) + "_waveform.csv";
  std::ofstream out(path);
  out << std::setprecision(17);
  out << "tick,rawAngle,clampedAngle\n";
  for (int t = 0; t < s.period; ++t) {
    const double raw = rawAngleAt(s.coeffs, s.order, t, s.period);
    out << t << ',' << raw << ',' << clamp(raw, -90.0, 90.0) << '\n';
  }
}

void printSolution(const Solution& s, bool comma) {
  std::cout << "  \"order" << s.order << "\": {\n";
  std::cout << "    \"periodTicks\": " << s.period << ",\n";
  std::cout << "    \"seconds\": " << static_cast<double>(s.period) / TICKS_PER_SECOND << ",\n";
  std::cout << "    \"dy\": " << s.eval.dy << ",\n";
  std::cout << "    \"climbRate\": " << s.eval.climbRate << ",\n";
  std::cout << "    \"dx\": " << s.eval.dx << ",\n";
  std::cout << "    \"avgHorizontal\": " << s.eval.avgHorizontal << ",\n";
  std::cout << "    \"endVx\": " << s.eval.endVx << ",\n";
  std::cout << "    \"endVy\": " << s.eval.endVy << ",\n";
  std::cout << "    \"cyclesUsed\": " << s.eval.cyclesUsed << ",\n";
  std::cout << "    \"converged\": " << (s.eval.converged ? "true" : "false") << ",\n";
  std::cout << "    \"minRawAngle\": " << s.minRawAngle << ",\n";
  std::cout << "    \"maxRawAngle\": " << s.maxRawAngle << ",\n";
  std::cout << "    \"minClampedAngle\": " << s.minClampedAngle << ",\n";
  std::cout << "    \"maxClampedAngle\": " << s.maxClampedAngle << ",\n";
  std::cout << "    \"coefficients\": [";
  for (size_t i = 0; i < s.coeffs.size(); ++i) {
    if (i) std::cout << ", ";
    std::cout << s.coeffs[i];
  }
  std::cout << "]\n";
  std::cout << "  }" << (comma ? "," : "") << "\n";
}

}  // namespace

int main() {
  for (int i = 0; i < 65536; ++i) {
    sinTable[i] = static_cast<float>(std::sin(static_cast<double>(i) * PI * 2.0 / 65536.0));
  }
  horizon = horizonState();

  const auto start = std::chrono::steady_clock::now();
  const Solution order3 = optimizeOrderFromStep(3);
  const Solution order5 = optimizeOrderFromStep(5);
  const Solution order7 = optimizeRaisedOrder(7, ORDER5_SEED, ORDER5_SEED_PERIOD);
  const Solution order9 = optimizeRaisedOrder(9, order7.coeffs, order7.period);
  const Solution order20 = optimizeRaisedOrder(20, order9.coeffs, order9.period);
  const auto end = std::chrono::steady_clock::now();

  writeWaveform(order3);
  writeWaveform(order5);
  writeWaveform(order7);
  writeWaveform(order9);
  writeWaveform(order20);
  writeTrajectory(order3);
  writeTrajectory(order5);
  writeTrajectory(order7);
  writeTrajectory(order9);
  writeTrajectory(order20);

  std::cout << std::setprecision(17);
  std::cout << "{\n";
  printSolution(order3, true);
  printSolution(order5, true);
  printSolution(order7, true);
  printSolution(order9, true);
  printSolution(order20, true);
  std::cout << "  \"elapsedSeconds\": " << std::chrono::duration<double>(end - start).count() << "\n";
  std::cout << "}\n";
  return 0;
}
