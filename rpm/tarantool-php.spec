%global php_apiver %((echo 0; php -i 2>/dev/null | sed -n 's/^PHP API => //p') | tail -1)
%global php_extdir %(php-config --extension-dir 2>/dev/null || echo "undefined")
%global php_version %(php-config --version 2>/dev/null || echo 0)
%global build_version %(git describe --long | sed "s/[0-9]*\.[0-9]*\.[0-9]*-//" | sed "s/-[a-z 0-9]*//")

Name: php-tarantool
Version: 0.0.14
Release: %{build_version}
Summary: PECL PHP driver for Tarantool/Box
Group: Development/Languages
License: MIT
URL: https://github.com/tarantool/tarantool-php/
Source0: https://github.com/tarantool/tarantool-php/archive/%{version}.tar.gz
Source1: tarantool.ini
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires: php-devel
Requires: php(zend-abi) = %{php_zend_api}
Requires: php(api) = %{php_apiver}
%description
PECL PHP driver for Tarantool/Box
Tarantool is an in-memory database and Lua application server.
This package provides PECL PHP driver for Tarantool/Box.

%prep
%setup -q -c tarantool-php-%{version}
%build
%{_bindir}/phpize
%configure
make %{?_smp_mflags}
%install
rm -rf $RPM_BUILD_ROOT
make install INSTALL_ROOT=$RPM_BUILD_ROOT
# install configuration
%{__mkdir} -p $RPM_BUILD_ROOT%{_sysconfdir}/php.d
%{__cp} %{SOURCE1} $RPM_BUILD_ROOT%{_sysconfdir}/php.d/tarantool.ini
%clean
rm -rf $RPM_BUILD_ROOT
%files
%defattr(-,root,root,-)
%config(noreplace) %{_sysconfdir}/php.d/tarantool.ini
%{php_extdir}/tarantool.so
