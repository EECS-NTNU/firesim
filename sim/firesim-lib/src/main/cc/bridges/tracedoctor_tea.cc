#include "tracedoctor_tea.h"
#include <sstream>
#include <iomanip>
#include <bitset>
#include <string.h>

constexpr static uint8_t const __ilp_magic[4] = {24, 12, 8, 6};

template <typename T, typename N> inline void value2hist(histogram<T> &hist, T const address, N const value, uint64_t const increment = 1) {
  auto &vec = hist[address];
  if (vec.size() <= value) {
    vec.resize(value + 1);
  }
  vec[value] += increment;
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

base_profiler::base_profiler(std::string const name, std::vector<std::string> const args, struct traceInfo const info, int const requiredFiles)
  : tracedoctor_worker(name, args, info, requiredFiles) {
  if (info.tokenBytes != 512 / 8) {
    throw std::invalid_argument("profiling workers are optimized towards 512 bit trace tokens coming from the DMA interface");
  }

  std::random_device rd;
  randomGenerator = std::mt19937(rd());

  for (auto &a: args) {
    std::vector<std::string> c = strSplit(a, ":");
    if (c[0].compare("flushAfter") == 0 && c.size() > 1) {
      flushThreshold = std::stoul(c[1], nullptr, 0);
    } else if (c[0].compare("samplingPeriod") == 0 && c.size() > 1) {
      samplingPeriod = std::stoul(c[1], nullptr, 0);
    } else if (c[0].compare("randomStartOffset") == 0 && c.size() > 1) {
      randomStartOffset = std::stoul(c[1], nullptr, 0);
    } else if (c[0].compare("randomOffset") == 0 && c.size() > 1) {
      randomOffset = std::stoul(c[1], nullptr, 0);
    } else if (c[0].compare("l2MissLatency") == 0 && c.size() > 1) {
      l2MissLatency = std::stoul(c[1], nullptr, 0);
    } else if (c[0].compare("l3MissLatency") == 0 && c.size() > 1) {
      l3MissLatency = std::stoul(c[1], nullptr, 0);
    }
  }

  if (randomStartOffset > 0) {
    std::uniform_int_distribution<unsigned long> randomRangeStart(0, randomStartOffset);
    randomStartOffset = randomRangeStart(randomGenerator);
  }

  if (samplingPeriod > 0 && randomOffset >= samplingPeriod) {
    fprintf(stdout, "%s: random offset cannot be bigger than the sampling period, reducing to %lu\n", tracerName.c_str(), samplingPeriod - 1);
    randomOffset = samplingPeriod - 1;
  }

  randomRange = std::uniform_int_distribution<unsigned long>(0, randomOffset);

  fprintf(stdout, "%s: ", tracerName.c_str());
  for (auto &a: fileRegister) {
    fprintf(stdout, "file(%s), ", std::get<freg_name>(a).c_str());
  }
  fprintf(stdout, "sampling_period(%lu), random_start(%lu), random_offset(%lu), flush_threshold(%lu)\n", samplingPeriod, randomStartOffset, randomOffset, flushThreshold);

  restartSampling(0);
}

/*
 * triggerDetection
 * - returns true if timing must be restarted
 * - flushes the results when triggered
  */
inline bool base_profiler::triggerDetection(struct robAnalysisToken const &token) {
  bool const flush = (flushThreshold && (token.state.tsc_cycle - lastFlushPeriod >= flushThreshold));

  if (flush) {
    lastFlushPeriod = token.state.tsc_cycle;
    flushResult();
  }

  // When returned true, timing must be restart from that point
  // currently no triggers are supported, so no restart of timing
  //
  // TODO: my latest thoughts....
  // trigger detection should have some support from the tracedoctor interface with the
  // flexibility for individual workers to act on it or not
  return false;
}

inline void base_profiler::restartSampling(uint64_t const &count) {
  lastPeriod = count + randomStartOffset;
  nextPeriodStart   = count + samplingPeriod + randomStartOffset;
  nextPeriod = count + samplingPeriod + randomStartOffset;
}

inline bool base_profiler::reachedSamplingPeriod(uint64_t const &count) {
  return nextPeriod <= count;
}

inline uint64_t base_profiler::advanceSamplingPeriod(uint64_t const &count) {
  uint64_t const passedPeriodCount = (nextPeriodStart < count) ? (count - nextPeriodStart) : 0;
  uint64_t passedCount = 0;

  if (passedPeriodCount >= samplingPeriod) {
    uint64_t const missedPeriods = passedPeriodCount / samplingPeriod;
    nextPeriodStart += missedPeriods * samplingPeriod;
    nextPeriod = nextPeriodStart;
    if (randomOffset) {
      nextPeriod -= randomRange(randomGenerator);
    }
  }

  do {
    passedCount += nextPeriod - lastPeriod;
    lastPeriod = nextPeriod;

    nextPeriodStart += samplingPeriod;
    nextPeriod = nextPeriodStart;
    if (randomOffset) {
      nextPeriod -= randomRange(randomGenerator);
    }
  } while (nextPeriod <= count);

  return passedCount;
}

void base_profiler::flushHeader() {}
void base_profiler::flushResult() {}
base_profiler::~base_profiler() {}

static inline void getFirstCommitting(struct robAnalysisToken const &token, uint64_t &address, uint16_t &flags, uint16_t &isslat, uint16_t &memlat) {
  if (token.instr0_flags & INSTR_COMMITS) {
    address = token.instr0_address;
    flags   = token.instr0_flags;
    isslat  = token.instr0_isslat;
    memlat  = token.instr0_memlat;
  } else if (token.instr1_flags & INSTR_COMMITS) {
    address = token.instr1_address;
    flags   = token.instr1_flags;
    isslat  = token.instr1_isslat;
    memlat  = token.instr1_memlat;
  } else if (token.instr2_flags & INSTR_COMMITS) {
    address = token.instr2_address;
    flags   = token.instr2_flags;
    isslat  = token.instr2_isslat;
    memlat  = token.instr2_memlat;
  } else {
    address = token.instr3_address;
    flags   = token.instr3_flags;
    isslat  = token.instr3_isslat;
    memlat  = token.instr3_memlat;
  }
}

static inline void getFirstCommitting(struct robAnalysisToken const &token, uint64_t &address, uint16_t &flags) {
  if (token.instr0_flags & INSTR_COMMITS) {
    address = token.instr0_address;
    flags   = token.instr0_flags;
  } else if (token.instr1_flags & INSTR_COMMITS) {
    address = token.instr1_address;
    flags   = token.instr1_flags;
  } else if (token.instr2_flags & INSTR_COMMITS) {
    address = token.instr2_address;
    flags   = token.instr2_flags;
  } else {
    address = token.instr3_address;
    flags   = token.instr3_flags;
  }
}

static inline void getFirstCommitting(struct robAnalysisToken const &token, uint64_t &address) {
  if (token.instr0_flags & INSTR_COMMITS)
    address = token.instr0_address;
  else if (token.instr1_flags & INSTR_COMMITS)
    address = token.instr1_address;
  else if (token.instr2_flags & INSTR_COMMITS)
    address = token.instr2_address;
  else
    address = token.instr3_address;
}



static inline void getFirstValid(struct robAnalysisToken const &token, uint64_t &address, uint16_t &flags, uint16_t &isslat, uint16_t &memlat) {
  if (token.instr0_flags & INSTR_VALID) {
    address = token.instr0_address;
    flags   = token.instr0_flags;
    isslat  = token.instr0_isslat;
    memlat  = token.instr0_memlat;
  } else if (token.instr1_flags & INSTR_VALID) {
    address = token.instr1_address;
    flags   = token.instr1_flags;
    isslat  = token.instr1_isslat;
    memlat  = token.instr1_memlat;
  } else if (token.instr2_flags & INSTR_VALID) {
    address = token.instr2_address;
    flags   = token.instr2_flags;
    isslat  = token.instr2_isslat;
    memlat  = token.instr2_memlat;
  } else {
    address = token.instr3_address;
    flags   = token.instr3_flags;
    isslat  = token.instr3_isslat;
    memlat  = token.instr3_memlat;
  }
}

static inline void getFirstValid(struct robAnalysisToken const &token, uint64_t &address, uint16_t &flags) {
  if (token.instr0_flags & INSTR_VALID) {
    address = token.instr0_address;
    flags   = token.instr0_flags;
  } else if (token.instr1_flags & INSTR_VALID) {
    address = token.instr1_address;
    flags   = token.instr1_flags;
  } else if (token.instr2_flags & INSTR_VALID) {
    address = token.instr2_address;
    flags   = token.instr2_flags;
  } else {
    address = token.instr3_address;
    flags   = token.instr3_flags;
  }
}

static inline void getFirstValid(struct robAnalysisToken const &token, uint64_t &address) {
  if (token.instr0_flags & INSTR_VALID)
    address = token.instr0_address;
  else if (token.instr1_flags & INSTR_VALID)
    address = token.instr1_address;
  else if (token.instr2_flags & INSTR_VALID)
    address = token.instr2_address;
  else
    address = token.instr3_address;
}

static inline bool getLastCommitting(struct robAnalysisToken const &token, uint64_t &address, uint16_t &flags, uint16_t &isslat, uint16_t &memlat) {
  if (token.instr3_flags & INSTR_COMMITS) {
    address = token.instr3_address;
    flags   = token.instr3_flags;
    isslat  = token.instr3_isslat;
    memlat  = token.instr3_memlat;
  } else if (token.instr2_flags & INSTR_COMMITS) {
    address = token.instr2_address;
    flags   = token.instr2_flags;
    isslat  = token.instr2_isslat;
    memlat  = token.instr2_memlat;
  } else if (token.instr1_flags & INSTR_COMMITS) {
    address = token.instr1_address;
    flags   = token.instr1_flags;
    isslat  = token.instr1_isslat;
    memlat  = token.instr1_memlat;
  } else {
    flags   = token.instr0_flags;
    address = token.instr0_address;
    isslat  = token.instr0_isslat;
    memlat  = token.instr0_memlat;
  }
  return flags & (INSTR_BR_MISS | INSTR_FLUSHS);
}

static inline bool getLastCommitting(struct robAnalysisToken const &token, uint64_t &address, uint16_t &flags) {
  if (token.instr3_flags & INSTR_COMMITS) {
    address = token.instr3_address;
    flags   = token.instr3_flags;
  } else if (token.instr2_flags & INSTR_COMMITS) {
    address = token.instr2_address;
    flags   = token.instr2_flags;
  } else if (token.instr1_flags & INSTR_COMMITS) {
    address = token.instr1_address;
    flags   = token.instr1_flags;
  } else {
    address = token.instr0_address;
    flags   = token.instr0_flags;
  }
  return flags & (INSTR_BR_MISS | INSTR_FLUSHS);
}


static inline void getLastCommitting(struct robAnalysisToken const &token, uint64_t &address) {
  if (token.instr3_flags & INSTR_COMMITS) {
    address = token.instr3_address;
  } else if (token.instr2_flags & INSTR_COMMITS) {
    address = token.instr2_address;
  } else if (token.instr1_flags & INSTR_COMMITS) {
    address = token.instr1_address;
  } else {
    address = token.instr0_address;
  }
}

static inline uint16_t genSignature(uint16_t const &lastFlags, uint16_t const &flags, uint16_t const &memlat, uint16_t const &l2MissLatency, uint16_t const &l3MissLatency) {
  return
    (flags & INSTR_MISS) |
    ((flags & INSTR_DCACHE_MISS && memlat >= l2MissLatency) ? 0x1 : 0x0) |
    ((flags & INSTR_DCACHE_MISS && memlat >= l3MissLatency) ? 0x2 : 0x0) |
    ((lastFlags & INSTR_OIR) << 3);
}

tracedoctor_oracle::tracedoctor_oracle(std::vector<std::string> const args, struct traceInfo const info)
  : base_profiler("Oracle", args, info, 1) {
  flushHeader();
}

tracedoctor_oracle::~tracedoctor_oracle() {
  flushResult();
}

void tracedoctor_oracle::flushHeader() {
  fprintf(std::get<freg_descriptor>(fileRegister[0]), "%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s\n",
          "pc",
          "tCycles",
          "tCommit",
          "tStall",
          "tDeferred",
          "tBrMiss",
          "tFlush",
          "tExcpt",
          "tIssueLatency",
          "tMemoryLatency",
          "cCommit",
          "cStall",
          "cDeferred",
          "cBrMiss",
          "cFlush",
          "cExcpt"
  );
}


void tracedoctor_oracle::flushResult() {
  for (auto &r: result) {
    double const tCommit = (double) r.second.tCommit / __ilp_magic[0];
    double const tCycles = tCommit +
      r.second.tStall +
      r.second.tDeferred +
      r.second.tBrMiss +
      r.second.tFlush +
      r.second.tExcpt;

    fprintf(std::get<freg_descriptor>(fileRegister[0]), "0x%lx;%f;%f;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu;%lu\n",
            r.first,
            tCycles,
            tCommit,
            r.second.tStall,
            r.second.tDeferred,
            r.second.tBrMiss,
            r.second.tFlush,
            r.second.tExcpt,
            r.second.tIssueLatency,
            r.second.tMemoryLatency,
            r.second.cCommit,
            r.second.cStall,
            r.second.cDeferred,
            r.second.cBrMiss,
            r.second.cFlush,
            r.second.cExcpt
    );
  }
  result.clear();
}

void tracedoctor_oracle::tick(char const * const data, unsigned int tokens) {
  struct robAnalysisToken const * const trace = (struct robAnalysisToken const *) data;
  uint16_t flags;
  uint64_t address;

  for (unsigned int i = 0; i < tokens; i ++) {
    struct robAnalysisToken const &token = trace[i];
    triggerDetection(token);

    // Oracle only needs to parse between committing, populated and exception tokens
    if (token.state.rob & (ROB_POPULATED | ROB_COMMITTING | ROB_EXCEPTION)) {
      uint64_t remainingCycles = token.state.tsc_cycle - lastToken.state.tsc_cycle;

      if (token.state.rob & ROB_POPULATED) {
        bool const lastException = lastToken.state.rob & ROB_EXCEPTION;
        uint64_t const deferredCycles = remainingCycles - 1;

        if (lastException) {
          getFirstValid(lastToken, address);
          result[address].tExcpt += deferredCycles;
        } else if (getLastCommitting(lastToken, address, flags)) {
          struct flatSample &target = result[address];
          bool const br_miss = flags & INSTR_BR_MISS;
          bool const flushs  = flags & INSTR_FLUSHS;
          target.tBrMiss += br_miss * deferredCycles;
          target.tFlush  += flushs  * deferredCycles;
          target.cBrMiss += br_miss;
          target.cFlush  += flushs;
        } else {
          getFirstValid(token, address);
          result[address].tDeferred += deferredCycles;
        }
        remainingCycles = 1;
      }

      bool const thisAttributeToken = token.state.rob & (ROB_COMMITTING | ROB_EXCEPTION);

      if (!thisAttributeToken || remainingCycles > 1) {
        getFirstValid(token, address);
        result[address].tStall += remainingCycles - thisAttributeToken;
      }

      if (thisAttributeToken) {
        if (token.state.rob & ROB_COMMITTING) {
          bool const thisPopulated = token.state.rob & ROB_POPULATED;
          bool const lastOnlyPopulated = (lastToken.state.rob & ROB_POPULATED) && !(lastToken.state.rob & (ROB_COMMITTING | ROB_EXCEPTION));
          bool stalled = !thisPopulated && lastOnlyPopulated;
          bool deferred = thisPopulated || lastOnlyPopulated;

          bool const instr0_commits = token.instr0_flags & INSTR_COMMITS;
          bool const instr1_commits = token.instr1_flags & INSTR_COMMITS;
          bool const instr2_commits = token.instr2_flags & INSTR_COMMITS;
          bool const instr3_commits = token.instr3_flags & INSTR_COMMITS;
          unsigned int const nCommitting = instr0_commits + instr1_commits + instr2_commits + instr3_commits;
          uint64_t const ilpCycles = __ilp_magic[nCommitting - 1];
          if (instr0_commits) {
            struct flatSample &target = result[token.instr0_address];
            target.tCommit        += ilpCycles;
            target.tIssueLatency  += token.instr0_isslat;
            target.tMemoryLatency += token.instr0_memlat;
            target.cCommit        += 1;
            target.cStall         += stalled;
            target.cDeferred      += deferred;
            stalled = false; deferred = false;
          }
          if (instr1_commits) {
            struct flatSample &target = result[token.instr1_address];
            target.tCommit        += ilpCycles;
            target.tIssueLatency  += token.instr1_isslat;
            target.tMemoryLatency += token.instr1_memlat;
            target.cCommit        += 1;
            target.cStall         += stalled;
            target.cDeferred      += deferred;
            stalled = false; deferred = false;
          }
          if (instr2_commits) {
            struct flatSample &target = result[token.instr2_address];
            target.tCommit        += ilpCycles;
            target.tIssueLatency  += token.instr2_isslat;
            target.tMemoryLatency += token.instr2_memlat;
            target.cCommit        += 1;
            target.cStall         += stalled;
            target.cDeferred      += deferred;
            stalled = false; deferred = false;
          }
          if (instr3_commits) {
            struct flatSample &target = result[token.instr3_address];
            target.tCommit        += ilpCycles;
            target.tMemoryLatency += token.instr3_memlat;
            target.tIssueLatency  += token.instr3_isslat;
            target.cCommit        += 1;
            target.cStall         += stalled;
            target.cDeferred      += deferred;
          }
        } else {
          getFirstValid(token, address);
          struct flatSample &target = result[address];
          target.tExcpt += 1;
          target.cExcpt += 1;
        }
      }
      lastToken = token;
    }
  }
}

tracedoctor_tea_gold::tracedoctor_tea_gold(std::vector<std::string> const args, struct traceInfo const info)
  : base_profiler("TEAGold", args, info, 2) {
  fprintf(stdout, "%s: eventBits(%u), l2MissLatency(%u), l3MissLatency(%u)\n", tracerName.c_str(), missBits + oirBits + oirBits, l2MissLatency, l3MissLatency);
  flushHeader();
};

tracedoctor_tea_gold::~tracedoctor_tea_gold() {
  attributeOIR(0);
  flushResult();
}

void tracedoctor_tea_gold::flushHeader() {
  fprintf(std::get<freg_descriptor>(fileRegister[0]), "signature;address;latencies\n");
  fprintf(std::get<freg_descriptor>(fileRegister[1]), "address;signatures\n");
}

void tracedoctor_tea_gold::flushResult() {
  for (uint64_t i = 0; i < severityHists.size(); i++) {
    hist2file<uint64_t, uint64_t>(std::get<freg_descriptor>(fileRegister[0]), severityHists[i], std::to_string(i) + ";");
    severityHists[i].clear();
  }

  hist2file<uint64_t, uint64_t, __ilp_magic[0]>(std::get<freg_descriptor>(fileRegister[1]), result);
}


inline void tracedoctor_tea_gold::attributeOIR(uint64_t const &additionalSeverity) {
  if (lastInstructionRegister.oir) {
    value2hist<uint64_t, uint64_t>(severityHists[lastInstructionRegister.signature], lastInstructionRegister.address, lastInstructionRegister.severity + additionalSeverity);
    value2hist<uint64_t, uint64_t>(result, lastInstructionRegister.address, lastInstructionRegister.signature, lastInstructionRegister.ilpLatency + ((lastInstructionRegister.severity + additionalSeverity) * __ilp_magic[0]));

    // Attribution done, its not an OIR any more
    lastInstructionRegister.oir = false;
  }
}

inline void tracedoctor_tea_gold::attribute(uint64_t const &address, uint16_t const &flags, uint16_t const &memlat, uint64_t const &severity, uint8_t const &ilpLatency) {
  uint16_t const signature = genSignature(lastInstructionRegister.flags, flags, memlat, l2MissLatency, l3MissLatency);

  if ((flags & INSTR_OIR) == 0) {
    value2hist<uint64_t, uint64_t>(severityHists[signature], address, severity);
    value2hist<uint64_t, uint64_t>(result, address, signature, ilpLatency + (severity * __ilp_magic[0]));
  } else {
    // We need to defer attribution to figure out full severity
    lastInstructionRegister.address = address;
    lastInstructionRegister.signature = signature;
    lastInstructionRegister.severity = severity;
    lastInstructionRegister.ilpLatency = ilpLatency;
    lastInstructionRegister.oir = true;
  }

  lastInstructionRegister.flags = flags;
}

void tracedoctor_tea_gold::tick(char const * const data, unsigned int tokens) {
  struct robAnalysisToken const * const trace = (struct robAnalysisToken const *) data;
  for (unsigned int i = 0; i < tokens; i ++) {
    struct robAnalysisToken const &token = trace[i];
    triggerDetection(token);


    if ((token.state.rob & ROB_POPULATED) && lastInstructionRegister.oir) {
      // In case we have an unattributed OIR instruction
      // Add all cycles but this one to the severity of the OIR instruction
      attributeOIR(token.state.tsc_cycle - lastProgressCycle - 1);

      // We have now accounted for all cycles but this one
      lastProgressCycle = token.state.tsc_cycle - 1;
    }

    if (token.state.rob & (ROB_COMMITTING | ROB_EXCEPTION)) {
      uint64_t severity = token.state.tsc_cycle - lastProgressCycle - 1;

      // In case we still have a pending OIR attribution do it here
      attributeOIR(0);

      if (token.state.rob & ROB_COMMITTING) {
        bool const instr0_commits = token.instr0_flags & INSTR_COMMITS;
        bool const instr1_commits = token.instr1_flags & INSTR_COMMITS;
        bool const instr2_commits = token.instr2_flags & INSTR_COMMITS;
        bool const instr3_commits = token.instr3_flags & INSTR_COMMITS;

        uint8_t const ilpLatency = __ilp_magic[instr0_commits + instr1_commits + instr2_commits + instr3_commits - 1];

        if (token.instr0_flags & INSTR_COMMITS) {
          attribute(token.instr0_address, token.instr0_flags, token.instr0_memlat, severity, ilpLatency);
          severity = 0;
        }
        if (token.instr1_flags & INSTR_COMMITS) {
          attribute(token.instr1_address, token.instr1_flags, token.instr1_memlat, severity, ilpLatency);
          severity = 0;
        }
        if (token.instr2_flags & INSTR_COMMITS) {
          attribute(token.instr2_address, token.instr2_flags, token.instr2_memlat, severity, ilpLatency);
          severity = 0;
        }
        if (token.instr3_flags & INSTR_COMMITS) {
          attribute(token.instr3_address, token.instr3_flags, token.instr3_memlat, severity, ilpLatency);
        }
      } else {
        uint64_t address;
        getFirstValid(token, address);
        attribute(address, INSTR_EXCPT, 0, severity, __ilp_magic[0]);
      }
      lastProgressCycle = token.state.tsc_cycle;
    }

  }
}

tracedoctor_latency_hist::tracedoctor_latency_hist(std::vector<std::string> const args, struct traceInfo const info)
  : base_profiler("LatencyHist", args, info, 1) {
  flushHeader();
};

tracedoctor_latency_hist::~tracedoctor_latency_hist() {
  flushResult();
}

void tracedoctor_latency_hist::flushHeader() {
  fprintf(std::get<freg_descriptor>(fileRegister[0]), "address;latencies\n");
}

void tracedoctor_latency_hist::flushResult() {
  hist2file<uint64_t, uint16_t>(std::get<freg_descriptor>(fileRegister[0]), memoryLatencyHist);
}

void tracedoctor_latency_hist::tick(char const * const data, unsigned int tokens) {
  struct robAnalysisToken const * const trace = (struct robAnalysisToken const *) data;
  for (unsigned int i = 0; i < tokens; i ++) {
    struct robAnalysisToken const &token = trace[i];
    if (token.state.rob & ROB_COMMITTING) {
      if (token.instr0_flags & INSTR_COMMITS) {
        value2hist<uint64_t, uint16_t>(memoryLatencyHist, token.instr0_address, token.instr0_memlat);
      }
      if (token.instr1_flags & INSTR_COMMITS) {
        value2hist<uint64_t, uint16_t>(memoryLatencyHist, token.instr1_address, token.instr1_memlat);
      }
      if (token.instr2_flags & INSTR_COMMITS) {
        value2hist<uint64_t, uint16_t>(memoryLatencyHist, token.instr2_address, token.instr2_memlat);
      }
      if (token.instr3_flags & INSTR_COMMITS) {
        value2hist<uint64_t, uint16_t>(memoryLatencyHist, token.instr3_address, token.instr3_memlat);
      }
    }
  }
}

tracedoctor_tea_sampler::tracedoctor_tea_sampler(std::vector<std::string> const args, struct traceInfo const info)
  : base_profiler("TEASampler", args, info, 1) {
  if (samplingPeriod == 0) {
    throw std::invalid_argument("sampling period missing or too low");
  }

  flushHeader();
};

tracedoctor_tea_sampler::~tracedoctor_tea_sampler() {
  flushResult();
}

void tracedoctor_tea_sampler::flushHeader() {
  fprintf(std::get<freg_descriptor>(fileRegister[0]), "%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s\n",
          "cycle",
          "stallLatency",
          "teaflags",
          "address0",
          "isslat0",
          "memlat0",
          "signature0",
          "address1",
          "isslat1",
          "memlat1",
          "signature1",
          "address2",
          "isslat2",
          "memlat2",
          "signature2",
          "address3",
          "isslat3",
          "memlat3",
          "signature3"
          );
}


void tracedoctor_tea_sampler::flushResult() {}

void tracedoctor_tea_sampler::tick(char const * const data, unsigned int tokens) {
  struct robAnalysisToken const * const trace = (struct robAnalysisToken const *) data;
  for (unsigned int i = 0; i < tokens; i ++) {
    struct robAnalysisToken const &token = trace[i];

    // TEA sampler algorithm only designed to work between commits, exceptions and popluations
    if (token.state.rob & (ROB_COMMITTING | ROB_EXCEPTION | ROB_POPULATED)) {

      if ((token.state.rob & ROB_POPULATED) && (lastInstructionRegister.flags & INSTR_OIR)) {
        lastProgressCycle = token.state.tsc_cycle - 1;
      }

      if (reachedSamplingPeriod(token.state.tsc_cycle)) {
        bool const exactHit = nextPeriod == token.state.tsc_cycle;
        bool const thisPopulated = token.state.rob & ROB_POPULATED;
        bool const thisOnlyPopulated = thisPopulated && !(token.state.rob & (ROB_COMMITTING | ROB_EXCEPTION));

        samplingCycle = nextPeriod;
        advanceSamplingPeriod(token.state.tsc_cycle);

        state = s_armed;

        if (!exactHit && thisPopulated) {
          // Should have sampled earlier but ROB was empty
          if (lastInstructionRegister.flags & INSTR_OIR) {
            // Previous instruction is an offending instruction, output it and done for this period
            uint16_t const teaflags = TEA_FLAG_VALID_0 | TEA_FLAG_OIR;
            uint16_t const signature = genSignature(lastInstructionRegister.prevFlags, lastInstructionRegister.flags, lastInstructionRegister.memlat, l2MissLatency, l3MissLatency);

            fprintf(std::get<freg_descriptor>(fileRegister[0]), "%lu;%lu;%u;0x%lx;%u;%u;%u;0x0;0;0;0;0x0;0;0;0;0x0;0;0;0\n",
                    samplingCycle, lastInstructionRegister.stallLatency, teaflags,
                    lastInstructionRegister.address,
                    lastInstructionRegister.isslat,
                    lastInstructionRegister.memlat,
                    signature
                    );

            state = s_off;
          } else {
            // No offending instruction, go to deferred state
            state = s_deferred;
          }
        } else if (!exactHit || thisOnlyPopulated) {
          // We sample a stalling instruction
          state = s_stalled;
        }
      }

      if (state && (token.state.rob & (ROB_COMMITTING | ROB_EXCEPTION))) {
        uint64_t const stallLatency = token.state.tsc_cycle - lastProgressCycle - 1;
        uint16_t teaflags       = ((state == s_stalled)  ? TEA_FLAG_STALLED : 0) | ((state == s_deferred) ? TEA_FLAG_DEFERRED : 0);
        uint64_t addresses[4]  = {};
        uint16_t isslats[4]    = {};
        uint16_t memlats[4]    = {};
        uint16_t signatures[4] = {};

        if (token.state.rob & ROB_EXCEPTION) {
          // If we have a excepting instruction, we only sample this one as an exception
          getFirstValid(token, addresses[0]);
          signatures[0] = genSignature(lastInstructionRegister.flags, INSTR_EXCPT, 0, l2MissLatency, l3MissLatency);
          teaflags      = TEA_FLAG_VALID_0;
        } else {
          uint16_t flags = lastInstructionRegister.flags;
          unsigned int index = 0;
          if (token.instr0_flags & INSTR_COMMITS) {
            addresses[index]   = token.instr0_address;
            isslats[index]     = token.instr0_isslat;
            memlats[index]     = token.instr0_memlat;
            signatures[index]  = genSignature(flags, token.instr0_flags, token.instr0_memlat, l2MissLatency, l3MissLatency);
            teaflags |= (1 << index);
            flags = token.instr0_flags; index++;
          }
          if (token.instr1_flags & INSTR_COMMITS) {
            addresses[index]   = token.instr1_address;
            isslats[index]     = token.instr1_isslat;
            memlats[index]     = token.instr1_memlat;
            signatures[index]  = genSignature(flags, token.instr1_flags, token.instr1_memlat, l2MissLatency, l3MissLatency);
            teaflags          |= (1 << index);
            flags = token.instr1_flags; index++;
          }
          if (token.instr2_flags & INSTR_COMMITS) {
            addresses[index]   = token.instr2_address;
            isslats[index]     = token.instr2_isslat;
            memlats[index]     = token.instr2_memlat;
            signatures[index]  = genSignature(flags, token.instr2_flags, token.instr2_memlat, l2MissLatency, l3MissLatency);
            teaflags          |= (1 << index);
            flags = token.instr2_flags; index++;
          }
          if (token.instr3_flags & INSTR_COMMITS) {
            addresses[index]   = token.instr3_address;
            isslats[index]     = token.instr3_isslat;
            memlats[index]     = token.instr3_memlat;
            signatures[index]  = genSignature(flags, token.instr3_flags, token.instr3_memlat, l2MissLatency, l3MissLatency);
            teaflags          |= (1 << index);
          }
        }
        fprintf(std::get<freg_descriptor>(fileRegister[0]), "%lu;%lu;%u;0x%lx;%u;%u;%u;0x%lx;%u;%u;%u;0x%lx;%u;%u;%u;0x%lx;%u;%u;%u\n",
                samplingCycle, stallLatency, teaflags,
                addresses[0], isslats[0], memlats[0], signatures[0],
                addresses[1], isslats[1], memlats[1], signatures[1],
                addresses[2], isslats[2], memlats[2], signatures[2],
                addresses[3], isslats[3], memlats[3], signatures[3]
        );
        state = s_off;
      }

      if (token.state.rob & (ROB_EXCEPTION | ROB_COMMITTING)) {
        uint64_t stallLatency = token.state.tsc_cycle - lastProgressCycle - 1;
        if (token.state.rob & ROB_EXCEPTION) {
          uint64_t address;
          getFirstValid(token, address);
          lastInstructionRegister.address   = address;
          lastInstructionRegister.prevFlags = lastInstructionRegister.flags;
          lastInstructionRegister.flags     = INSTR_VALID | INSTR_EXCPT;
          lastInstructionRegister.isslat    = 0;
          lastInstructionRegister.memlat    = 0;
          lastInstructionRegister.stallLatency = stallLatency;
        } else {
          if (token.instr0_flags & INSTR_COMMITS) {
            lastInstructionRegister.prevFlags = lastInstructionRegister.flags;
            lastInstructionRegister.address   = token.instr0_address;
            lastInstructionRegister.flags     = token.instr0_flags;
            lastInstructionRegister.isslat    = token.instr0_isslat;
            lastInstructionRegister.memlat    = token.instr0_memlat;
            lastInstructionRegister.stallLatency = stallLatency;
            stallLatency = 0;
          }
          if (token.instr1_flags & INSTR_COMMITS) {
            lastInstructionRegister.prevFlags = lastInstructionRegister.flags;
            lastInstructionRegister.address   = token.instr1_address;
            lastInstructionRegister.flags     = token.instr1_flags;
            lastInstructionRegister.isslat    = token.instr1_isslat;
            lastInstructionRegister.memlat    = token.instr1_memlat;
            lastInstructionRegister.stallLatency = stallLatency;
            stallLatency = 0;
          }
          if (token.instr2_flags & INSTR_COMMITS) {
            lastInstructionRegister.prevFlags = lastInstructionRegister.flags;
            lastInstructionRegister.address   = token.instr2_address;
            lastInstructionRegister.flags     = token.instr2_flags;
            lastInstructionRegister.isslat    = token.instr2_isslat;
            lastInstructionRegister.memlat    = token.instr2_memlat;
            lastInstructionRegister.stallLatency = stallLatency;
            stallLatency = 0;
          }
          if (token.instr3_flags & INSTR_COMMITS) {
            lastInstructionRegister.prevFlags = lastInstructionRegister.flags;
            lastInstructionRegister.address   = token.instr3_address;
            lastInstructionRegister.flags     = token.instr3_flags;
            lastInstructionRegister.isslat    = token.instr3_isslat;
            lastInstructionRegister.memlat    = token.instr3_memlat;
            lastInstructionRegister.stallLatency = stallLatency;
          }
        }
        lastProgressCycle = token.state.tsc_cycle;
      }
    }
  }
}

tracedoctor_ibs_sampler::tracedoctor_ibs_sampler(std::vector<std::string> const args, struct traceInfo const info)
  : base_profiler("IBSSampler", args, info, 1) {
  if (samplingPeriod == 0) {
    throw std::invalid_argument("sampling period missing or too low");
  }

  for (auto &a: args) {
    std::vector<std::string> c = strSplit(a, ":");
    if (c[0].compare("coreWidth") == 0 && c.size() > 1) {
      coreWidth = std::stoul(c[1], nullptr, 0);
    }
  }

  fprintf(stderr, "%s: coreWidth(%u)\n", tracerName.c_str(), coreWidth);
  flushHeader();
};

tracedoctor_ibs_sampler::~tracedoctor_ibs_sampler() {
  flushResult();
  fprintf(stderr, "%s: evicted(%ld)\n", tracerName.c_str(), evicted);
}

void tracedoctor_ibs_sampler::flushHeader() {
  fprintf(std::get<freg_descriptor>(fileRegister[0]), "%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s\n",
          "cycle",
          "stallLatency",
          "teaflags",
          "address0",
          "isslat0",
          "memlat0",
          "signature0",
          "address1",
          "isslat1",
          "memlat1",
          "signature1",
          "address2",
          "isslat2",
          "memlat2",
          "signature2",
          "address3",
          "isslat3",
          "memlat3",
          "signature3"
          );
}


void tracedoctor_ibs_sampler::flushResult() {}

void tracedoctor_ibs_sampler::tick(char const * const data, unsigned int tokens) {
  struct robAnalysisToken const * const trace = (struct robAnalysisToken const *) data;
  for (unsigned int i = 0; i < tokens; i ++) {
    struct robAnalysisToken const &token = trace[i];


    if ((token.state.rob & ROB_POPULATED) && (lastInstructionRegister.flags & INSTR_OIR)) {
      lastProgressCycle = token.state.tsc_cycle - 1;
    }

    if (reachedSamplingPeriod(token.state.tsc_cycle)) {
        samplingCycle = nextPeriod;
        advanceSamplingPeriod(token.state.tsc_cycle);

        state = s_tagging;
    }

    // Check for eviction and sample before we check for the next sampling period
    if (state == s_armed) {
      uint8_t const tail = token.state.rob_tail;
      // Align the head to the ROB row of the BOOM (least significant bits are inaccurate)
      uint16_t const head = token.state.rob_head - (token.state.rob_head % coreWidth);

      bool const instr_valid = (token.instr0_flags | token.instr1_flags | token.instr2_flags | token.instr3_flags) & INSTR_VALID;
      // If tail is behind head, and tag behind tail, tail has moved back, instruction is evicted
      bool const evict_1 = tail > head && tag >= tail;
      // If tail is in front of head, and tag behind head, tail has wrapped back around, instruction is evicted
      bool const evict_2 = tail > head && tag < head;
      // If tail and tag is behind the head and tail is behind tag too, tail has moved back, instruction is evicted
      bool const evict_3 = tail < head && tag >= tail && tag < head;
      // If tail is at head and the head is not valid, ROB is empty, instruction is evicted
      bool const evict_4 = tail == head && !instr_valid;

      if (evict_1 || evict_2 || evict_3 || evict_4) {
        evicted++;
        state = s_idle;
      } else if ((token.state.rob & ROB_COMMITTING) && tag >= head && tag < (head + coreWidth)) {
        uint64_t const stallLatency = token.state.tsc_cycle - lastProgressCycle - 1;
        uint16_t const teaflags = TEA_FLAG_VALID_0;
        uint64_t address; uint16_t isslat; uint16_t memlat; uint16_t flags;

        getFirstCommitting(token, address, flags, isslat, memlat);

        uint16_t const signature = genSignature(lastInstructionRegister.flags, flags, memlat, l2MissLatency, l3MissLatency);

        fprintf(std::get<freg_descriptor>(fileRegister[0]), "%lu;%lu;%u;0x%lx;%u;%u;%u;0x0;0;0;0;0x0;0;0;0;0x0;0;0;0\n",
                samplingCycle, stallLatency, teaflags,
                address, isslat, memlat, signature
        );

        state = s_idle;
      }
    }

    if (reachedSamplingPeriod(token.state.tsc_cycle)) {
        samplingCycle = nextPeriod;
        advanceSamplingPeriod(token.state.tsc_cycle);
        state = s_tagging;
    }

    if ((state == s_tagging) && (token.state.rob & ROB_DISPATCHING)) {
      tag = token.state.rob_tail;
      state = s_armed;
    }


    if (token.state.rob & (ROB_EXCEPTION | ROB_COMMITTING)) {
      if (token.state.rob & ROB_EXCEPTION) {
        lastInstructionRegister.flags     = INSTR_VALID | INSTR_EXCPT;
      } else {
        if (token.instr3_flags & INSTR_COMMITS) {
          lastInstructionRegister.flags     = token.instr3_flags;
        } else if (token.instr2_flags & INSTR_COMMITS) {
          lastInstructionRegister.flags     = token.instr2_flags;
        } else if (token.instr1_flags & INSTR_COMMITS) {
          lastInstructionRegister.flags     = token.instr1_flags;
        } else if (token.instr0_flags & INSTR_COMMITS) {
          lastInstructionRegister.flags     = token.instr0_flags;
        }
      }
      lastProgressCycle = token.state.tsc_cycle;
    }


  }
}

tracedoctor_pebs_sampler::tracedoctor_pebs_sampler(std::vector<std::string> const args, struct traceInfo const info)
  : base_profiler("PEBSSampler", args, info, 1) {
  if (samplingPeriod == 0) {
    throw std::invalid_argument("sampling period missing or too low");
  }
  flushHeader();
};

tracedoctor_pebs_sampler::~tracedoctor_pebs_sampler() {
  flushResult();
}

void tracedoctor_pebs_sampler::flushHeader() {
  fprintf(std::get<freg_descriptor>(fileRegister[0]), "%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s\n",
          "cycle",
          "stallLatency",
          "teaflags",
          "address0",
          "isslat0",
          "memlat0",
          "signature0",
          "address1",
          "isslat1",
          "memlat1",
          "signature1",
          "address2",
          "isslat2",
          "memlat2",
          "signature2",
          "address3",
          "isslat3",
          "memlat3",
          "signature3"
          );
}


void tracedoctor_pebs_sampler::flushResult() {}

void tracedoctor_pebs_sampler::tick(char const * const data, unsigned int tokens) {
  struct robAnalysisToken const * const trace = (struct robAnalysisToken const *) data;
  for (unsigned int i = 0; i < tokens; i ++) {
    struct robAnalysisToken const &token = trace[i];

    if ((token.state.rob & ROB_POPULATED) && (lastInstructionRegister.flags & INSTR_OIR)) {
      lastProgressCycle = token.state.tsc_cycle - 1;
    }

    if (reachedSamplingPeriod(token.state.tsc_cycle)) {
        samplingCycle = nextPeriod;
        advanceSamplingPeriod(token.state.tsc_cycle);

        state = s_armed;
    }

    if ((state == s_armed) && token.state.rob & ROB_COMMITTING) {
      uint64_t const stallLatency = token.state.tsc_cycle - lastProgressCycle - 1;
      uint16_t const teaflags = TEA_FLAG_VALID_0;
      uint64_t address; uint16_t isslat; uint16_t memlat; uint16_t flags;

      getFirstCommitting(token, address, flags, isslat, memlat);

      uint16_t const signature = genSignature(lastInstructionRegister.flags, flags, memlat, l2MissLatency, l3MissLatency);

      fprintf(std::get<freg_descriptor>(fileRegister[0]), "%lu;%lu;%u;0x%lx;%u;%u;%u;0x0;0;0;0;0x0;0;0;0;0x0;0;0;0\n",
              samplingCycle, stallLatency, teaflags,
              address, isslat, memlat, signature
              );

      state = s_idle;

    }

    if (token.state.rob & (ROB_EXCEPTION | ROB_COMMITTING)) {
      if (token.state.rob & ROB_EXCEPTION) {
        lastInstructionRegister.flags     = INSTR_VALID | INSTR_EXCPT;
      } else {
        if (token.instr3_flags & INSTR_COMMITS) {
          lastInstructionRegister.flags     = token.instr3_flags;
        } else if (token.instr2_flags & INSTR_COMMITS) {
          lastInstructionRegister.flags     = token.instr2_flags;
        } else if (token.instr1_flags & INSTR_COMMITS) {
          lastInstructionRegister.flags     = token.instr1_flags;
        } else if (token.instr0_flags & INSTR_COMMITS) {
          lastInstructionRegister.flags     = token.instr0_flags;
        }
      }
      lastProgressCycle = token.state.tsc_cycle;
    }
  }
}

