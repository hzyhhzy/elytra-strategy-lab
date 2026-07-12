#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr double TICKS_PER_SECOND = 20.0;
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
constexpr double INITIAL_VX = 0.0;
constexpr double INITIAL_VY = -3.920003814700875;

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

struct Params {
  double sinPitch = 0.0;
  double horizontalLook = 0.0;
  double lift = 0.0;
  double lookSign = 1.0;
  bool pitchUp = false;
};

Params makeParams(double angle) {
  angle = clamp(angle, -90.0, 90.0);
  const double mcPitchDeg = -angle;
  const float pitchRadF = static_cast<float>(static_cast<float>(mcPitchDeg) * DEG_TO_RAD);
  const double pitchRad = pitchRadF;
  const double sinPitch = mthSin(pitchRad);
  const double cosPitch = mthCos(pitchRad);
  const double horizontalLook = std::abs(cosPitch);
  double lift = static_cast<float>(static_cast<float>(cosPitch * cosPitch));
  return {sinPitch, horizontalLook, lift, cosPitch < 0.0 ? -1.0 : 1.0, pitchRad < 0.0};
}

void stepVelocity(double vx, double vy, const Params& p, double& outVx, double& outVy) {
  const double oldHorizontalSpeed = std::abs(vx);
  double nextVx = vx;
  double nextVy = vy + GRAVITY + p.lift * LIFT;
  if (nextVy < 0.0 && p.horizontalLook > 0.0) {
    const double yAccel = nextVy * FALL_TO_GLIDE * p.lift;
    nextVy += yAccel;
    nextVx += p.lookSign * yAccel;
  }
  if (p.pitchUp && p.horizontalLook > 0.0) {
    const double climb = oldHorizontalSpeed * -p.sinPitch * PITCH_UP_X;
    nextVy += climb * PITCH_UP_Y;
    nextVx -= p.lookSign * climb;
  }
  if (p.horizontalLook > 0.0) {
    nextVx += (p.lookSign * oldHorizontalSpeed - nextVx) * HORIZONTAL_ALIGN;
  }
  outVx = nextVx * HORIZONTAL_DRAG;
  outVy = nextVy * VERTICAL_DRAG;
}

struct Args {
  std::filesystem::path outDir = "analysis/reachable_peak_speed_bfs";
  int maxTicks = 5000;
  double angleStep = 1.0;
  double vxMin = 0.0;
  double vxMax = 4.8;
  double vyMin = -4.6;
  double vyMax = 1.6;
  double dvx = 0.0005;
  double dvy = 0.0005;
  int printEvery = 1000000;
  bool reachOnly = false;
  bool writeMap = false;
};

bool takeValue(int& i, int argc, char** argv, std::string& out) {
  if (i + 1 >= argc) return false;
  out = argv[++i];
  return true;
}

Args parseArgs(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string key = argv[i];
    std::string value;
    if (key == "--out-dir" && takeValue(i, argc, argv, value)) args.outDir = value;
    else if (key == "--ticks" && takeValue(i, argc, argv, value)) args.maxTicks = std::stoi(value);
    else if (key == "--angle-step" && takeValue(i, argc, argv, value)) args.angleStep = std::stod(value);
    else if (key == "--vx-min" && takeValue(i, argc, argv, value)) args.vxMin = std::stod(value);
    else if (key == "--vx-max" && takeValue(i, argc, argv, value)) args.vxMax = std::stod(value);
    else if (key == "--vy-min" && takeValue(i, argc, argv, value)) args.vyMin = std::stod(value);
    else if (key == "--vy-max" && takeValue(i, argc, argv, value)) args.vyMax = std::stod(value);
    else if (key == "--dvx" && takeValue(i, argc, argv, value)) args.dvx = std::stod(value);
    else if (key == "--dvy" && takeValue(i, argc, argv, value)) args.dvy = std::stod(value);
    else if (key == "--print-every" && takeValue(i, argc, argv, value)) args.printEvery = std::stoi(value);
    else if (key == "--reach-only") args.reachOnly = true;
    else if (key == "--write-map") args.writeMap = true;
    else {
      std::cerr << "Unknown or incomplete option: " << key << "\n";
      std::exit(2);
    }
  }
  return args;
}

