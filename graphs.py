import sys
import os
import timeit

def run_program():
	os.system("./" + sys.argv[1] + " " + " ".join(sys.argv[2:])+" > /dev/null")
    
t = timeit.Timer(run_program)

execution_time = t.timeit(1000)

print(execution_time/1000)
