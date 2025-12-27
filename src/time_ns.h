#ifndef _TIME_H_
#define _TIME_H_

// Returns the current value of the monotonic clock in nanoseconds.
double monoclock_time_ns();

// Sleeps for the specified number of nanoseconds.
void sleep_ns(long ns);

#endif // _TIME_H_
