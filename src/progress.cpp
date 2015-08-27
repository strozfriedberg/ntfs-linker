#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "progress.h"

void ProgressBar::addToDo(unsigned long long x) {
  toDo += x;
}

void ProgressBar::addDone(unsigned long long x) {
  done += x;
  printProgress();
}

void ProgressBar::setDone(unsigned long long x) {
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

ProgressBar::ProgressBar(unsigned long long x) {
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
