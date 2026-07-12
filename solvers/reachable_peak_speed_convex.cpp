#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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

struct Point {
  double vx = 0.0;
  double vy = 0.0;
};

struct Params {
  double angle = 0.0;
  double sinPitch = 0.0;
  double horizontalLook = 0.0;
  double lift = 0.0;
  double lookSign = 1.0;
  bool pitchUp = false;
  double branchVy = 0.0;
};

Params makeParams(double angle) {
  angle = clamp(angle, -90.0, 90.0);
  const double mcPitchDeg = -angle;
  const float pitchRadF = static_cast<float>(static_cast<float>(mcPitchDeg) * DEG_TO_RAD);
  const double pitchRad = pitchRadF;
  const double sinPitch = mthSin(pitchRad);
  const double cosPitch = mthCos(pitchRad);
  const double horizontalLook = std::abs(cosPitch);
  const double lift = static_cast<float>(static_cast<float>(cosPitch * cosPitch));
  // The fall-to-glide branch switches when vy + gravity + lift * 0.06 == 0.
  const double branchVy = -GRAVITY - lift * LIFT;
  return {angle, sinPitch, horizontalLook, lift, cosPitch < 0.0 ? -1.0 : 1.0, pitchRad < 0.0, branchVy};
}

Point stepVelocity(const Point& p0, const Params& p) {
  const double oldHorizontalSpeed = std::abs(p0.vx);
  double vx = p0.vx;
  double vy = p0.vy + GRAVITY + p.lift * LIFT;
  if (vy < 0.0 && p.horizontalLook > 0.0) {
    const double yAccel = vy * FALL_TO_GLIDE * p.lift;
    vy += yAccel;
    vx += p.lookSign * yAccel;
  }
  if (p.pitchUp && p.horizontalLook > 0.0) {
    const double climb = oldHorizontalSpeed * -p.sinPitch * PITCH_UP_X;
    vy += climb * PITCH_UP_Y;
    vx -= p.lookSign * climb;
  }
  if (p.horizontalLook > 0.0) {
    vx += (p.lookSign * oldHorizontalSpeed - vx) * HORIZONTAL_ALIGN;
  }
  return {vx * HORIZONTAL_DRAG, vy * VERTICAL_DRAG};
}

double cross(const Point& a, const Point& b, const Point& c) {
  return (b.vx - a.vx) * (c.vy - a.vy) - (b.vy - a.vy) * (c.vx - a.vx);
}

std::vector<Point> convexHull(std::vector<Point>& pts) {
  std::sort(pts.begin(), pts.end(), [](const Point& a, const Point& b) {
    if (a.vx != b.vx) return a.vx < b.vx;
    return a.vy < b.vy;
  });
  pts.erase(std::unique(pts.begin(), pts.end(), [](const Point& a, const Point& b) {
    return std::abs(a.vx - b.vx) < 1e-13 && std::abs(a.vy - b.vy) < 1e-13;
  }), pts.end());
  if (pts.size() <= 2) return pts;

  std::vector<Point> h;
  h.reserve(pts.size() * 2);
  for (const Point& p : pts) {
    while (h.size() >= 2 && cross(h[h.size() - 2], h[h.size() - 1], p) <= 1e-14) {
      h.pop_back();
    }
    h.push_back(p);
  }
  const size_t lowerSize = h.size();
  for (int i = static_cast<int>(pts.size()) - 2; i >= 0; --i) {
    const Point& p = pts[static_cast<size_t>(i)];
    while (h.size() > lowerSize && cross(h[h.size() - 2], h[h.size() - 1], p) <= 1e-14) {
      h.pop_back();
    }
    h.push_back(p);
  }
  if (!h.empty()) h.pop_back();
  return h;
}

