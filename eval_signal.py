import matplotlib.pyplot as plt
import numpy as np
import math


def list_of_arrays(path):
	""" Save values from file to an array for each mode. """
	
	file = open(path,'r')
	list = []
	for line in file:
		list.append(str_to_flts(line))

	# Find borders between different modes (SE_MUX / DIFF_MUX / SE_CONT / DIFF_CONT)
	newModeLine = [0]
	for x in xrange(0,len(list)-1):
		if list[x+1][0] < list[x][0]:
			newModeLine.append(x+1)	
	newModeLine.append(len(list))

	# Create numpy arrays (one 2d array for each mode) 
	arrays = []
	for x in range(0, len(newModeLine)-1):
		arrays.append (np.array(list [newModeLine[x] : newModeLine[x+1]] ))

	file.close()
	return arrays


def str_to_flts(string):
	""" Convert string of values to floats. """
	return [float(num) for num in string.split()]


def clean_data(array):
	""" Delete data that is completely wrong. """ #modify for more than one channel
	size = np.size(array[:,0])

	#calculate average: do not include values that are out of 0 - 5 V range
	values = array[ array[:,1] < 5]
	avg = np.average(values[:,1])

	#delete rows that are more than 5uV different from average -> OK since we are measuring static voltage
	del_row = []
	row_num = 0
	for row in array:
		if abs(row[1] - avg) > 0.005: # modify this
			del_row.append(row_num)
		row_num += 1
	array = np.delete(array, del_row, 0)

	return array


def std_dev(array):
	""" Calculate standard deviation of a sample. """
	vals = array[:,1]
	avg = np.average(vals)
	size = len(array[:,1])
	var = 0 #variance
	for val in vals:
		var += (val - avg)**2/size
	return math.sqrt(var)


def fourier(array):
	""" Calculate fourier transform of a sample. """
	val = np.fft.fft(array[:,1])
	abs_val = [math.sqrt(x) for x in val.real**2 + val.imag**2]
	return abs_val

def sampling(array):
	""" Calculate average sampling frequency. """
	time = array[len(array)-1,2] - array[0,2]
	samples = len(array)
	return samples/time*1e6


labels = ["4230 SPS, enostranski nacin merjenja z menjavo kanalov",
	"4230 SPS, diferencni nacin merjenja z menjavo kanalov",
	"30 000 SPS, enostranski kontinuirani nacin merjenja",
	"30 000 SPS, diferencni kontinuirani nacin merjenja"]
xlabel1 = "Cas [ms]"
xlabel2 = "Frekvenca [Hz]"
ylabel1 = "Napetost [V]"
ylabel2 = "Amplituda"

############################################
## read file with data + perform cleaning ##
############################################

#arrays = list_of_arrays('test_file3_5s')
arrays = list_of_arrays('test_file4')
for x in xrange(0,len(arrays)):
	arrays[x] = clean_data(arrays[x])

##################################
## calculate standard deviation ##
##################################

deviation = []
print "Standard deviation: "
for arr in arrays:
	deviation.append(std_dev(arr))
print deviation

##################################
## calcualate fourier transform ##
##################################

ft = []   # fourier transform values
sps = []  # sampling speed values
freq = [] # frequency vectors - for plotting
for i in range(0,len(arrays)):
	ft.append(fourier(arrays[i]))
	sps.append(sampling(arrays[i]))
	freq.append( sps[i]/len(arrays[i]) * np.linspace(0, len(arrays[i])-1, len(arrays[i])))

print sps

# plot volt - time graph
i = 0
for arr in arrays:
	fig = plt.figure()
	plt.plot(arr[:,1], label = labels[i])
	fig.suptitle(labels[i])
	plt.xlabel(xlabel1)
	plt.ylabel(ylabel1)
	ax = plt.axes()
	ax.get_yaxis().get_major_formatter().set_useOffset(False)
	plt.show()
	i += 1

# plot amplitude - frequency graph
i = 0
for mode, f in zip(ft, freq):
	fig = plt.figure()
	plt.plot(f[1:len(f)], mode[1:len(mode)], label = labels[i])
	fig.suptitle(labels[i])
	plt.xlabel(xlabel2)
	plt.ylabel(ylabel2)
	ax = plt.axes()
	ax.get_yaxis().get_major_formatter().set_useOffset(False)
	plt.show()
	i += 1



