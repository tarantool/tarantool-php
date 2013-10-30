# Created by: Gvozdikov Veniamin <g.veniamin@googlemail.com>
# $FreeBSD$

PORTNAME=	tarantool
PORTVERSION=	${TR_VER}
CATEGORIES=	databases
MASTER_SITES=	GH GHC
PKGNAMEPREFIX=	php5-

MAINTAINER=	g.veniamin@googlemail.com
COMMENT=	PECL PHP driver for Tarantool/Box

USE_PHP=	yes
USE_GITHUB=	yes
USE_PHPEXT=	yes
USE_PHPIZE=	yes
DEFAULT_PHP_VER=5

PHP_MODNAME=	${PORTNAME}
GH_PROJECT=	${PORTNAME}-php
GH_ACCOUNT=	zloidemon
GH_COMMIT=	427bcbb
GH_TAGNAME=	master
TR_VER=		20130513

CONFIGURE_ARGS+=	--with-tarantool

.include <bsd.port.mk>
