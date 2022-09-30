#!/usr/bin/env python3
import sys
MAX_ENERGY = 262143304619
r = []
for energy_start in sys.stdin:
    energy_start = int(energy_start.strip())
    energy_end = int(sys.stdin.readline().strip())
    assert energy_start < MAX_ENERGY
    assert energy_end < MAX_ENERGY
    if energy_end >= energy_start:
        energy_result = energy_end - energy_start
        did_overflow = False
    else:
        energy_result = (MAX_ENERGY - energy_start) + energy_end
        did_overflow = True

    print("{}{}".format(energy_result, " --" if did_overflow else "")) 
    r.append(round(energy_result / 1000000000, 2))

print(r)
