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

#define COREWIDTH 4

#define STALL_IFU_REDIRECT  (0x1 << 0)
#define STALL_ROCC          (0x1 << 1)
#define STALL_W8_EMPTY      (0x1 << 2)
#define STALL_REN_STALL     (0x1 << 3)
#define STALL_ISSUE_FULL    (0x1 << 4)
#define STALL_STQ_FULL      (0x1 << 5)
#define STALL_LSQ_FULL      (0x1 << 6)
#define STALL_ROB           (0x1 << 7)

struct __attribute__((__packed__)) traceStatsToken {
  uint16_t branchLatency[COREWIDTH];
  uint16_t instLatency[COREWIDTH];
  
  uint8_t isCommit;
  uint8_t isBranch;
  uint8_t branchMispredict;

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
  uint8_t blockingSignals;

  uint8_t pad1;

  uint64_t pad2;
  uint64_t pad3;
  uint64_t pad4;
  uint64_t pad5;
};

struct traceStatsSample {  
  bool isBranch[COREWIDTH];
  uint16_t branchLatency[COREWIDTH];

  bool isCommit[COREWIDTH];
  uint16_t instLatency[COREWIDTH];

  uint8_t isMispredict;
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

  bool stallIFURedirect;
  bool stallROCCWait;
  bool stallWaitForEmpty;
  bool stallRenStall;
  bool stallIssueFull;
  bool stallSTQFull;
  bool stallLSQFull;
  bool stallROBNotReady;
};

struct traceStatsSummary {
  uint64_t totalCycles;
  uint64_t totalCommitted;
  
  uint64_t totalBranches;
  uint64_t totalBranchLatency;
  
  uint64_t totalMispredicts;
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

  uint64_t totalStallIFURedirect;
  uint64_t totalStallROCCWait;
  uint64_t totalStallWaitForEmpty;
  uint64_t totalStallRenStall;
  uint64_t totalStallIssueFull;
  uint64_t totalStallSTQFull;
  uint64_t totalStallLSQFull;
  uint64_t totalStallROBNotReady;
  };

class tracedoctor_shadow : public base_profiler {
private:
  struct traceStatsSummary result = {};
  struct traceStatsToken lastToken = {};
  std::vector<uint64_t> instLatencyHist;
  uint64_t lastProgressCycle = 0;
  bool stalled = false;
  bool deferred = false;
  std::ofstream stream;
  FILE* failing;
private:
  void print_debug(char const * const data, struct traceStatsSample const &sample);
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
