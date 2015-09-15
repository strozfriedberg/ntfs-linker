#include "progress.h"

#include <iomanip>
#include <iostream>
#include <sstream>

void ProgressBar::addToDo(uint64_t x) {
  toDo += x;
}

void ProgressBar::addDone(uint64_t x) {
  done += x;
  printProgress();
}

void ProgressBar::setDone(uint64_t x) {
  done = x;
  printProgress();
}

void ProgressBar::printProgress() {
  long double percent = (long double) done / toDo;
  const int width = 50;
  if((int) (width * percent) > last) {
    std::stringstream ss;
    ss << "\r[";
    int i;
    for(i = 0; i < (int) (width * percent); i++)
      ss << "=";
    for(; i < width; i++)
      ss << " ";
    ss << "] ";
    ss << (int) (percent * 100);
    ss << "%";
    std::cout << ss.str();
    std::cout.flush();
    last = (int) (width * percent);
  }
}

ProgressBar::ProgressBar(uint64_t x) {
  toDo = x;
  done = 0;
  last = -1;
  printProgress();
}

void ProgressBar::finish() {
  done = toDo;
  printProgress();
  clear();
}

void ProgressBar::clear() {
  std::cout << std::setw(70) << std::left << std::setfill(' ') << "\r";
  std::cout << "\r";
  std::cout.flush();
}
