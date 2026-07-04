#define main segmented_optimizer_entry_point
#include "segmented_sampled_optimize.cpp"
#undef main

#include <cctype>
#include <map>

namespace {

enum class AuditMode {
  SoftSpeed,
  HardSpeed,
  Climb,
};

AuditMode auditMode = AuditMode::SoftSpeed;

std::pair<std::string, std::string> splitCsvLine(const std::string& line) {
  const size_t pos = line.find(',');
  if (pos == std::string::npos) return {line, ""};
  return {line.substr(0, pos), line.substr(pos + 1)};
}

Params loadParamsCsv(const std::string& path) {
  Params p;
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot open params csv: " + path);
  }
  std::string line;
  std::getline(in, line);
  while (std::getline(in, line)) {
    const auto [field, raw] = splitCsvLine(line);
    if (field.empty() || raw.empty()) continue;
    auto asInt = [&]() { return static_cast<int>(std::llround(std::stod(raw))); };
    auto asDouble = [&]() { return std::stod(raw); };
    if (field == "n1_negative_constant") p.n1 = asInt();
    else if (field == "n12_negative_transition_linear") p.n12 = asInt();
    else if (field == "n2_negative_bezier") p.n2 = asInt();
    else if (field == "n3_hold_zero_after_negative") p.n3 = asInt();
    else if (field == "n34_positive_ramp_linear") p.n34 = asInt();
    else if (field == "nPosHold_positive_hold") p.nPosHold = asInt();
    else if (field == "n4_positive_bezier_to_zero") p.n4 = asInt();
    else if (field == "n5_hold_zero_end") p.n5 = asInt();
    else if (field == "negative_constant_angle") p.negHoldAngle = asDouble();
    else if (field.rfind("neg_control_", 0) == 0) {
      const int index = std::stoi(field.substr(12));
      if (0 <= index && index < NEG_CONTROLS) p.neg[index] = asDouble();
    } else if (field.rfind("pos_control_", 0) == 0) {
      const int index = std::stoi(field.substr(12));
      if (0 <= index && index < POS_CONTROLS) p.pos[index] = asDouble();
    }
  }
  enforce(p);
  return p;
}

double auditScore(const Eval& e) {
  if (auditMode == AuditMode::Climb) return e.climbRate;
  if (auditMode == AuditMode::HardSpeed) {
    if (!e.converged || e.dy < -1e-12) return -1e100;
    return e.avgHorizontal;
  }
  return e.objective;
}

bool auditBetterEval(const Eval& a, const Eval& b) {
  const double sa = auditScore(a);
  const double sb = auditScore(b);
  if (sa != sb) return sa > sb;
  return a.dy > b.dy;
}

bool auditBetterCandidate(const Candidate& a, const Candidate& b) {
  return auditBetterEval(a.eval, b.eval);
}

void updateBest(Candidate& best, const Params& params) {
  Candidate trial{params, evaluateParams(params)};
  if (auditBetterCandidate(trial, best)) best = trial;
}

Candidate fineCoordinatePolish(Candidate seed) {
  Candidate best = seed;
  const std::vector<int> durationSteps = {1};
  const std::vector<double> angleSteps = {0.05, 0.02, 0.01};
  for (int outer = 0; outer < 4; ++outer) {
    bool improved = false;
    const Eval beforeOuter = best.eval;
    for (int dStep : durationSteps) {
      for (int pass = 0; pass < 4; ++pass) {
        const Eval beforePass = best.eval;
        for (int field = 0; field < 8; ++field) {
          for (int sign : {-1, 1}) {
            Params p = best.params;
            int* durations[] = {&p.n1, &p.n12, &p.n2, &p.n3, &p.n34, &p.nPosHold, &p.n4, &p.n5};
            *durations[field] += sign * dStep;
            enforce(p);
            updateBest(best, p);
          }
        }
        if (!auditBetterEval(best.eval, beforePass)) break;
      }
    }
    for (double aStep : angleSteps) {
      for (int pass = 0; pass < 5; ++pass) {
        const Eval beforePass = best.eval;
        for (double sign : {-1.0, 1.0}) {
          Params p = best.params;
          p.negHoldAngle += sign * aStep;
          enforce(p);
          updateBest(best, p);
        }
        for (int i = 0; i < NEG_CONTROLS; ++i) {
          for (double sign : {-1.0, 1.0}) {
            Params p = best.params;
            p.neg[i] += sign * aStep;
            enforce(p);
            updateBest(best, p);
          }
        }
        for (int i = 0; i < POS_CONTROLS - 1; ++i) {
          for (double sign : {-1.0, 1.0}) {
            Params p = best.params;
            p.pos[i] += sign * aStep;
            enforce(p);
            updateBest(best, p);
          }
        }
        if (!auditBetterEval(best.eval, beforePass)) break;
      }
    }
    improved = auditBetterEval(best.eval, beforeOuter);
    if (!improved) break;
  }
  return best;
}