int32_t cellId(double vx, double vy, const Args& args, int nx, int ny) {
  const int ix = static_cast<int>(std::floor((vx - args.vxMin) / args.dvx));
  const int iy = static_cast<int>(std::floor((vy - args.vyMin) / args.dvy));
  if (ix < 0 || ix >= nx || iy < 0 || iy >= ny) return -1;
  return static_cast<int32_t>(ix * ny + iy);
}

struct QueueEntry {
  int32_t cell = -1;
  int32_t tick = 0;
  float vx = 0.0f;
  float vy = 0.0f;
};

struct TraceRow {
  int tick = 0;
  double angle = 0.0;
  double x = 0.0;
  double y = 0.0;
  double vx = 0.0;
  double vy = 0.0;
};

std::vector<double> reconstructActions(
    int32_t bestCell,
    int32_t startCell,
    const std::vector<int32_t>& predCell,
    const std::vector<int16_t>& predAction,
    const std::vector<double>& actions) {
  std::vector<double> result;
  int32_t cur = bestCell;
  while (cur != startCell) {
    const int16_t ai = predAction[static_cast<size_t>(cur)];
    if (cur < 0 || ai < 0) {
      std::cerr << "Broken predecessor chain at cell " << cur << "\n";
      std::exit(3);
    }
    result.push_back(actions[static_cast<size_t>(ai)]);
    cur = predCell[static_cast<size_t>(cur)];
  }
  std::reverse(result.begin(), result.end());
  return result;
}

std::vector<TraceRow> simulate(const std::vector<double>& pitchActions) {
  std::vector<TraceRow> rows;
  rows.reserve(pitchActions.size());
  double x = 0.0;
  double y = 0.0;
  double vx = INITIAL_VX;
  double vy = INITIAL_VY;
  for (size_t i = 0; i < pitchActions.size(); ++i) {
    const Params p = makeParams(pitchActions[i]);
    double nextVx = 0.0;
    double nextVy = 0.0;
    stepVelocity(vx, vy, p, nextVx, nextVy);
    vx = nextVx;
    vy = nextVy;
    x += vx;
    y += vy;
    rows.push_back({static_cast<int>(i + 1), pitchActions[i], x, y, vx, vy});
  }
  return rows;
}