struct Args {
  std::filesystem::path outDir = "analysis/reachable_peak_speed_convex";
  int maxTicks = 3000;
  double angleStep = 1.0;
  int printEvery = 50;
  double stopTol = 1e-12;
  int patience = 400;
  double simplifyEps = 0.0;
  bool traceBest = false;
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
    else if (key == "--print-every" && takeValue(i, argc, argv, value)) args.printEvery = std::stoi(value);
    else if (key == "--stop-tol" && takeValue(i, argc, argv, value)) args.stopTol = std::stod(value);
    else if (key == "--patience" && takeValue(i, argc, argv, value)) args.patience = std::stoi(value);
    else if (key == "--simplify-eps" && takeValue(i, argc, argv, value)) args.simplifyEps = std::stod(value);
    else if (key == "--trace-best") args.traceBest = true;
    else {
      std::cerr << "Unknown or incomplete option: " << key << "\n";
      std::exit(2);
    }
  }
  return args;
}

std::vector<Point> simplifyHull(const std::vector<Point>& hull, double eps) {
  if (eps <= 0.0 || hull.size() <= 3) return hull;
  std::vector<Point> pts = hull;
  for (int pass = 0; pass < 8; ++pass) {
    const size_t n = pts.size();
    if (n <= 3) break;
    double maxVx = -std::numeric_limits<double>::infinity();
    for (const Point& p : pts) maxVx = std::max(maxVx, p.vx);
    std::vector<Point> keep;
    keep.reserve(n);
    bool changed = false;
    for (size_t i = 0; i < n; ++i) {
      const Point& a = pts[(i + n - 1) % n];
      const Point& b = pts[i];
      const Point& c = pts[(i + 1) % n];
      const double base = std::hypot(c.vx - a.vx, c.vy - a.vy);
      const double dist = base > 0.0 ? std::abs(cross(a, b, c)) / base : 0.0;
      const bool protectRightEdge = std::abs(b.vx - maxVx) <= eps * 20.0;
      if (dist < eps && !protectRightEdge) {
        changed = true;
      } else {
        keep.push_back(b);
      }
    }
    pts.swap(keep);
    if (!changed) break;
  }
  return pts;
}

enum class SourceKind : int {
  Initial = 0,
  Step = 1,
  MixStep = 2,
};

struct Node {
  Point p;
  int tick = 0;
  SourceKind kind = SourceKind::Initial;
  int parentA = -1;
  int parentB = -1;
  double lambdaA = 1.0;
  int actionIndex = -1;
};

struct TracePoint {
  Point p;
  int nodeId = -1;
};

struct TraceCandidate {
  Point p;
  int existingNode = -1;
  SourceKind kind = SourceKind::Initial;
  int parentA = -1;
  int parentB = -1;
  double lambdaA = 1.0;
  int actionIndex = -1;
};

double cross(const TraceCandidate& a, const TraceCandidate& b, const TraceCandidate& c) {
  return cross(a.p, b.p, c.p);
}

std::vector<TraceCandidate> convexHullTrace(std::vector<TraceCandidate>& pts) {
  std::sort(pts.begin(), pts.end(), [](const TraceCandidate& a, const TraceCandidate& b) {
    if (a.p.vx != b.p.vx) return a.p.vx < b.p.vx;
    return a.p.vy < b.p.vy;
  });
  std::vector<TraceCandidate> unique;
  unique.reserve(pts.size());
  for (const TraceCandidate& p : pts) {
    if (!unique.empty() && std::abs(p.p.vx - unique.back().p.vx) < 1e-13 &&
        std::abs(p.p.vy - unique.back().p.vy) < 1e-13) {
      if (unique.back().kind == SourceKind::MixStep && p.kind != SourceKind::MixStep) {
        unique.back() = p;
      }
    } else {
      unique.push_back(p);
    }
  }
  if (unique.size() <= 2) return unique;

  std::vector<TraceCandidate> h;
  h.reserve(unique.size() * 2);
  for (const TraceCandidate& p : unique) {
    while (h.size() >= 2 && cross(h[h.size() - 2], h[h.size() - 1], p) <= 1e-14) {
      h.pop_back();
    }
    h.push_back(p);
  }
  const size_t lowerSize = h.size();
  for (int i = static_cast<int>(unique.size()) - 2; i >= 0; --i) {
    const TraceCandidate& p = unique[static_cast<size_t>(i)];
    while (h.size() > lowerSize && cross(h[h.size() - 2], h[h.size() - 1], p) <= 1e-14) {
      h.pop_back();
    }
    h.push_back(p);
  }
  if (!h.empty()) h.pop_back();
  return h;
}

