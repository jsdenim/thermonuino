#include <TimeLib.h>
//https://github.com/PaulStoffregen/Time
//Représente un cycle de travail.
class DutyCycle {
  private :
    time_t cycleStart;
    time_t cycleEnd;
    long wattGenerated;
};