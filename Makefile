$(shell mkdir -p build)


all: build/request build/response build/request.py build/response.py build/raw_request build/raw_response

build/request: request/request.c
	gcc $< -O3 -o $@

build/request.py: request/request.py
	cp $< $@

build/response: response/response.c
	gcc $< -O3 -o $@

build/response.py: response/response.py
	cp $< $@


build/raw_request: raw_request/request.c
	gcc $< -O3 -o $@

build/raw_response: raw_response/response.c
	gcc $< -O3 -o $@
clean:
	rm -rf build





