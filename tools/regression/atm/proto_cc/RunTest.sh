#!/bin/sh
# $FreeBSD: src/tools/regression/atm/proto_cc/RunTest.sh,v 1.1.34.1 2010/12/21 17:10:29 kensmith Exp $

. ../Funcs.sh

parse_options $*

DATA=$LOCALBASE/share/atmsupport/testsuite_cc

$LOCALBASE/bin/ats_cc $options $DATA/CC_Funcs $DATA/CC_??_??
