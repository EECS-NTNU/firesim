#ifndef __TRACEDOCTOR_TEA_H
#define __TRACEDOCTOR_TEA_H

#include "tracedoctor_worker.h"

#include <map>
#include <random>
#include <stdexcept>
#include <array>
#include <unordered_map>
#include <limits>

#define ROB_COMMITTING        (0x1 << 0)
#define ROB_POPULATED         (0x1 << 1)
#define ROB_DISPATCHING       (0x1 << 2)
#define ROB_EXCEPTION         (0x1 << 3)

#define INSTR_COMMITS         (0x1 << 0)
#define INSTR_VALID           (0x1 << 1)
#define INSTR_ICACHE_MISS     (0x1 << 2)
#define INSTR_ITLB_SMISS      (0x1 << 3)
#define INSTR_ITLB_PMISS      (0x1 << 4)
#define INSTR_DCACHE_MISS     (0x1 << 5)
#define INSTR_DTLB_SMISS      (0x1 << 6)
#define INSTR_DTLB_PMISS      (0x1 << 7)
#define INSTR_LSU_FULL        (0x1 << 8)
#define INSTR_REFETCHED       (0x1 << 9)
#define INSTR_BR_MISS         (0x1 << 10) // OIR
#define INSTR_FLUSHS          (0x1 << 11) // OIR
#define INSTR_EXCPT           (0x1 << 12) // OIR


#define INSTR_OIR   (INSTR_BR_MISS | INSTR_FLUSHS | INSTR_EXCPT)
#define INSTR_MISS  (INSTR_ICACHE_MISS | INSTR_ITLB_PMISS | INSTR_ITLB_SMISS | INSTR_DCACHE_MISS | INSTR_DTLB_PMISS | INSTR_DTLB_SMISS | INSTR_LSU_FULL | INSTR_REFETCHED | INSTR_BR_MISS | INSTR_FLUSHS | INSTR_EXCPT)

// genSignature produces a bit mask that is compatible so far
#define SIG_OIR INSTR_OIR
#define SIG_MISS INSTR_MISS

struct robAnalysisToken {
  struct {
      uint64_t tsc_cycle : 44;
      uint8_t  rob : 4;
      uint8_t  rob_head : 8;
      uint8_t  rob_tail : 8;
  } state;

  uint16_t instr0_flags;
  uint16_t instr1_flags;
  uint16_t instr2_flags;
  uint16_t instr3_flags;

  uint64_t instr0_address;
  uint64_t instr1_address;
  uint64_t instr2_address;
  uint64_t instr3_address;

  uint16_t instr0_memlat;
  uint16_t instr1_memlat;
  uint16_t instr2_memlat;
  uint16_t instr3_memlat;

  uint16_t instr0_isslat;
  uint16_t instr1_isslat;
  uint16_t instr2_isslat;
  uint16_t instr3_isslat;
};

class base_profiler : public tracedoctor_worker {
protected:
  // Sampling based profiler
  uint64_t samplingPeriod    = 0; // Sampling Period
  uint64_t randomStartOffset = 0; // Random start offset
  uint64_t randomOffset      = 0; // Random sampling offset
  uint64_t lastPeriod        = 0; // Last sampled cycle
  uint64_t nextPeriodStart   = 0; // When the next sampling period starts
  uint64_t nextPeriod        = 0; // when the next sampling cycle will be

  uint64_t lastFlushPeriod   = 0;
  // Every so many target cycles the results are dumped to the file
  uint64_t flushThreshold    = 0;

  uint16_t l2MissLatency = 32;
  uint16_t l3MissLatency = 84;

  std::mt19937 randomGenerator;
  std::uniform_int_distribution<unsigned long> randomRange;
public:
  base_profiler(std::string const name, std::vector<std::string> const args, struct traceInfo const info, int const requiredFiles);
  bool triggerDetection(struct robAnalysisToken const &token);

  inline void restartSampling(uint64_t const &count);
  inline bool reachedSamplingPeriod(uint64_t const &count);
  inline uint64_t advanceSamplingPeriod(uint64_t const &count);

  virtual void flushResult();
  virtual void flushHeader();
  ~base_profiler();
};

template <typename A> using histogram = std::unordered_map<A, std::vector<uint64_t>>;

class tracedoctor_tea_gold : public base_profiler {
private:
  static unsigned int const missBits = 10;
  static unsigned int const oirBits = 3;
  static unsigned int const numSignatures = 1 << (missBits + oirBits + oirBits);

  struct instrInfo {
    uint64_t address;
    uint16_t flags;
    uint16_t signature;
    uint64_t severity;
    uint8_t  ilpLatency;
    bool     oir;
  };

