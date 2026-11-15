#pragma once
// -D PERF_PROF (perf builds only): LVGL's profiler hooks (lv_conf.h) into a
// per-function tally -- calls, self and total CPU cycles -- on the UI core.
// PerfTest.h prints it per screen. LvProf.cpp holds the tally.
#ifdef __cplusplus
extern "C" {
#endif
void lvprof_begin(const char* name);
void lvprof_end(const char* name);
void lvprof_reset(void);
void lvprof_dump(const char* tag, int top);
void lvprof_end_cleanup(const char** tag);
#ifdef __cplusplus
}
#endif
#define LV_PROFILER_BEGIN          lvprof_begin(__func__)
#define LV_PROFILER_END            lvprof_end(__func__)
#define LV_PROFILER_BEGIN_TAG(tag) lvprof_begin(tag)
#define LV_PROFILER_END_TAG(tag)   lvprof_end(tag)
// A scope timed to its end, early returns included (diagnostic patches of LVGL).
#define LVPROF_SCOPE(tag) lvprof_begin(tag); const char* lvprof_scope_ __attribute__((cleanup(lvprof_end_cleanup))) = tag
