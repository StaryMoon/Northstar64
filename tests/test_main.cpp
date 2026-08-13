#include "test_support.hpp"

#include <exception>
#include <iostream>

int main() {
  std::size_t passed = 0;
  std::size_t failed = 0;
  for (const auto& test : northstar64::test::registry()) {
    try {
      test.function();
      ++passed;
      std::cout << "[PASS] " << test.name << '\n';
    } catch (const std::exception& error) {
      ++failed;
      std::cerr << "[FAIL] " << test.name << "\n       " << error.what() << '\n';
    } catch (...) {
      ++failed;
      std::cerr << "[FAIL] " << test.name << "\n       non-standard exception\n";
    }
  }
  std::cout << "\n" << passed << " passed, " << failed << " failed\n";
  return failed == 0U ? 0 : 1;
}
