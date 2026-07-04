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
constexpr int FOURIER9_PERIOD = 249;

const std::vector<double> FOURIER9 = {
    -12.865782000000067,
    25.398692929372302,
    -29.901692994152871,
    -13.172134603402393,
    -13.748563968133736,
    -0.60443737151558086,
    8.1038818591884549,
    10.60642843447317,
    -2.3879961649766104,
    1.2341180409220704,
    -6.3969726637891338,
    -1.46,
    1.115,
    2.605,
    4.4499999999999993,
    4.0,
    0.89000000000000001,
    -3.7400000000000002,
    -2.04,
};

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
  int controls = 0;
  int period = 0;
  std::vector<double> cps;
  Eval eval;
  double minAngle = 0.0;
  double maxAngle = 0.0;
  double rmsAccel = 0.0;
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

State horizonState() {
  State state;
  const TickParams p = makeTickParams(0.0);
  for (int i = 0; i < 3000; ++i) tick(state, p);
  return state;
}

State horizon;

double fourier9Angle(double tick, int period) {
  const double phase = 2.0 * PI * tick / static_cast<double>(period);
  double angle = FOURIER9[0];
  for (int k = 1; k <= 9; ++k) {
    angle += FOURIER9[2 * k - 1] * std::cos(k * phase) + FOURIER9[2 * k] * std::sin(k * phase);
  }
  return angle;
}

double bsplineAngle(const std::vector<double>& cps, double u) {
  const int n = static_cast<int>(cps.size());
  const double wrapped = u - std::floor(u);
  const double x = wrapped * n;
  const int i = static_cast<int>(std::floor(x));
  const double t = x - i;
  const double t2 = t * t;
  const double t3 = t2 * t;
  const double b0 = (1.0 - 3.0 * t + 3.0 * t2 - t3) / 6.0;
  const double b1 = (4.0 - 6.0 * t2 + 3.0 * t3) / 6.0;
  const double b2 = (1.0 + 3.0 * t + 3.0 * t2 - 3.0 * t3) / 6.0;
  const double b3 = t3 / 6.0;
  const auto at = [&](int idx) -> double { return cps[(idx % n + n) % n]; };
  return b0 * at(i - 1) + b1 * at(i) + b2 * at(i + 1) + b3 * at(i + 2);
}

std::vector<double> makeAngles(const std::vector<double>& cps, int period) {
  std::vector<double> angles;
  angles.reserve(period);
  for (int t = 0; t < period; ++t) {
    angles.push_back(clamp(bsplineAngle(cps, static_cast<double>(t) / period), -90.0, 90.0));
  }
  return angles;
}

