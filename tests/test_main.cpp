// Test runner: one translation unit per area, all reporting through
// tests/Test.h. Suites print one line each; failures print file:line.

#include <cstdio>

#include "Test.h"

void DeadzoneMathTests();
void ConfigTests();
void HidTests();
void SupportTests();
void TunableTests();

int main() {
  DeadzoneMathTests();
  ConfigTests();
  HidTests();
  SupportTests();
  TunableTests();
  const auto& t = crtest::TotalsRef();
  if (t.failed == 0) {
    std::printf("ALL %d CHECKS PASSED\n", t.checks);
    return 0;
  }
  std::printf("%d OF %d CHECKS FAILED\n", t.failed, t.checks);
  return 1;
}
