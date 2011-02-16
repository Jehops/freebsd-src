#!/bin/sh

# $FreeBSD: src/tools/regression/usr.bin/make/syntax/enl/test.t,v 1.2.14.1 2010/12/21 17:10:29 kensmith Exp $

cd `dirname $0`
. ../../common.sh

# Description
DESC="Test escaped new-lines handling."

# Run
TEST_N=5
TEST_2_TODO="bug in parser"

eval_cmd $*
