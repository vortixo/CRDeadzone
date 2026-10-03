#pragma once

// Minimal test framework. Passes stay silent; every failure prints file:line
// and the failed expression, and each suite reports its check count. No
// dependencies, no fixtures, no hidden state.

#include <cmath>
#include <cstdio>

namespace crtest {

struct Totals {
  int checks = 0;
  int failed = 0;
};

inline Totals& TotalsRef() {
  static Totals t;
  return t;
}

inline void Record(bool ok, const char* expr, const char* file, int line) {
  ++TotalsRef().checks;
  if (!ok) {
    ++TotalsRef().failed;
    std::printf("FAIL %s:%d: %s\n", file, line, expr);
  }
}

template <typename F>
void Suite(const char* name, F&& fn) {
  const int checksBefore = TotalsRef().checks;
  const int failedBefore = TotalsRef().failed;
  fn();
  const int checks = TotalsRef().checks - checksBefore;
  const int failed = TotalsRef().failed - failedBefore;
  std::printf("%s %s (%d checks)\n", failed == 0 ? "ok" : "FAIL", name, checks);
}

}  // namespace crtest

#define EXPECT_TRUE(cond) \
  ::crtest::Record((cond), "EXPECT_TRUE(" #cond ")", __FILE__, __LINE__)

#define EXPECT_EQ(a, b) \
  ::crtest::Record(((a) == (b)), "EXPECT_EQ(" #a ", " #b ")", __FILE__, __LINE__)

#define EXPECT_NEAR(a, b, eps)                                                   \
  ::crtest::Record((std::fabs((a) - (b)) <= (eps)), "EXPECT_NEAR(" #a ", " #b ")", \
                   __FILE__, __LINE__)
