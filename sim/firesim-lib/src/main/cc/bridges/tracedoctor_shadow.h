#ifndef __TRACEDOCTOR_SHADOW_C
#define __TRACEDOCTOR_SHADOW_C

#include "tracedoctor_worker.h"
#include "tracedoctor_tea.h"

#include <map>
#include <random>
#include <stdexcept>
#include <array>
#include <unordered_map>
#include <limits>
#include <fstream>
#include <iostream>

struct __attribute__((__packed__)) traceStatsToken {
  uint64_t branchLatency;
  
  uint8_t numCommit;
  uint8_t isBranch;
  uint8_t filledMSHRs;

  uint8_t memAccesses;
  uint8_t hitsInCache;
  uint8_t missesInCache;

  uint8_t helpDebug;
  
  uint8_t taintsCalced;
  uint8_t yrotsOnCalc;
  uint8_t taintedLoads;

  uint8_t filledIntIssueSlots;
  uint8_t filledFpIssueSlots;
  uint8_t filledMemIssueSlots;

  uint16_t pad1;
  uint8_t pad2;

  uint64_t pad3;
  uint64_t pad4;
  uint64_t pad5;
  uint64_t pad6;
  uint64_t pad7;
};

struct traceStatsSample {
  uint8_t numCommit;
  
  uint8_t isBranch[4];
  uint16_t branchLatency[4];

  uint8_t filledMSHRs;

  uint8_t memAccesses;
  uint8_t hitsInCache;
  uint8_t missesInCache;

  uint8_t taintsCalced;
  uint8_t yrotsOnCalc;
  uint8_t taintedLoads;

  uint8_t filledIntIssueSlots;
  uint8_t filledFpIssueSlots;
  uint8_t filledMemIssueSlots;
};

struct traceStatsSummary {
  uint64_t totalCycles;
  uint64_t totalCommitted;
  
  uint64_t totalBranches;
  uint64_t totalBranchLatency;
  
  uint64_t totalFilledMSHRs;
  
  uint64_t totalMemAccesses;
  uint64_t totalHitsInCache;
  uint64_t totalMissesInCache;

  uint64_t totalTaintsCalced;
  uint64_t totalYrotsOnCalc;
  uint64_t totalTaintedLoads;

  uint64_t totalFilledIntIssueSlots;
  uint64_t totalFilledFpIssueSlots;
  uint64_t totalFilledMemIssueSlots;
  };

class tracedoctor_shadow : public base_profiler {
private:
  struct traceStatsSummary result;
  struct traceStatsToken lastToken = {};
  uint64_t lastProgressCycle = 0;
  bool stalled = false;
  bool deferred = false;
  std::ofstream stream;
  FILE* failing;
private:
  void print_debug(char const * const data);
  bool check_sample(struct traceStatsSample const &sample);
  void dump_failing_token(char const * const data, int tokens,
                          struct traceStatsToken const &token,
                          struct traceStatsSample const &sample);
  void build_sample(struct traceStatsToken const &trace,
                    struct traceStatsSample &sample);
  void addStats(struct traceStatsSummary &results,
                struct traceStatsSample &sample);
public:
  tracedoctor_shadow(std::vector<std::string> const args, struct traceInfo const info);
  ~tracedoctor_shadow();
  void flushHeader();
  void flushResult();
  bool triggerDetection(struct traceStatsToken const &token);
  void tick(char const * const, unsigned int);
};


#endif
