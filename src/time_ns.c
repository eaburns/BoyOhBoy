#include "time_ns.h"

#include <time.h>

double monoclock_time_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000000000 + (double)ts.tv_nsec;
}

void sleep_ns(long ns) {
  static const double NS_PER_S = 1e9;
  if (ns <= 0) {
    return;
  }
  long s = ns / NS_PER_S;
  struct timespec ts = {
      .tv_sec = s,
      .tv_nsec = ns - s,
  };
  nanosleep(&ts, NULL);
}
