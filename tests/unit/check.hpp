#pragma once

// Minimal test helpers shared by the unit tests.
//
// CHECK() is used instead of assert(): it stays active under NDEBUG (Release
// builds) and records every failure instead of aborting on the first one.

#include <iostream>

namespace test
{
  // inline: one shared counter per test executable
  inline int failures = 0;

  inline void run(const char* name, void (*fn)())
  {
    const int before = failures;
    std::cout << name << "... ";
    fn();
    std::cout << (failures == before ? "PASSED" : "\n  FAILED") << "\n";
  }

  // Use as `return test::summary();` at the end of main()
  inline int summary()
  {
    if (failures)
    {
      std::cout << "\n" << failures << " check(s) failed\n";
      return 1;
    }
    std::cout << "\nAll tests passed!\n";
    return 0;
  }
}

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      ++test::failures;                                                      \
      std::cout << "\n  CHECK failed: " #cond " (" __FILE__ ":"              \
                << __LINE__ << ")";                                          \
    }                                                                        \
  } while (0)
