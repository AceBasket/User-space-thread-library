import sys
import os
import timeit
import numpy as np
import matplotlib.pyplot as plt


files_by_id = {}

dir = "install/bin"
files = os.listdir(dir)
files.sort()

for file in files:
    tst_id = file[:2]
    if tst_id in files_by_id:
        files_by_id[tst_id].append(file)
    else:
        files_by_id[tst_id] = [file]

# converting dictionnary to array
files_by_id = list(files_by_id.values())


def run_program(name: str, args: list[int]):
	os.system("./" + name + " " + " ".join(str(arg) for arg in args) + " > /dev/null")

def get_nb_args(name: str):
	nb_args = {
		0: ['01', '02', '11', '12'],
	 	1: ['21', '22', '23', '51'],
		2: ['32', '33']
	}
	for i in range(3):
		if name[:2] in nb_args[i]:
			return i

def measure_time(name: str, args: list[int]=[], nb_exec: int = 100)->float:
	return timeit.Timer(lambda: run_program(name, args)).timeit(nb_exec)

def plot_no_arg(data: dict, ax, fname: str):
	ax.bar(list(data.keys()), [x[0] for x in list(data.values())])
	ax.set_ylabel("Temps (/s)")
	ax.set_title(fname)
	return ax

def plot_with_args(data: dict, ax, fname: str, args_name: list[str], args_value: list[int]):
	ax.plot(data['thread'], c='b', label="thread")
	ax.plot(data['pthread'], c='r', label="pthread")
	ax.legend()
	title = fname + ": " + args_name[0] + " = " + str(args_value[0])
	if (len(args_name) == 2):
		title += " " + args_name[1] + " = " + str(args_value[1])
	ax.set_title(title)
	ax.set_ylabel("Temps (/s)")
	return ax

nb_cols = 3
nb_rows = len(files_by_id)//nb_cols if (len(files_by_id) > nb_cols) else 1
fig, axs = plt.subplots(nb_rows, nb_cols)
plot_idx = 0
set_arg_value = 3

for [f_thread, f_pthread] in files_by_id:
	f_thread_name = "install/bin/" + f_thread
	f_pthread_name = "install/bin/" + f_pthread
	data = {'thread': [], 'pthread': []}
	nb_args = get_nb_args(f_thread)
	(row_idx, col_idx) = divmod(plot_idx, nb_cols)
	ax = axs[col_idx] if nb_rows == 1 else axs[row_idx, col_idx]
	plot_idx+=1
	if (nb_args == 0):
		data['thread'].append(measure_time(f_thread_name))
		data['pthread'].append(measure_time(f_pthread_name))
		plot_no_arg(data, ax, f_thread).plot()
	elif (nb_args == 1):
		for i in range(1, 11):
			data['thread'].append(measure_time(f_thread_name, [i]))
			data['pthread'].append(measure_time(f_pthread_name, [i]))
		plot_with_args(data, ax, f_thread,['nb threads'], [i]).plot()
	else:
		for i in range(1, 11):
			data['thread'].append(measure_time(f_thread_name, [i, set_arg_value]))
			data['pthread'].append(measure_time(f_pthread_name, [i, set_arg_value]))
		plot_with_args(data, ax, f_thread, ['nb threads', 'nb yields'], [i, set_arg_value]).plot()

plt.show()

# d = {'thread': [0.08163618499929726], 'pthread': [0.06830254799933755]}

# fig, ax = plt.subplots(1)

# plot_no_arg(d,ax).plot()

# plt.show()