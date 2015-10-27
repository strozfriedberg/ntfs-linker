/*
 * ntfs-linker
 * Copyright 2015 Stroz Friedberg, LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License is available at
 * <http://www.gnu.org/licenses/>.
 *
 * You can contact Stroz Friedberg by electronic and paper mail as follows:
 *
 * Stroz Friedberg, LLC
 * 32 Avenue of the Americas
 * 4th Floor
 * New York, NY, 10013
 * info@strozfriedberg.com
 */

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