Candidate randomLocalProbe(Candidate seed, int trials, uint64_t seedValue) {
  std::mt19937_64 rng(seedValue);
  Candidate best = seed;
  const std::vector<double> angleSigmas = {0.03, 0.08, 0.2, 0.6, 1.5};
  const std::vector<double> durationSigmas = {0.15, 0.35, 0.8, 1.8};
  for (int i = 0; i < trials; ++i) {
    const double aSigma = angleSigmas[static_cast<size_t>(i) % angleSigmas.size()];
    const double dSigma = durationSigmas[static_cast<size_t>(i / static_cast<int>(angleSigmas.size())) %
                                        durationSigmas.size()];
    Params p = seed.params;
    auto perturbDuration = [&](int& v) {
      v += static_cast<int>(std::llround(std::normal_distribution<double>(0.0, dSigma)(rng)));
    };
    perturbDuration(p.n1);
    perturbDuration(p.n12);
    perturbDuration(p.n2);
    perturbDuration(p.n3);
    perturbDuration(p.n34);
    perturbDuration(p.nPosHold);
    perturbDuration(p.n4);
    perturbDuration(p.n5);
    p.negHoldAngle += std::normal_distribution<double>(0.0, aSigma)(rng);
    for (double& v : p.neg) v += std::normal_distribution<double>(0.0, aSigma)(rng);
    for (int j = 0; j < POS_CONTROLS - 1; ++j) p.pos[j] += std::normal_distribution<double>(0.0, aSigma)(rng);
    enforce(p);
    updateBest(best, p);
  }
  return fineCoordinatePolish(best);
}

void printAudit(const Candidate& base, const Candidate& coarse, const Candidate& fine, const Candidate& randomBest) {
  auto printEval = [](const char* name, const Eval& e) {
    std::cout << std::setprecision(17) << name << ": objective=" << e.objective << " avgH=" << e.avgHorizontal
              << " climb=" << e.climbRate << " dy=" << e.dy << " period=" << e.period
              << " auditScore=" << auditScore(e) << " meanDelta=" << e.meanAbsDelta
              << " maxDelta=" << e.maxAbsDelta << "\n";
  };
  printEval("base", base.eval);
  printEval("after_existing_coordinatePolish", coarse.eval);
  printEval("after_fine_coordinatePolish", fine.eval);
  printEval("after_random_local_probe", randomBest.eval);
  std::cout << std::setprecision(17) << "best_minus_base_score=" << auditScore(randomBest.eval) - auditScore(base.eval)
            << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: audit_segmented_local.exe <speed|speed-hard|climb> <best_params.csv> <trials> [seed] [outDir]\n";
    return 2;
  }
  const std::string modeArg = argv[1];
  if (modeArg == "climb") {
    objectiveMode = ObjectiveMode::ClimbRate;
    auditMode = AuditMode::Climb;
  } else if (modeArg == "speed-hard") {
    objectiveMode = ObjectiveMode::HorizontalSpeed;
    auditMode = AuditMode::HardSpeed;
  } else {
    objectiveMode = ObjectiveMode::HorizontalSpeed;
    auditMode = AuditMode::SoftSpeed;
  }
  const std::string paramsPath = argv[2];
  const int trials = std::stoi(argv[3]);
  const uint64_t seedValue = argc >= 5 ? std::stoull(argv[4], nullptr, 0) : 0xA11D17ULL;
  const std::string outDir = argc >= 6 ? argv[5] : "";

  for (int i = 0; i < 65536; ++i) {
    sinTable[i] = static_cast<float>(std::sin(static_cast<double>(i) * PI * 2.0 / 65536.0));
  }
  horizon = horizonState();

  Candidate base{loadParamsCsv(paramsPath), {}};
  base.eval = evaluateParams(base.params);
  Candidate coarse = auditMode == AuditMode::SoftSpeed ? coordinatePolish(base) : fineCoordinatePolish(base);
  Candidate fine = fineCoordinatePolish(coarse);
  Candidate randomBest = randomLocalProbe(fine, trials, seedValue);
  printAudit(base, coarse, fine, randomBest);
  if (!outDir.empty()) {
    std::filesystem::create_directories(outDir);
    const std::vector<double> angles = buildAngles(randomBest.params);
    writeWaveform(outDir + "/best_waveform.csv", angles, randomBest.params);
    writeSimpleWaveform(outDir + "/best_waveform_simple.csv", angles);
    writeTrajectory(outDir + "/best_trajectory.csv", angles);
    writeParams(outDir + "/best_params.csv", randomBest);
    std::cout << "wrote=" << outDir << "\n";
  }
  return 0;
}