Eval evaluate(const std::vector<double>& cps, int period, double smoothLambda = 0.0) {
  const std::vector<double> angles = makeAngles(cps, period);
  std::vector<TickParams> params;
  params.reserve(period);
  for (double angle : angles) params.push_back(makeTickParams(angle));

  State state{0.0, 0.0, horizon.vx, horizon.vy};
  Eval out;
  for (int cycle = 0; cycle < 600; ++cycle) {
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

  double accelPenalty = 0.0;
  for (int t = 0; t < period; ++t) {
    const double a = angles[(t + 1) % period] - 2.0 * angles[t] + angles[(t - 1 + period) % period];
    accelPenalty += a * a;
  }
  accelPenalty /= period;
  out.objective = out.climbRate - smoothLambda * accelPenalty;
  if (!out.converged) out.objective -= 1000.0 * (std::abs(out.dvx) + std::abs(out.dvy));
  return out;
}

bool better(const Eval& a, const Eval& b) {
  if (a.objective != b.objective) return a.objective > b.objective;
  return a.avgHorizontal > b.avgHorizontal;
}

std::vector<double> initialControls(int controls, int period) {
  std::vector<double> cps(controls);
  for (int i = 0; i < controls; ++i) {
    const double tick = static_cast<double>(i) / controls * period;
    cps[i] = clamp(fourier9Angle(tick, FOURIER9_PERIOD), -80.0, 80.0);
  }
  return cps;
}

void clampControls(std::vector<double>& cps) {
  for (double& cp : cps) cp = clamp(cp, -85.0, 85.0);
}

Eval localOptimize(std::vector<double>& cps, int period, double smoothLambda, bool polish) {
  clampControls(cps);
  Eval best = evaluate(cps, period, smoothLambda);
  const std::vector<double> steps = polish
      ? std::vector<double>{8.0, 4.0, 2.0, 1.0, 0.4, 0.16, 0.06}
      : std::vector<double>{10.0, 5.0, 2.0, 0.8};

  for (double step : steps) {
    bool improved = true;
    int passes = 0;
    while (improved && passes < 8) {
      improved = false;
      ++passes;
      for (size_t i = 0; i < cps.size(); ++i) {
        Eval localBest = best;
        std::vector<double> localCps = cps;
        for (double sign : {-1.0, 1.0}) {
          std::vector<double> trial = cps;
          trial[i] += sign * step;
          clampControls(trial);
          const Eval e = evaluate(trial, period, smoothLambda);
          if (better(e, localBest)) {
            localBest = e;
            localCps = trial;
          }
        }
        if (better(localBest, best)) {
          cps = localCps;
          best = localBest;
          improved = true;
        }
      }
    }
  }
  return best;
}

Solution makeSolution(int controls, int period, std::vector<double> cps, double smoothLambda, bool polish) {
  Eval e = localOptimize(cps, period, smoothLambda, polish);
  Solution s;
  s.controls = controls;
  s.period = period;
  s.cps = cps;
  s.eval = e;

  const std::vector<double> angles = makeAngles(cps, period);
  s.minAngle = *std::min_element(angles.begin(), angles.end());
  s.maxAngle = *std::max_element(angles.begin(), angles.end());
  double accel = 0.0;
  for (int t = 0; t < period; ++t) {
    const double a = angles[(t + 1) % period] - 2.0 * angles[t] + angles[(t - 1 + period) % period];
    accel += a * a;
  }
  s.rmsAccel = std::sqrt(accel / period);
  return s;
}

Solution optimizeControls(int controls, double smoothLambda) {
  bool hasBest = false;
  Solution best;
  for (int period = 220; period <= 280; period += 4) {
    Solution s = makeSolution(controls, period, initialControls(controls, period), smoothLambda, false);
    if (!hasBest || better(s.eval, best.eval)) {
      best = s;
      hasBest = true;
    }
  }

  Solution fine = best;
  for (int period = std::max(120, best.period - 8); period <= best.period + 8; ++period) {
    Solution s = makeSolution(controls, period, best.cps, smoothLambda, true);
    if (better(s.eval, fine.eval)) fine = s;
  }
  return makeSolution(controls, fine.period, fine.cps, smoothLambda, true);
}

void writeWaveform(const Solution& s) {
  const std::string path = "analysis/bspline_" + std::to_string(s.controls) + "_waveform.csv";
  std::ofstream out(path);
  out << std::setprecision(17);
  out << "tick,angle\n";
  for (int t = 0; t < s.period; ++t) {
    out << t << ',' << makeAngles(s.cps, s.period)[t] << '\n';
  }
}

void writeTrajectory(const Solution& s) {
  const std::string path = "analysis/bspline_" + std::to_string(s.controls) + "_trajectory.csv";
  std::ofstream out(path);
  out << std::setprecision(17);
  out << "tick,x,y,vx,vy,angle\n";

  const std::vector<double> angles = makeAngles(s.cps, s.period);
  std::vector<TickParams> params;
  for (double angle : angles) params.push_back(makeTickParams(angle));

  State state{0.0, 0.0, horizon.vx, horizon.vy};
  for (int cycle = 0; cycle < 700; ++cycle) {
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
  out << 0 << ',' << 0.0 << ',' << 0.0 << ',' << state.vx << ',' << state.vy << ',' << angles[0] << '\n';
  for (int t = 0; t < s.period; ++t) {
    tick(state, params[t]);
    out << t + 1 << ',' << state.x - x0 << ',' << state.y - y0 << ',' << state.vx << ',' << state.vy
        << ',' << angles[(t + 1) % s.period] << '\n';
  }
}

void printSolution(const Solution& s, bool comma) {
  std::cout << "  \"bspline" << s.controls << "\": {\n";
  std::cout << "    \"controls\": " << s.controls << ",\n";
  std::cout << "    \"periodTicks\": " << s.period << ",\n";
  std::cout << "    \"seconds\": " << static_cast<double>(s.period) / TICKS_PER_SECOND << ",\n";
  std::cout << "    \"dy\": " << s.eval.dy << ",\n";
  std::cout << "    \"climbRate\": " << s.eval.climbRate << ",\n";
  std::cout << "    \"dx\": " << s.eval.dx << ",\n";
  std::cout << "    \"avgHorizontal\": " << s.eval.avgHorizontal << ",\n";
  std::cout << "    \"cyclesUsed\": " << s.eval.cyclesUsed << ",\n";
  std::cout << "    \"converged\": " << (s.eval.converged ? "true" : "false") << ",\n";
  std::cout << "    \"minAngle\": " << s.minAngle << ",\n";
  std::cout << "    \"maxAngle\": " << s.maxAngle << ",\n";
  std::cout << "    \"rmsAngleAccel\": " << s.rmsAccel << ",\n";
  std::cout << "    \"controlPoints\": [";
  for (size_t i = 0; i < s.cps.size(); ++i) {
    if (i) std::cout << ", ";
    std::cout << s.cps[i];
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
  const double smoothLambda = 0.0;
  const Solution s8 = optimizeControls(8, smoothLambda);
  const Solution s12 = optimizeControls(12, smoothLambda);
  const Solution s16 = optimizeControls(16, smoothLambda);
  const auto end = std::chrono::steady_clock::now();

  writeWaveform(s8);
  writeWaveform(s12);
  writeWaveform(s16);
  writeTrajectory(s8);
  writeTrajectory(s12);
  writeTrajectory(s16);

  std::cout << std::setprecision(17);
  std::cout << "{\n";
  printSolution(s8, true);
  printSolution(s12, true);
  printSolution(s16, true);
  std::cout << "  \"elapsedSeconds\": " << std::chrono::duration<double>(end - start).count() << "\n";
  std::cout << "}\n";
  return 0;
}
