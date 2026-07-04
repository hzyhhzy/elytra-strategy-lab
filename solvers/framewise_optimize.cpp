#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
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
  double rmsDelta = 0.0;
  double maxAbsDelta = 0.0;
  double minAngle = 0.0;
  double maxAngle = 0.0;
  int lowBoundTicks = 0;
  int highBoundTicks = 0;
  int cyclesUsed = 0;
  bool converged = false;
};

struct Solution {
  std::string name;
  int period = 0;
  std::vector<double> angles;
  Eval eval;
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

std::vector<double> loadAngles(const std::string& path) {
  std::ifstream in(path);
  std::vector<double> angles;
  std::string line;
  std::getline(in, line);
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::stringstream ss(line);
    std::string cell;
    std::vector<std::string> cells;
    while (std::getline(ss, cell, ',')) cells.push_back(cell);
    if (cells.size() >= 3) {
      angles.push_back(std::stod(cells[2]));
    } else if (cells.size() >= 2) {
      angles.push_back(std::stod(cells[1]));
    }
  }
  return angles;
}

void clampAngles(std::vector<double>& angles) {
  for (double& angle : angles) angle = clamp(angle, -90.0, 90.0);
}

Eval evaluate(const std::vector<double>& inputAngles) {
  std::vector<double> angles = inputAngles;
  clampAngles(angles);
  const int period = static_cast<int>(angles.size());

  Eval out;
  out.minAngle = *std::min_element(angles.begin(), angles.end());
  out.maxAngle = *std::max_element(angles.begin(), angles.end());
  for (double angle : angles) {
    if (angle <= -89.999) ++out.lowBoundTicks;
    if (angle >= 89.999) ++out.highBoundTicks;
  }
  double delta2 = 0.0;
  for (int t = 0; t < period; ++t) {
    const double d = angles[(t + 1) % period] - angles[t];
    delta2 += d * d;
    out.maxAbsDelta = std::max(out.maxAbsDelta, std::abs(d));
  }
  out.rmsDelta = std::sqrt(delta2 / static_cast<double>(period));

  std::vector<TickParams> params;
  params.reserve(period);
  for (double angle : angles) params.push_back(makeTickParams(angle));

  State state{0.0, 0.0, horizon.vx, horizon.vy};
  for (int cycle = 0; cycle < 700; ++cycle) {
    const State before = state;
    for (const TickParams& p : params) tick(state, p);
    out.dx = state.x - before.x;
    out.dy = state.y - before.y;
    const double dvx = state.vx - before.vx;
    const double dvy = state.vy - before.vy;
    out.cyclesUsed = cycle + 1;
    out.converged = cycle > 8 && std::max(std::abs(dvx), std::abs(dvy)) < 1e-14;
    if (out.converged) break;
  }
  const double seconds = static_cast<double>(period) / TICKS_PER_SECOND;
  out.climbRate = out.dy / seconds;
  out.avgHorizontal = out.dx / seconds;
  out.objective = out.climbRate;
  if (!out.converged) out.objective -= 1000.0;
  return out;
}

bool better(const Eval& a, const Eval& b) {
  if (a.objective != b.objective) return a.objective > b.objective;
  return a.avgHorizontal > b.avgHorizontal;
}

Eval stepOptimize(std::vector<double>& angles) {
  clampAngles(angles);
  Eval best = evaluate(angles);
  const std::vector<double> steps = {10.0, 5.0, 2.0, 1.0, 0.45, 0.18, 0.07, 0.025};

  for (double step : steps) {
    bool improved = true;
    int passes = 0;
    while (improved && passes < 8) {
      improved = false;
      ++passes;
      for (size_t i = 0; i < angles.size(); ++i) {
        Eval localBest = best;
        double localAngle = angles[i];
        for (double sign : {-1.0, 1.0}) {
          std::vector<double> trial = angles;
          trial[i] = clamp(trial[i] + sign * step, -90.0, 90.0);
          if (trial[i] == angles[i]) continue;
          const Eval e = evaluate(trial);
          if (better(e, localBest)) {
            localBest = e;
            localAngle = trial[i];
          }
        }
        if (better(localBest, best)) {
          angles[i] = localAngle;
          best = localBest;
          improved = true;
        }
      }
    }
  }
  return best;
}

Eval lineSearchOptimize(std::vector<double>& angles) {
  clampAngles(angles);
  Eval best = evaluate(angles);
  const std::vector<std::pair<double, double>> levels = {
      {15.0, 180.0}, {5.0, 30.0}, {1.0, 7.0}, {0.25, 1.5}, {0.07, 0.35}, {0.02, 0.10},
  };

  for (const auto& [step, radius] : levels) {
    bool improved = true;
    int passes = 0;
    while (improved && passes < 5) {
      improved = false;
      ++passes;
      for (size_t i = 0; i < angles.size(); ++i) {
        Eval localBest = best;
        double localAngle = angles[i];

        const double lo = radius >= 180.0 ? -90.0 : clamp(angles[i] - radius, -90.0, 90.0);
        const double hi = radius >= 180.0 ? 90.0 : clamp(angles[i] + radius, -90.0, 90.0);
        for (double candidate = lo; candidate <= hi + step * 0.25; candidate += step) {
          std::vector<double> trial = angles;
          trial[i] = clamp(candidate, -90.0, 90.0);
          if (trial[i] == angles[i]) continue;
          const Eval e = evaluate(trial);
          if (better(e, localBest)) {
            localBest = e;
            localAngle = trial[i];
          }
        }
        if (better(localBest, best)) {
          angles[i] = localAngle;
          best = localBest;
          improved = true;
        }
      }
    }
  }
  return best;
}

