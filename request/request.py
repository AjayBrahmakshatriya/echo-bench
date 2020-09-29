import socket
import sys
import os

REQUEST_BINARY_PATH="./request"

def main():
	if len(sys.argv) < 4:
		print("Usage: " + sys.argv[0] + " <ip address of server> <parallel requests> <total requests>")
		exit(-1)
	
	remote_ip = sys.argv[1]
	parallel_request = int(sys.argv[2])
	total_requests = int(sys.argv[3])

	s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	s.connect((remote_ip, 50050))
	
	bin_args = [REQUEST_BINARY_PATH, str(s.fileno()), str(parallel_request), str(total_requests)]
	os.set_inheritable(s.fileno(), True)
	os.execv(REQUEST_BINARY_PATH, bin_args)

	print ("Unrechable!")
	exit(-1)


if __name__ == "__main__":
	main()
	