std::vector<TraceCandidate> simplifyTraceHull(const std::vector<TraceCandidate>& hull, double eps) {
  if (eps <= 0.0 || hull.size() <= 3) return hull;
  std::vector<TraceCandidate> pts = hull;
  for (int pass = 0; pass < 8; ++pass) {
    const size_t n = pts.size();
    if (n <= 3) break;
    double maxVx = -std::numeric_limits<double>::infinity();
    for (const TraceCandidate& p : pts) maxVx = std::max(maxVx, p.p.vx);
    std::vector<TraceCandidate> keep;
    keep.reserve(n);
    bool changed = false;
    for (size_t i = 0; i < n; ++i) {
      const Point& a = pts[(i + n - 1) % n].p;
      const Point& b = pts[i].p;
      const Point& c = pts[(i + 1) % n].p;
      const double base = std::hypot(c.vx - a.vx, c.vy - a.vy);
      const double dist = base > 0.0 ? std::abs(cross(a, b, c)) / base : 0.0;
      const bool protectRightEdge = std::abs(b.vx - maxVx) <= eps * 20.0;
      if (dist < eps && !protectRightEdge) {
        changed = true;
      } else {
        keep.push_back(pts[i]);
      }
    }
    pts.swap(keep);
    if (!changed) break;
  }
  return pts;
}

bool collectDeterministicActions(
    int nodeId,
    const std::vector<Node>& nodes,
    const std::vector<Params>& actions,
    std::vector<double>& out,
    int& firstMixedNode) {
  const Node& node = nodes[static_cast<size_t>(nodeId)];
  if (node.kind == SourceKind::Initial) return true;
  if (node.kind == SourceKind::MixStep) {
    firstMixedNode = nodeId;
    return false;
  }
  if (!collectDeterministicActions(node.parentA, nodes, actions, out, firstMixedNode)) return false;
  out.push_back(actions[static_cast<size_t>(node.actionIndex)].angle);
  return true;
}

