.PHONY: test

all: package

package:
	git clone https://github.com/packpack/packpack.git packpack
	packpack/packpack

test:
	curl -s https://packagecloud.io/install/repositories/tarantool/1_10/script.deb.sh | sudo bash
	sudo apt install tarantool
	phpize && ./configure && make all && sudo make install
	sudo pip install PyYAML
	./test-run.py
