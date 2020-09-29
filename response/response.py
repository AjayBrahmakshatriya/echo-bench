import socket
import sys
import os

RESPONSE_BINARY_PATH="./response"

def main():
	# no arguments to parse here	
	s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	
	s.bind(("0.0.0.0", 50050))	
	
	s.listen(100)	
	while True:
		(conn, addr) = s.accept()	
		if os.fork() == 0:
			s.close()
			bin_args = [RESPONSE_BINARY_PATH, str(conn.fileno())]
			os.set_inheritable(conn.fileno(), True)
			os.execv(RESPONSE_BINARY_PATH, bin_args)
	
			print("Unreachable!")
			exit(-1)
		else:
			conn.close()

	print ("Server closed")

if __name__ == "__main__":
	main()

	
