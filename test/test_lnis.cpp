#include <scope/test.h>

#include <iostream>
#include <vector>
#include <list>

#include "aggregate.h"

std::ostream& operator<<(std::ostream& out, std::list<int> list) {
  out << "[";
  bool first = true;
  for(auto x: list) {
    if (!first)
      out << ", ";
    first = false;
    out << x;
  }
  out << "]";
  return out;
}

SCOPE_TEST(testLNIS) {
  std::vector<std::vector<int>> sequences { {10, 9, 9, 8, 2, 1, 8, 7, 6, 5},
                               {8, 5, 9, 2, 1, 4, 3, 6, 2} };
  std::vector<std::list<int>> expected { { 0, 1, 2, 3, 6, 7, 8, 9},
                              { 0, 1, 5, 6, 8} };
  for (unsigned int j = 0; j < sequences.size(); j++) {
    std::vector<int> elements(sequences[j]);
    std::vector<int> indices;
    for(unsigned int i = 0; i < elements.size(); ++i)
      indices.push_back(i);

    std::list<int> lnis = computeLNIS(elements, indices);
    SCOPE_ASSERT_EQUAL(expected[j], lnis);
  }
}