  struct instrInfo lastInstructionRegister = {};

  std::array<histogram<uint64_t>, numSignatures> severityHists;
  histogram<uint64_t> result;

  uint64_t lastProgressCycle = 0;
public:
  tracedoctor_tea_gold(std::vector<std::string> const args, struct traceInfo const info);
  ~tracedoctor_tea_gold();
  inline void attributeOIR(uint64_t const &);
  inline void attribute(uint64_t const &, uint16_t const &, uint16_t const &, uint64_t const &, uint8_t const &);
  void tick(char const * const, unsigned int);
  void flushHeader();
  void flushResult();
};

class tracedoctor_latency_hist : public base_profiler {
private:
  histogram<uint64_t> memoryLatencyHist;
public:
  tracedoctor_latency_hist(std::vector<std::string> const args, struct traceInfo const info);
  ~tracedoctor_latency_hist();
  void tick(char const * const, unsigned int);
  void flushHeader();
  void flushResult();
};

class tracedoctor_oracle : public base_profiler {
private:
  struct flatSample {
    uint64_t tCommit;
    uint64_t tStall;
    uint64_t tDeferred;
    uint64_t tBrMiss;
    uint64_t tFlush;
    uint64_t tExcpt;
    uint64_t tIssueLatency;
    uint64_t tMemoryLatency;
    uint64_t cCommit;
    uint64_t cStall;
    uint64_t cDeferred;
    uint64_t cBrMiss;
    uint64_t cFlush;
    uint64_t cExcpt;
  };
  std::unordered_map<uint64_t, struct flatSample> result;
  struct robAnalysisToken lastToken = {};
  uint64_t lastProgressCycle = 0;
  bool stalled = false;
  bool deferred = false;
public:
  tracedoctor_oracle(std::vector<std::string> const args, struct traceInfo const info);
  ~tracedoctor_oracle();
  void flushHeader();
  void flushResult();
  void tick(char const * const, unsigned int);
};

#define TEA_FLAG_VALID_0   (1 << 0)
#define TEA_FLAG_VALID_1   (1 << 1)
#define TEA_FLAG_VALID_2   (1 << 2)
#define TEA_FLAG_VALID_3   (1 << 3)
#define TEA_FLAG_STALLED   (1 << 4)
#define TEA_FLAG_DEFERRED  (1 << 5)
#define TEA_FLAG_OIR       (1 << 6)

class tracedoctor_tea_sampler : public base_profiler {
private:
  struct instrInfo {
    uint64_t address;
    uint16_t flags;
    uint16_t isslat;
    uint16_t memlat;
    uint16_t prevFlags;
    uint64_t stallLatency;
  };

  struct instrInfo lastInstructionRegister = {};
  uint64_t lastProgressCycle = 0;
  uint64_t samplingCycle = 0;

  enum profiler_state : unsigned int {
     s_off       = 0,
     s_deferred  = 1,
     s_stalled   = 2,
     s_armed     = 3,
  };
  unsigned int state = s_off;
public:
  tracedoctor_tea_sampler(std::vector<std::string> const args, struct traceInfo const info);
  ~tracedoctor_tea_sampler();
  void flushHeader();
  void flushResult();
  void tick(char const * const, unsigned int);
};

class tracedoctor_ibs_sampler : public base_profiler {
private:
  struct instrInfo {
    uint16_t flags;
  };

  unsigned int coreWidth = 4;

  struct instrInfo lastInstructionRegister = {};
  uint64_t lastProgressCycle = 0;
  uint64_t samplingCycle = 0;

  uint8_t tag = 0;

  enum profiler_state : unsigned int {
     s_idle    = 0,
     s_tagging = 1,
     s_armed   = 2,
  };

  unsigned int state = s_idle;
  uint64_t evicted = 0;
public:
  tracedoctor_ibs_sampler(std::vector<std::string> const args, struct traceInfo const info);
  ~tracedoctor_ibs_sampler();
  void flushHeader();
  void flushResult();
  void tick(char const * const, unsigned int);
};


class tracedoctor_pebs_sampler : public base_profiler {
private:
  struct instrInfo {
    uint16_t flags;
  };

  struct instrInfo lastInstructionRegister = {};
  uint64_t lastProgressCycle = 0;
  uint64_t samplingCycle = 0;

  enum profiler_state : unsigned int {
     s_idle    = 0,
     s_armed   = 2,
  };

  unsigned int state = s_idle;
public:
  tracedoctor_pebs_sampler(std::vector<std::string> const args, struct traceInfo const info);
  ~tracedoctor_pebs_sampler();
  void flushHeader();
  void flushResult();
  void tick(char const * const, unsigned int);
};

#endif