void writeOutputs(
    const Args& args,
    const std::vector<double>& pitchActions,
    const std::vector<TraceRow>& rows,
    double bestVx,
    double bestVy,
    int bestTick,
    int32_t bestCell,
    size_t visited,
    int ticksRun,
    int nx,
    int ny) {
  std::filesystem::create_directories(args.outDir);
  {
    std::ofstream f(args.outDir / "waveform.csv");
    f << "tick,angle\n";
    f << std::setprecision(17);
    for (size_t i = 0; i < pitchActions.size(); ++i) {
      f << (i + 1) << "," << pitchActions[i] << "\n";
    }
  }
  {
    std::ofstream f(args.outDir / "trajectory.csv");
    f << "tick,angle,x,y,vx,vy,horizontal_bps,vertical_bps\n";
    f << std::setprecision(17);
    for (const auto& row : rows) {
      f << row.tick << "," << row.angle << "," << row.x << "," << row.y << ","
        << row.vx << "," << row.vy << "," << row.vx * TICKS_PER_SECOND << ","
        << row.vy * TICKS_PER_SECOND << "\n";
    }
  }

  double minAngle = 0.0;
  double maxAngle = 0.0;
  double rmsDelta = 0.0;
  double maxAbsDelta = 0.0;
  if (!pitchActions.empty()) {
    minAngle = *std::min_element(pitchActions.begin(), pitchActions.end());
    maxAngle = *std::max_element(pitchActions.begin(), pitchActions.end());
    if (pitchActions.size() > 1) {
      double sumSq = 0.0;
      for (size_t i = 1; i < pitchActions.size(); ++i) {
        const double d = pitchActions[i] - pitchActions[i - 1];
        sumSq += d * d;
        maxAbsDelta = std::max(maxAbsDelta, std::abs(d));
      }
      rmsDelta = std::sqrt(sumSq / static_cast<double>(pitchActions.size() - 1));
    }
  }
  const double finalDrop = rows.empty() ? 0.0 : rows.back().y;
  const double finalX = rows.empty() ? 0.0 : rows.back().x;
  std::ofstream f(args.outDir / "summary.json");
  f << std::setprecision(17);
  f << "{\n";
  f << "  \"best_horizontal_bps\": " << bestVx * TICKS_PER_SECOND << ",\n";
  f << "  \"best_vx\": " << bestVx << ",\n";
  f << "  \"best_vy\": " << bestVy << ",\n";
  f << "  \"best_tick\": " << bestTick << ",\n";
  f << "  \"best_cell\": " << bestCell << ",\n";
  f << "  \"visited_cells\": " << visited << ",\n";
  f << "  \"ticks_run\": " << ticksRun << ",\n";
  f << "  \"angle_step_deg\": " << args.angleStep << ",\n";
  f << "  \"dvx\": " << args.dvx << ",\n";
  f << "  \"dvy\": " << args.dvy << ",\n";
  f << "  \"vx_min\": " << args.vxMin << ",\n";
  f << "  \"vx_max\": " << args.vxMax << ",\n";
  f << "  \"vy_min\": " << args.vyMin << ",\n";
  f << "  \"vy_max\": " << args.vyMax << ",\n";
  f << "  \"grid_nx\": " << nx << ",\n";
  f << "  \"grid_ny\": " << ny << ",\n";
  f << "  \"min_angle\": " << minAngle << ",\n";
  f << "  \"max_angle\": " << maxAngle << ",\n";
  f << "  \"rms_delta_deg\": " << rmsDelta << ",\n";
  f << "  \"max_abs_delta_deg\": " << maxAbsDelta << ",\n";
  f << "  \"final_drop_m\": " << finalDrop << ",\n";
  f << "  \"final_x_m\": " << finalX << "\n";
  f << "}\n";
}

void writeReachOnlySummary(
    const Args& args,
    double bestVx,
    double bestVy,
    int bestTick,
    int32_t bestCell,
    size_t visited,
    int ticksRun,
    int nx,
    int ny,
    double elapsed) {
  std::filesystem::create_directories(args.outDir);
  std::ofstream f(args.outDir / "summary.json");
  f << std::setprecision(17);
  f << "{\n";
  f << "  \"mode\": \"reach_only\",\n";
  f << "  \"best_horizontal_bps\": " << bestVx * TICKS_PER_SECOND << ",\n";
  f << "  \"best_vx\": " << bestVx << ",\n";
  f << "  \"best_vy\": " << bestVy << ",\n";
  f << "  \"best_tick\": " << bestTick << ",\n";
  f << "  \"best_cell\": " << bestCell << ",\n";
  f << "  \"visited_cells\": " << visited << ",\n";
  f << "  \"ticks_run\": " << ticksRun << ",\n";
  f << "  \"angle_step_deg\": " << args.angleStep << ",\n";
  f << "  \"dvx\": " << args.dvx << ",\n";
  f << "  \"dvy\": " << args.dvy << ",\n";
  f << "  \"vx_min\": " << args.vxMin << ",\n";
  f << "  \"vx_max\": " << args.vxMax << ",\n";
  f << "  \"vy_min\": " << args.vyMin << ",\n";
  f << "  \"vy_max\": " << args.vyMax << ",\n";
  f << "  \"grid_nx\": " << nx << ",\n";
  f << "  \"grid_ny\": " << ny << ",\n";
  f << "  \"elapsed_seconds\": " << elapsed << "\n";
  f << "}\n";
}

void writeReachMap(const Args& args, const std::vector<uint8_t>& visited, int nx, int ny) {
  std::filesystem::create_directories(args.outDir);
  std::ofstream f(args.outDir / "reachable_map.pgm", std::ios::binary);
  f << "P5\n" << nx << " " << ny << "\n255\n";
  std::vector<uint8_t> row(static_cast<size_t>(nx));
  for (int iy = ny - 1; iy >= 0; --iy) {
    for (int ix = 0; ix < nx; ++ix) {
      row[static_cast<size_t>(ix)] = visited[static_cast<size_t>(ix) * static_cast<size_t>(ny) + static_cast<size_t>(iy)]
          ? 255
          : 0;
    }
    f.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
  }
}

}  // namespace

