curl http://tarantool.org/dist/public.key | sudo apt-key add -
echo "deb http://tarantool.org/dist/master/ubuntu/ `lsb_release -c -s` main" | sudo tee -a /etc/apt/sources.list.d/tarantool.list
sudo apt-get update > /dev/null
sudo apt-get -q -y install tarantool tarantool-dev

phpize && ./configure
make
make install
sudo pip install PyYAML
which python
sudo which python
sudo python test-run.py --flags