void writeTraceOutputs(
    const Args& args,
    const std::vector<Node>& nodes,
    const std::vector<Params>& actions,
    int bestNode,
    double bestVx,
    double bestVy,
    int bestTick,
    int ticksRun,
    double elapsed,
    size_t maxCandidateCount,
    size_t hullSize) {
  std::filesystem::create_directories(args.outDir);
  int firstMixedNode = -1;
  std::vector<double> deterministicActions;
  const bool deterministic =
      collectDeterministicActions(bestNode, nodes, actions, deterministicActions, firstMixedNode);
  if (deterministic) {
    std::ofstream wf(args.outDir / "trace_waveform.csv");
    wf << "tick,angle\n";
    wf << std::setprecision(17);
    for (size_t i = 0; i < deterministicActions.size(); ++i) {
      wf << (i + 1) << "," << deterministicActions[i] << "\n";
    }
  }

  std::ofstream f(args.outDir / "trace_summary.json");
  f << std::setprecision(17);
  f << "{\n";
  f << "  \"mode\": \"convex_hull_trace\",\n";
  f << "  \"best_horizontal_bps\": " << bestVx * TICKS_PER_SECOND << ",\n";
  f << "  \"best_vx\": " << bestVx << ",\n";
  f << "  \"best_vy\": " << bestVy << ",\n";
  f << "  \"best_tick\": " << bestTick << ",\n";
  f << "  \"best_node\": " << bestNode << ",\n";
  f << "  \"ticks_run\": " << ticksRun << ",\n";
  f << "  \"angle_step_deg\": " << args.angleStep << ",\n";
  f << "  \"simplify_eps\": " << args.simplifyEps << ",\n";
  f << "  \"hull_vertices\": " << hullSize << ",\n";
  f << "  \"node_count\": " << nodes.size() << ",\n";
  f << "  \"max_candidate_count\": " << maxCandidateCount << ",\n";
  f << "  \"elapsed_seconds\": " << elapsed << ",\n";
  f << "  \"deterministic_path\": " << (deterministic ? "true" : "false") << ",\n";
  if (deterministic) {
    f << "  \"deterministic_length\": " << deterministicActions.size() << "\n";
  } else {
    const Node& m = nodes[static_cast<size_t>(firstMixedNode)];
    const Node& a = nodes[static_cast<size_t>(m.parentA)];
    const Node& b = nodes[static_cast<size_t>(m.parentB)];
    f << "  \"first_mixed_node\": " << firstMixedNode << ",\n";
    f << "  \"first_mixed_tick\": " << m.tick << ",\n";
    f << "  \"first_mixed_action_deg\": " << actions[static_cast<size_t>(m.actionIndex)].angle << ",\n";
    f << "  \"first_mixed_lambda_a\": " << m.lambdaA << ",\n";
    f << "  \"first_mixed_parent_a\": {\"node\": " << m.parentA << ", \"tick\": " << a.tick
      << ", \"vx\": " << a.p.vx << ", \"vy\": " << a.p.vy << "},\n";
    f << "  \"first_mixed_parent_b\": {\"node\": " << m.parentB << ", \"tick\": " << b.tick
      << ", \"vx\": " << b.p.vx << ", \"vy\": " << b.p.vy << "}\n";
  }
  f << "}\n";
}

void writeOutputs(
    const Args& args,
    const std::vector<Point>& hull,
    double bestVx,
    double bestVy,
    int bestTick,
    int ticksRun,
    double elapsed,
    size_t maxCandidateCount) {
  std::filesystem::create_directories(args.outDir);
  {
    std::ofstream f(args.outDir / "hull.csv");
    f << "vx,vy,horizontal_bps,vertical_bps\n";
    f << std::setprecision(17);
    for (const Point& p : hull) {
      f << p.vx << "," << p.vy << "," << p.vx * TICKS_PER_SECOND << "," << p.vy * TICKS_PER_SECOND << "\n";
    }
  }
  {
    std::ofstream f(args.outDir / "summary.json");
    f << std::setprecision(17);
    f << "{\n";
    f << "  \"mode\": \"convex_hull_boundary\",\n";
    f << "  \"best_horizontal_bps\": " << bestVx * TICKS_PER_SECOND << ",\n";
    f << "  \"best_vx\": " << bestVx << ",\n";
    f << "  \"best_vy\": " << bestVy << ",\n";
    f << "  \"best_tick\": " << bestTick << ",\n";
    f << "  \"ticks_run\": " << ticksRun << ",\n";
    f << "  \"angle_step_deg\": " << args.angleStep << ",\n";
    f << "  \"simplify_eps\": " << args.simplifyEps << ",\n";
    f << "  \"hull_vertices\": " << hull.size() << ",\n";
    f << "  \"max_candidate_count\": " << maxCandidateCount << ",\n";
    f << "  \"elapsed_seconds\": " << elapsed << "\n";
    f << "}\n";
  }
}

}  // namespace

