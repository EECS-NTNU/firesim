
#include "tracedoctor_shadow.h"
#include <sstream>
#include <iomanip>
#include <bitset>
#include <string.h>
#include <assert.h>

constexpr static uint8_t const __ilp_magic[4] = {24, 12, 8, 6};

template <typename T, typename N> inline void value2hist(histogram<T> &hist, T const address, N const value, uint64_t const increment = 1) {
  auto &vec = hist[address];
  if (vec.size() <= value) {
    vec.resize(value + 1);
  }
  vec[value] += increment;
}

inline void hist2file(std::vector<uint64_t> hist) {
  std::ostringstream outbuf;
  outbuf.str("");
  int const maxValue = hist.size()-1;
  for (int value = 0; value < maxValue; value++) {
    if (hist[value] == 0)
      continue;
    outbuf << value << ":" << hist[value] << ";";
  }
  outbuf << maxValue << ":" << hist[maxValue];
  FILE *out = fopen("latency-hist.txt", "w");
  assert(out);
  fprintf(out, "%s%s\n", "latency-hist;", outbuf.str().c_str());
  fclose(out);
}

template <typename T, typename N> inline void hist2file(FILE * const &out, histogram<T> const hist, std::string const prefix = "") {
  std::ostringstream outbuf;
  for (auto &map: hist) {
    outbuf.str("");
    N const maxValue = map.second.size() - 1;
    outbuf << "0x" << std::hex << map.first << ";" << std::dec;
    for (N value = 0; value < maxValue; value++) {
      if (map.second[value] == 0)
        continue;
      outbuf << value << ":" << map.second[value] << "/";
    }
    outbuf << maxValue << ":" << map.second[maxValue];
    fprintf(out, "%s%s\n", prefix.c_str(), outbuf.str().c_str());
  }
}

template <typename T, typename N, unsigned long const norm> inline void hist2file(FILE * const &out, histogram<T> const hist, std::string const prefix = "") {
  std::ostringstream outbuf;
  outbuf.setf(std::ios_base::fixed, std::ios_base::floatfield);
  outbuf << std::setprecision(6);
  for (auto &map: hist) {
    outbuf.str("");
    N const maxValue = map.second.size() - 1;
    outbuf << "0x" << std::hex << map.first << ";" << std::dec;
    for (N value = 0; value < maxValue; value++) {
      if (map.second[value] == 0)
        continue;
      outbuf << value << ":" << ((double) map.second[value] / norm) << "/";
    }
    outbuf << maxValue << ":" << ((double) map.second[maxValue] / norm);
    fprintf(out, "%s%s\n", prefix.c_str(), outbuf.str().c_str());
  }
}

void tracedoctor_shadow::print_debug(char const * const data, struct traceStatsSample const &sample) {
  if (!stream) {
    stream.open("/cluster/home/amundbk/chipyard/sims/firesim/deploy/alveo/trace-debug.txt");
  }
  assert(stream);
  stream.write(data, 64);
  stream.flush();
  if (!failing) {
    failing = fopen("/cluster/home/amundbk/chipyard/sims/firesim/deploy/alveo/sample-debug.txt", "w   ");
  }
  assert(failing);
  fprintf(failing, "%u %u %u %u ; %u %u %u %u ; %u %u %u %u ; %u %u %u %u\n",
    sample.isCommit[0],
    sample.isCommit[1],
    sample.isCommit[2],
    sample.isCommit[3],
    sample.isBranch[0],
    sample.isBranch[1],
    sample.isBranch[2],
    sample.isBranch[3],
    sample.branchLatency[0],
    sample.branchLatency[1],
    sample.branchLatency[2],
    sample.branchLatency[3],
    sample.instLatency[0],
    sample.instLatency[1],
    sample.instLatency[2],
    sample.instLatency[3]
    );

}

bool tracedoctor_shadow::check_sample(struct traceStatsSample const &sample) {
  bool valid = true;
  valid = valid && (sample.memAccesses <= 2);
  valid = valid && (sample.hitsInCache <= 2);
  valid = valid && (sample.missesInCache <= 2);
  valid = valid && (sample.taintsCalced <= 2);
  valid = valid && (sample.yrotsOnCalc <= 2);
  valid = valid && (sample.taintedLoads <= 2);
  return valid;
}

