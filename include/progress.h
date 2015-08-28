#include <ctime>

class ProgressBar {
private:
  unsigned long long done;
  unsigned long long toDo;
  int last;
public:
  void addToDo(unsigned long long x);
  void addDone(unsigned long long x);
  void setDone(unsigned long long x);
  void printProgress();
  ProgressBar(unsigned long long x);
  void finish();
  void clear();
};