int main(int argc, char** argv) {
  for (size_t i = 0; i < sinTable.size(); ++i) {
    sinTable[i] = static_cast<float>(std::sin(static_cast<double>(i) * 2.0 * PI / 65536.0));
  }
  const Args args = parseArgs(argc, argv);
  std::vector<Params> actions;
  for (double a = -90.0; a <= 90.0 + args.angleStep * 0.5; a += args.angleStep) {
    actions.push_back(makeParams(a));
  }

  if (args.traceBest) {
    std::vector<Node> nodes;
    nodes.reserve(2'000'000);
    nodes.push_back({{INITIAL_VX, INITIAL_VY}, 0, SourceKind::Initial, -1, -1, 1.0, -1});
    std::vector<TracePoint> hull{{{INITIAL_VX, INITIAL_VY}, 0}};
    double bestVx = INITIAL_VX;
    double bestVy = INITIAL_VY;
    int bestTick = 0;
    int bestNode = 0;
    int lastImproved = 0;
    int ticksRun = 0;
    size_t maxCandidateCount = 0;
    const auto started = std::chrono::steady_clock::now();

    std::cout << "convex hull trace propagation, actions=" << actions.size()
              << ", start vy=" << std::setprecision(12) << INITIAL_VY << "\n";

    for (int tick = 1; tick <= args.maxTicks; ++tick) {
      std::vector<TraceCandidate> candidates;
      candidates.reserve(hull.size() * actions.size() * 2 + hull.size());
      for (const TracePoint& p : hull) {
        candidates.push_back({p.p, p.nodeId, SourceKind::Initial, -1, -1, 1.0, -1});
      }

      for (size_t ai = 0; ai < actions.size(); ++ai) {
        const Params& action = actions[ai];
        for (const TracePoint& p : hull) {
          candidates.push_back(
              {stepVelocity(p.p, action), -1, SourceKind::Step, p.nodeId, -1, 1.0, static_cast<int>(ai)});
        }
        if (hull.size() >= 2) {
          for (size_t i = 0; i < hull.size(); ++i) {
            const TracePoint& a = hull[i];
            const TracePoint& b = hull[(i + 1) % hull.size()];
            const double da = a.p.vy - action.branchVy;
            const double db = b.p.vy - action.branchVy;
            if (da == 0.0) {
              candidates.push_back(
                  {stepVelocity(a.p, action), -1, SourceKind::Step, a.nodeId, -1, 1.0, static_cast<int>(ai)});
            } else if ((da < 0.0 && db > 0.0) || (da > 0.0 && db < 0.0)) {
              const double u = (action.branchVy - a.p.vy) / (b.p.vy - a.p.vy);
              const Point mid{a.p.vx + (b.p.vx - a.p.vx) * u, action.branchVy};
              candidates.push_back(
                  {stepVelocity(mid, action), -1, SourceKind::MixStep, a.nodeId, b.nodeId, 1.0 - u,
                   static_cast<int>(ai)});
            }
          }
        }
      }

      maxCandidateCount = std::max(maxCandidateCount, candidates.size());
      std::vector<TraceCandidate> selected = simplifyTraceHull(convexHullTrace(candidates), args.simplifyEps);
      hull.clear();
      hull.reserve(selected.size());
      for (const TraceCandidate& c : selected) {
        int nodeId = c.existingNode;
        if (nodeId < 0) {
          nodeId = static_cast<int>(nodes.size());
          nodes.push_back({c.p, tick, c.kind, c.parentA, c.parentB, c.lambdaA, c.actionIndex});
        }
        hull.push_back({c.p, nodeId});
      }

      ticksRun = tick;
      for (const TracePoint& p : hull) {
        if (p.p.vx > bestVx + args.stopTol) {
          bestVx = p.p.vx;
          bestVy = p.p.vy;
          bestTick = tick;
          bestNode = p.nodeId;
          lastImproved = tick;
        }
      }

      if (args.printEvery > 0 && (tick % args.printEvery == 0 || tick == 1)) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - started).count();
        std::cout << "tick=" << tick
                  << " hull=" << hull.size()
                  << " nodes=" << nodes.size()
                  << " candidates=" << candidates.size()
                  << " best=" << std::fixed << std::setprecision(9) << bestVx * TICKS_PER_SECOND
                  << " m/s vy=" << bestVy * TICKS_PER_SECOND
                  << " bestTick=" << bestTick
                  << " elapsed=" << elapsed << "s\n";
      }
      if (tick - lastImproved >= args.patience) break;
    }

    const auto ended = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(ended - started).count();
    writeTraceOutputs(args, nodes, actions, bestNode, bestVx, bestVy, bestTick, ticksRun, elapsed, maxCandidateCount,
                      hull.size());
    std::cout << "\nBEST\n";
    std::cout << "best_horizontal_bps=" << std::fixed << std::setprecision(12) << bestVx * TICKS_PER_SECOND << "\n";
    std::cout << "best_vx=" << bestVx << " best_vy=" << bestVy
              << " tick=" << bestTick << " hull=" << hull.size() << " nodes=" << nodes.size()
              << " elapsed=" << elapsed << "s\n";
    std::cout << "out_dir=" << args.outDir.string() << "\n";
    return 0;
  }

  std::vector<Point> hull{{INITIAL_VX, INITIAL_VY}};
  double bestVx = INITIAL_VX;
  double bestVy = INITIAL_VY;
  int bestTick = 0;
  int lastImproved = 0;
  int ticksRun = 0;
  size_t maxCandidateCount = 0;
  const auto started = std::chrono::steady_clock::now();

  std::cout << "convex hull propagation, actions=" << actions.size()
            << ", start vy=" << std::setprecision(12) << INITIAL_VY << "\n";

  for (int tick = 1; tick <= args.maxTicks; ++tick) {
    std::vector<Point> candidates;
    candidates.reserve(hull.size() * actions.size() * 2 + hull.size());
    candidates.insert(candidates.end(), hull.begin(), hull.end());

    for (const Params& action : actions) {
      for (const Point& p : hull) {
        candidates.push_back(stepVelocity(p, action));
      }
      if (hull.size() >= 2) {
        for (size_t i = 0; i < hull.size(); ++i) {
          const Point& a = hull[i];
          const Point& b = hull[(i + 1) % hull.size()];
          const double da = a.vy - action.branchVy;
          const double db = b.vy - action.branchVy;
          if (da == 0.0) {
            candidates.push_back(stepVelocity(a, action));
          } else if ((da < 0.0 && db > 0.0) || (da > 0.0 && db < 0.0)) {
            const double u = (action.branchVy - a.vy) / (b.vy - a.vy);
            const Point mid{a.vx + (b.vx - a.vx) * u, action.branchVy};
            candidates.push_back(stepVelocity(mid, action));
          }
        }
      }
    }

    maxCandidateCount = std::max(maxCandidateCount, candidates.size());
    hull = simplifyHull(convexHull(candidates), args.simplifyEps);
    ticksRun = tick;
    for (const Point& p : hull) {
      if (p.vx > bestVx + args.stopTol) {
        bestVx = p.vx;
        bestVy = p.vy;
        bestTick = tick;
        lastImproved = tick;
      }
    }

    if (args.printEvery > 0 && (tick % args.printEvery == 0 || tick == 1)) {
      const auto now = std::chrono::steady_clock::now();
      const double elapsed = std::chrono::duration<double>(now - started).count();
      std::cout << "tick=" << tick
                << " hull=" << hull.size()
                << " candidates=" << candidates.size()
                << " best=" << std::fixed << std::setprecision(9) << bestVx * TICKS_PER_SECOND
                << " m/s vy=" << bestVy * TICKS_PER_SECOND
                << " bestTick=" << bestTick
                << " elapsed=" << elapsed << "s\n";
    }
    if (tick - lastImproved >= args.patience) break;
  }

  const auto ended = std::chrono::steady_clock::now();
  const double elapsed = std::chrono::duration<double>(ended - started).count();
  writeOutputs(args, hull, bestVx, bestVy, bestTick, ticksRun, elapsed, maxCandidateCount);
  std::cout << "\nBEST\n";
  std::cout << "best_horizontal_bps=" << std::fixed << std::setprecision(12) << bestVx * TICKS_PER_SECOND << "\n";
  std::cout << "best_vx=" << bestVx << " best_vy=" << bestVy
            << " tick=" << bestTick << " hull=" << hull.size()
            << " elapsed=" << elapsed << "s\n";
  std::cout << "out_dir=" << args.outDir.string() << "\n";
  return 0;
}
