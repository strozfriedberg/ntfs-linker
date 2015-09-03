#include <ctime>

class ProgressBar {
private:
  uint64_t done;
  uint64_t toDo;
  int last;
public:
  void addToDo(uint64_t x);
  void addDone(uint64_t x);
  void setDone(uint64_t x);
  void printProgress();
  ProgressBar(uint64_t x);
  void finish();
  void clear();
};


