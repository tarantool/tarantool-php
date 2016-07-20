curl http://download.tarantool.org/tarantool/1.6/gpgkey | sudo apt-key add -
release=`lsb_release -c -s`

# append two lines to a list of source repositories
sudo rm -f /etc/apt/sources.list.d/*tarantool*.list
sudo tee /etc/apt/sources.list.d/tarantool_1_6.list <<- EOF
deb http://download.tarantool.org/tarantool/1.6/ubuntu/ $release main
EOF

# install
sudo apt-get update > /dev/null
sudo apt-get -qy install tarantool tarantool-dev

tarantool --version
phpize && ./configure
make
make install
sudo pip install PyYAML
/usr/bin/python test-run.py