void tracedoctor_shadow::dump_failing_token(char const * const data, int tokens,
                                            struct traceStatsToken const &token,
                                            struct traceStatsSample const &sample) {
  failing = fopen("/cluster/home/amundbk/chipyard/sims/firesim/deploy/alveo/failing_token.txt", "w");
  assert(failing);
  stream.write(data, 64);
  stream.flush();

  fprintf(failing, "tokens: %d\n", tokens);

  fprintf(failing, "%lu %u %u %u %u%u%u %u %u%u%u %u%u%u\n",
    token.branchLatency,

    token.isCommit,
    
    token.isBranch,
    
    token.filledMSHRs,
    
    token.memAccesses,
    token.hitsInCache,
    token.missesInCache,
    
    token.helpDebug,
    
    token.taintsCalced,
    token.yrotsOnCalc,
    token.taintedLoads,
    
    token.filledIntIssueSlots,
    token.filledFpIssueSlots,
    token.filledMemIssueSlots);
  
  fprintf(failing, "%u %u%u%u%u %u%u%u%u %u %u%u%u %u%u%u %u%u%u\n",
    sample.isCommit,
    
    sample.isBranch[0],
    sample.isBranch[1],
    sample.isBranch[2],
    sample.isBranch[3],
    
    sample.branchLatency[0],
    sample.branchLatency[1],
    sample.branchLatency[2],
    sample.branchLatency[3],
    
    sample.filledMSHRs,
    
    sample.memAccesses,
    sample.hitsInCache,
    sample.missesInCache,
    
    sample.taintsCalced,
    sample.yrotsOnCalc,
    sample.taintedLoads,
    
    sample.filledIntIssueSlots,
    sample.filledFpIssueSlots,
    sample.filledMemIssueSlots
    );
    fclose(failing);
}


tracedoctor_shadow::tracedoctor_shadow(std::vector<std::string> const args, struct traceInfo const info)
  : base_profiler("shadow", args, info, 1) {
  flushHeader();
  
}

tracedoctor_shadow::~tracedoctor_shadow() {
  flushResult();
  hist2file(instLatencyHist);
}

void tracedoctor_shadow::flushHeader() {
  fprintf(std::get<freg_descriptor>(fileRegister[0]), "%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s\n",
          "totalCycles",
          "totalCommitted",

          "totalBranches",
          "totalBranchLatency",

          "totalMispredicts",
          "totalFilledMSHRs",

          "totalMemAccesses",
          "totalHitsInCache",
          "totalMissesInCache",
          
          "totalTaintsCalced",
          "totalYrotsOnCalc",
          "totalTaintedLoads",

          "totalFilledIntIssueSlots",
          "totalFilledFpIssueSlots",
          "totalFilledMemIssueSlots",

          "totalStallIFURedirect",
          "totalStallROCCWait",
          "totalStallWaitForEmpty",
          "totalStallRenStall",
          "totalStallIssueFull",
          "totalStallSTQFull",
          "totalStallLSQFull",
          "totalStallROBNotReady"
  );
}


void tracedoctor_shadow::flushResult() {
  fprintf(std::get<freg_descriptor>(fileRegister[0]), "%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu\n",
          result.totalCycles,
          result.totalCommitted,

          result.totalBranches,
          result.totalBranchLatency,

          result.totalMispredicts,
          result.totalFilledMSHRs,
          
          result.totalMemAccesses,
          result.totalHitsInCache,
          result.totalMissesInCache,
          
          result.totalTaintsCalced,
          result.totalYrotsOnCalc,
          result.totalTaintedLoads,

          result.totalFilledIntIssueSlots,
          result.totalFilledFpIssueSlots,
          result.totalFilledMemIssueSlots,

          result.totalStallIFURedirect,
          result.totalStallROCCWait,
          result.totalStallWaitForEmpty,
          result.totalStallRenStall,
          result.totalStallIssueFull,
          result.totalStallSTQFull,
          result.totalStallLSQFull,
          result.totalStallROBNotReady
  );
}

