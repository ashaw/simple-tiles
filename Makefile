all:
	./waf

install:
	./waf install

data:
	cd data && make && cd ..

test: all data
	build/test/runner
	build/test/api
	build/test/benchmark

.PHONY: all install test data