int main(int argc, char** argv) {
  for (size_t i = 0; i < sinTable.size(); ++i) {
    sinTable[i] = static_cast<float>(std::sin(static_cast<double>(i) * 2.0 * PI / 65536.0));
  }
  const Args args = parseArgs(argc, argv);
  const int nx = static_cast<int>(std::ceil((args.vxMax - args.vxMin) / args.dvx));
  const int ny = static_cast<int>(std::ceil((args.vyMax - args.vyMin) / args.dvy));
  const int64_t cellCount64 = static_cast<int64_t>(nx) * static_cast<int64_t>(ny);
  if (cellCount64 > std::numeric_limits<int32_t>::max()) {
    std::cerr << "Grid is too large for int32 cell ids.\n";
    return 2;
  }
  const size_t cellCount = static_cast<size_t>(cellCount64);
  std::vector<double> actions;
  std::vector<Params> params;
  for (double a = -90.0; a <= 90.0 + args.angleStep * 0.5; a += args.angleStep) {
    actions.push_back(a);
    params.push_back(makeParams(a));
  }

  const int32_t startCell = cellId(INITIAL_VX, INITIAL_VY, args, nx, ny);
  if (startCell < 0) {
    std::cerr << "Initial velocity is outside the grid.\n";
    return 2;
  }

  if (args.reachOnly) {
    std::vector<uint8_t> visited(cellCount, 0);
    std::deque<QueueEntry> queue;
    queue.push_back({startCell, 0, static_cast<float>(INITIAL_VX), static_cast<float>(INITIAL_VY)});
    visited[static_cast<size_t>(startCell)] = 1;

    size_t popped = 0;
    size_t visitedCount = 1;
    int32_t bestCell = startCell;
    double bestVx = INITIAL_VX;
    double bestVy = INITIAL_VY;
    int bestTick = 0;
    int ticksRun = 0;
    const auto started = std::chrono::steady_clock::now();

    std::cout << "grid " << nx << "x" << ny << "=" << cellCount
              << " cells, actions=" << actions.size()
              << ", mode=reach_only"
              << ", start vy=" << std::setprecision(12) << INITIAL_VY << "\n";

    while (!queue.empty()) {
      const QueueEntry current = queue.front();
      queue.pop_front();
      ++popped;
      ticksRun = std::max(ticksRun, current.tick);
      if (current.tick < args.maxTicks) {
        const double vx = current.vx;
        const double vy = current.vy;
        for (size_t ai = 0; ai < params.size(); ++ai) {
          double nextVx = 0.0;
          double nextVy = 0.0;
          stepVelocity(vx, vy, params[ai], nextVx, nextVy);
          const int32_t nextCell = cellId(nextVx, nextVy, args, nx, ny);
          if (nextCell < 0) continue;
          const size_t ci = static_cast<size_t>(nextCell);
          if (visited[ci]) continue;
          visited[ci] = 1;
          queue.push_back({nextCell, current.tick + 1, static_cast<float>(nextVx), static_cast<float>(nextVy)});
          ++visitedCount;
          if (nextVx > bestVx) {
            bestVx = nextVx;
            bestVy = nextVy;
            bestCell = nextCell;
            bestTick = current.tick + 1;
          }
        }
      }

      if (args.printEvery > 0 && popped % static_cast<size_t>(args.printEvery) == 0) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - started).count();
        std::cout << "popped=" << popped
                  << " pending=" << queue.size()
                  << " visited=" << visitedCount
                  << " tick=" << current.tick
                  << " best=" << std::fixed << std::setprecision(6) << bestVx * TICKS_PER_SECOND
                  << " m/s vy=" << bestVy * TICKS_PER_SECOND
                  << " elapsed=" << elapsed << "s\n";
      }
    }

    const auto ended = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(ended - started).count();
    if (args.writeMap) {
      std::cout << "writing reachable_map.pgm...\n";
      writeReachMap(args, visited, nx, ny);
    }
    writeReachOnlySummary(args, bestVx, bestVy, bestTick, bestCell, visitedCount, ticksRun, nx, ny, elapsed);
    std::cout << "\nBEST\n";
    std::cout << "best_horizontal_bps=" << std::fixed << std::setprecision(9)
              << bestVx * TICKS_PER_SECOND << "\n";
    std::cout << "best_vx=" << bestVx << " best_vy=" << bestVy
              << " tick=" << bestTick << " visited=" << visitedCount
              << " elapsed=" << elapsed << "s\n";
    std::cout << "out_dir=" << args.outDir.string() << "\n";
    return 0;
  }

  std::vector<uint8_t> visited(cellCount, 0);
  std::vector<int32_t> predCell(cellCount, -1);
  std::vector<int16_t> predAction(cellCount, -1);
  std::vector<QueueEntry> queue;
  queue.reserve(1 << 20);
  queue.push_back({startCell, 0, static_cast<float>(INITIAL_VX), static_cast<float>(INITIAL_VY)});
  visited[static_cast<size_t>(startCell)] = 1;

  size_t head = 0;
  size_t visitedCount = 1;
  int32_t bestCell = startCell;
  double bestVx = INITIAL_VX;
  double bestVy = INITIAL_VY;
  int bestTick = 0;
  int ticksRun = 0;
  const auto started = std::chrono::steady_clock::now();

  std::cout << "grid " << nx << "x" << ny << "=" << cellCount
            << " cells, actions=" << actions.size()
            << ", start vy=" << std::setprecision(12) << INITIAL_VY << "\n";

  while (head < queue.size()) {
    const QueueEntry current = queue[head++];
    ticksRun = std::max(ticksRun, current.tick);
    if (current.tick >= args.maxTicks) continue;
    const double vx = current.vx;
    const double vy = current.vy;
    for (size_t ai = 0; ai < params.size(); ++ai) {
      double nextVx = 0.0;
      double nextVy = 0.0;
      stepVelocity(vx, vy, params[ai], nextVx, nextVy);
      const int32_t nextCell = cellId(nextVx, nextVy, args, nx, ny);
      if (nextCell < 0) continue;
      const size_t ci = static_cast<size_t>(nextCell);
      if (visited[ci]) continue;
      visited[ci] = 1;
      predCell[ci] = current.cell;
      predAction[ci] = static_cast<int16_t>(ai);
      queue.push_back({nextCell, current.tick + 1, static_cast<float>(nextVx), static_cast<float>(nextVy)});
      ++visitedCount;
      if (nextVx > bestVx) {
        bestVx = nextVx;
        bestVy = nextVy;
        bestCell = nextCell;
        bestTick = current.tick + 1;
      }
    }

    if (args.printEvery > 0 && head % static_cast<size_t>(args.printEvery) == 0) {
      const auto now = std::chrono::steady_clock::now();
      const double elapsed = std::chrono::duration<double>(now - started).count();
      std::cout << "popped=" << head
                << " queue=" << queue.size()
                << " visited=" << visitedCount
                << " tick=" << current.tick
                << " best=" << std::fixed << std::setprecision(6) << bestVx * TICKS_PER_SECOND
                << " m/s vy=" << bestVy * TICKS_PER_SECOND
                << " elapsed=" << elapsed << "s\n";
    }
  }

  const auto pitchActions = reconstructActions(bestCell, startCell, predCell, predAction, actions);
  const auto rows = simulate(pitchActions);
  writeOutputs(args, pitchActions, rows, bestVx, bestVy, bestTick, bestCell, visitedCount, ticksRun, nx, ny);

  const auto ended = std::chrono::steady_clock::now();
  const double elapsed = std::chrono::duration<double>(ended - started).count();
  std::cout << "\nBEST\n";
  std::cout << "best_horizontal_bps=" << std::fixed << std::setprecision(9)
            << bestVx * TICKS_PER_SECOND << "\n";
  std::cout << "best_vx=" << bestVx << " best_vy=" << bestVy
            << " tick=" << bestTick << " visited=" << visitedCount
            << " elapsed=" << elapsed << "s\n";
  std::cout << "out_dir=" << args.outDir.string() << "\n";
  return 0;
}
