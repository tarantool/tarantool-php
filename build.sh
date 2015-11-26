mkdir -p rpmbuild/SOURCES

git clone -b $1 https://github.com/tarantool/tarantool-php.git
cd tarantool-php
git submodule update --init --recursive
tar cvf `cat rpm/php-tarantool.spec | grep Version: |sed -e  's/Version: //'`.tar.gz . --exclude=.git
sudo yum-builddep -y rpm/php-tarantool.spec

cp *.tar.gz ../rpmbuild/SOURCES/
cp rpm/*.ini ../rpmbuild/SOURCES/
rpmbuild -ba rpm/php-tarantool.spec
cd ../

# move source rpm
sudo mv /home/rpm/rpmbuild/SRPMS/*.src.rpm result/

# move rpm, devel, debuginfo
sudo mv /home/rpm/rpmbuild/RPMS/x86_64/*.rpm result/
ls -liah result
