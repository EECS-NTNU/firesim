#ifndef TRACEDOCTOR_REGISTER_H__
#define TRACEDOCTOR_REGISTER_H__

#include "tracedoctor_worker.h"
#include "tracedoctor_example.h"
#include "tracedoctor_tea.h"

#include <string>
#include <memory>
#include <functional>
#include <memory>

#define __REGISTER_TRACEDOCTOR_WORKER(__name, __class, ...)                  \
  {                                                                          \
    __name,                                                                  \
    [](std::vector<std::string> const &args, struct traceInfo const &info) { \
      return std::unique_ptr<tracedoctor_worker>(new __class(__VA_ARGS__));  \
    }                                                                        \
  }

#define REGISTER_TRACEDOCTOR_WORKER(__name, __class) __REGISTER_TRACEDOCTOR_WORKER(__name, __class, args, info)

typedef std::map<std::string, std::function<std::unique_ptr<tracedoctor_worker>(std::vector<std::string> const &, struct traceInfo const &)>> tracedoctor_register_t;

// This is the worker register. Add your entries in this map to register new workers.
static tracedoctor_register_t const tracedoctor_register = {
  REGISTER_TRACEDOCTOR_WORKER("dummy",   tracedoctor_dummy),
  REGISTER_TRACEDOCTOR_WORKER("filer",   tracedoctor_filer),
  REGISTER_TRACEDOCTOR_WORKER("oracle",  tracedoctor_oracle),
  REGISTER_TRACEDOCTOR_WORKER("latency_hist",  tracedoctor_latency_hist),
  REGISTER_TRACEDOCTOR_WORKER("tea_gold",  tracedoctor_tea_gold),
  REGISTER_TRACEDOCTOR_WORKER("tea_sampler",  tracedoctor_tea_sampler),
  REGISTER_TRACEDOCTOR_WORKER("ibs_sampler",  tracedoctor_ibs_sampler),
  REGISTER_TRACEDOCTOR_WORKER("pebs_sampler",  tracedoctor_pebs_sampler),
};

// HINT: if the compiler complains about 'expected primary-expression'
// it most likely couldn't find the tracedoctor worker class you have
// specificed. Make sure you have included the correct header file and
// that the class is spelled correctly.

#endif
