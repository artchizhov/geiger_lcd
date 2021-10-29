import serial


s = serial.Serial('COM8')
f = open("geiger_data.csv", "w")

f.write("Time;Counts;CPM\n")

while True:
	res = str(s.readline()).replace("b'", "").replace("\\r\\n'", "")
	f.write(res)
	f.write("\n")
	print(res)

f.close()
s.close()