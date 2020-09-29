$(shell mkdir -p build)


all: build/request build/response build/request.py build/response.py

build/request: request/request.c
	gcc $< -O3 -o $@

build/request.py: request/request.py
	cp $< $@

build/response: response/response.c
	gcc $< -O3 -o $@

build/response.py: response/response.py
	cp $< $@


clean:
	rm -rf build





