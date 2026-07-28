#include <iostream>

#include "math_utils.h"

int main() {
  const auto total = archivist_test::add(20, 22);
  const auto message = archivist_test::makeGreeting("Archivist");

  std::cout << message << " Result: " << total << '\n';
  return 0;
}
