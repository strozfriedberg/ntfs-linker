#include "aggregate.h"
#include <iostream>
#include <vector>
#include <list>

int main() {
  //std::vector<int> elements {10, 9, 9, 8, 2, 1, 8, 7, 6, 5};
  std::vector<int> elements {8, 5, 9, 2, 1, 4, 3, 6, 2};
  std::vector<int> indices;
  for(unsigned int i = 0; i < elements.size(); ++i)
    indices.push_back(i);

  std::list<int> lnis = computeLNIS(elements, indices);
  for(auto x: lnis)
    std::cout << elements[x] << " ";
  std::cout << std::endl;
}
