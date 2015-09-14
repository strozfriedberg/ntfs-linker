#include <scope/testrunner.h>

#include <iostream>

int main(int argc, char** argv) {
  return scope::DefaultRun(std::cout, argc, argv) ? 0: -1;
}