void tracedoctor_shadow::build_sample(struct traceStatsToken const &trace,
                                      struct traceStatsSample &sample) {
  for (int i = 0; i < COREWIDTH; i++) {
    sample.isCommit[i] = !!(trace.isCommit & (0x1 << i));
    sample.isBranch[i] = !!(trace.isBranch & (0x1 << i));

    sample.branchLatency[i] = trace.branchLatency[i];
    sample.instLatency[i] = trace.instLatency[i];
  }

  sample.isMispredict = (bool) trace.branchMispredict;

  sample.filledMSHRs = trace.filledMSHRs;

  sample.memAccesses = trace.memAccesses;
  sample.hitsInCache = trace.hitsInCache;
  sample.missesInCache = trace.missesInCache;
  
  sample.taintsCalced = trace.taintsCalced;
  sample.yrotsOnCalc = trace.yrotsOnCalc;
  sample.taintedLoads = trace.taintedLoads;

  sample.filledIntIssueSlots = trace.filledIntIssueSlots;
  sample.filledFpIssueSlots = trace.filledFpIssueSlots;
  sample.filledMemIssueSlots = trace.filledMemIssueSlots;

  sample.stallIFURedirect = trace.blockingSignals & STALL_IFU_REDIRECT;
  sample.stallROCCWait    = trace.blockingSignals & STALL_ROCC;
  sample.stallWaitForEmpty = trace.blockingSignals & STALL_W8_EMPTY;
  sample.stallRenStall    = trace.blockingSignals & STALL_REN_STALL;
  sample.stallIssueFull   = trace.blockingSignals & STALL_ISSUE_FULL;
  sample.stallSTQFull     = trace.blockingSignals & STALL_STQ_FULL;
  sample.stallLSQFull     = trace.blockingSignals & STALL_LSQ_FULL;
  sample.stallROBNotReady = trace.blockingSignals & STALL_ROB;
}

void tracedoctor_shadow::addStats(struct traceStatsSummary &results, 
                                  struct traceStatsSample &sample) {
  results.totalCycles += 1;

  for (int i = 0; i < COREWIDTH; i++) {
    if (sample.isBranch[i]) {
      results.totalBranches++;
      results.totalBranchLatency += sample.branchLatency[i];
    }

    if (sample.isCommit[i]) {
      results.totalCommitted++;

      int value = sample.instLatency[i];
      if (instLatencyHist.size() <= value) {
        instLatencyHist.resize(value + 1);
      }
      instLatencyHist[value] += 1;
    }
  }

  if (sample.isMispredict) {
    results.totalMispredicts++;
  }

  results.totalFilledMSHRs += sample.filledMSHRs;
  results.totalMemAccesses += sample.memAccesses;
  results.totalHitsInCache += sample.hitsInCache;
  results.totalMissesInCache += sample.missesInCache;

  results.totalTaintsCalced += sample.taintsCalced;
  results.totalYrotsOnCalc += sample.yrotsOnCalc;
  results.totalTaintedLoads += sample.taintedLoads;

  results.totalFilledIntIssueSlots += sample.filledIntIssueSlots;
  results.totalFilledFpIssueSlots += sample.filledFpIssueSlots;
  results.totalFilledMemIssueSlots += sample.filledMemIssueSlots;

  results.totalStallIFURedirect += sample.stallIFURedirect;
  results.totalStallROCCWait += sample.stallROCCWait;
  results.totalStallWaitForEmpty += sample.stallWaitForEmpty;
  results.totalStallRenStall += sample.stallRenStall;
  results.totalStallIssueFull += sample.stallIssueFull;
  results.totalStallSTQFull += sample.stallSTQFull;
  results.totalStallLSQFull += sample.stallLSQFull;
  results.totalStallROBNotReady += sample.stallROBNotReady;
}

bool tracedoctor_shadow::triggerDetection(struct traceStatsToken const &token) {
  if (firstToken) {
    firstToken = false;
    return true;
  }

  return false;
}

void tracedoctor_shadow::tick(char const * const data, unsigned int tokens) {
  struct traceStatsToken const * const trace = (struct traceStatsToken const *) data;

  struct traceStatsSample sample;
  for (unsigned int i = 0; i < tokens; i ++) {
    struct traceStatsToken const &token = trace[i];
    if (triggerDetection(token)) {
      lastToken = token;
      continue;
    }

    build_sample(token, sample);

    if (result.totalCycles % 10000 == 0) {
      print_debug(data, sample);
    }
    //if (!check_sample(sample)) {
    //  dump_failing_token(data, tokens, token, sample);
    //  assert(false);
    //}
    addStats(result, sample);

    lastToken = token;
    }
  }