State steadyStateBeforeCycle(const std::vector<double>& angles) {
  std::vector<TickParams> params;
  params.reserve(angles.size());
  for (double angle : angles) params.push_back(makeTickParams(angle));

  State state{0.0, 0.0, horizon.vx, horizon.vy};
  for (int cycle = 0; cycle < 700; ++cycle) {
    const State before = state;
    for (const TickParams& p : params) tick(state, p);
    const double dvx = state.vx - before.vx;
    const double dvy = state.vy - before.vy;
    if (cycle > 8 && std::max(std::abs(dvx), std::abs(dvy)) < 1e-14) {
      return before;
    }
  }
  return state;
}

void writeWaveform(const Solution& s) {
  const std::string path = "analysis/framewise_" + s.name + "_waveform.csv";
  std::ofstream out(path);
  out << std::setprecision(17);
  out << "tick,angle\n";
  for (int t = 0; t < s.period; ++t) out << t << ',' << s.angles[t] << '\n';
}

void writeTrajectory(const Solution& s) {
  const std::string path = "analysis/framewise_" + s.name + "_trajectory.csv";
  std::ofstream out(path);
  out << std::setprecision(17);
  out << "tick,x,y,vx,vy,angle\n";

  std::vector<TickParams> params;
  params.reserve(s.period);
  for (double angle : s.angles) params.push_back(makeTickParams(angle));
  State state = steadyStateBeforeCycle(s.angles);
  const double x0 = state.x;
  const double y0 = state.y;
  out << 0 << ',' << 0.0 << ',' << 0.0 << ',' << state.vx << ',' << state.vy << ',' << s.angles[0] << '\n';
  for (int t = 0; t < s.period; ++t) {
    tick(state, params[t]);
    out << t + 1 << ',' << state.x - x0 << ',' << state.y - y0 << ',' << state.vx << ',' << state.vy << ','
        << s.angles[(t + 1) % s.period] << '\n';
  }
}

void writeSummary(const std::vector<Solution>& sols) {
  std::ofstream out("analysis/framewise_summary.csv");
  out << std::setprecision(17);
  out << "name,period,dy,climbRate,dx,avgHorizontal,rmsDelta,maxAbsDelta,minAngle,maxAngle,lowBoundTicks,highBoundTicks,cyclesUsed,converged\n";
  for (const Solution& s : sols) {
    out << s.name << ',' << s.period << ',' << s.eval.dy << ',' << s.eval.climbRate << ',' << s.eval.dx << ','
        << s.eval.avgHorizontal << ',' << s.eval.rmsDelta << ',' << s.eval.maxAbsDelta << ',' << s.eval.minAngle
        << ',' << s.eval.maxAngle << ',' << s.eval.lowBoundTicks << ',' << s.eval.highBoundTicks << ','
        << s.eval.cyclesUsed << ',' << (s.eval.converged ? 1 : 0) << '\n';
  }
}

void printSolution(const Solution& s, bool comma) {
  std::cout << "  \"" << s.name << "\": {"
            << "\"periodTicks\":" << s.period
            << ",\"seconds\":" << static_cast<double>(s.period) / TICKS_PER_SECOND
            << ",\"dy\":" << s.eval.dy
            << ",\"climbRate\":" << s.eval.climbRate
            << ",\"dx\":" << s.eval.dx
            << ",\"avgHorizontal\":" << s.eval.avgHorizontal
            << ",\"rmsDelta\":" << s.eval.rmsDelta
            << ",\"maxAbsDelta\":" << s.eval.maxAbsDelta
            << ",\"minAngle\":" << s.eval.minAngle
            << ",\"maxAngle\":" << s.eval.maxAngle
            << ",\"lowBoundTicks\":" << s.eval.lowBoundTicks
            << ",\"highBoundTicks\":" << s.eval.highBoundTicks
            << ",\"cyclesUsed\":" << s.eval.cyclesUsed
            << ",\"converged\":" << (s.eval.converged ? "true" : "false")
            << "}" << (comma ? "," : "") << "\n";
}

}  // namespace

int main() {
  for (int i = 0; i < 65536; ++i) {
    sinTable[i] = static_cast<float>(std::sin(static_cast<double>(i) * PI * 2.0 / 65536.0));
  }
  horizon = horizonState();
  const auto start = std::chrono::steady_clock::now();

  std::vector<double> seed = loadAngles("analysis/fourier_high_order_k100_fourier20_waveform.csv");
  Solution initial{"k100_seed", static_cast<int>(seed.size()), seed, evaluate(seed)};

  std::vector<double> stepAngles = seed;
  const Eval stepEval = stepOptimize(stepAngles);
  Solution step{"framewise_step", static_cast<int>(stepAngles.size()), stepAngles, stepEval};

  std::vector<double> lineAngles = seed;
  Eval lineEval = lineSearchOptimize(lineAngles);
  const Eval linePolishedEval = stepOptimize(lineAngles);
  if (better(linePolishedEval, lineEval)) lineEval = linePolishedEval;
  Solution line{"framewise_line", static_cast<int>(lineAngles.size()), lineAngles, lineEval};

  std::vector<Solution> sols = {initial, step, line};
  for (const Solution& s : sols) {
    writeWaveform(s);
    writeTrajectory(s);
  }
  writeSummary(sols);

  const auto end = std::chrono::steady_clock::now();
  std::cout << std::setprecision(17);
  std::cout << "{\n";
  for (size_t i = 0; i < sols.size(); ++i) printSolution(sols[i], true);
  std::cout << "  \"elapsedSeconds\": " << std::chrono::duration<double>(end - start).count() << "\n";
  std::cout << "}\n";
  return 0;
}
