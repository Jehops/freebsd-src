#!/bin/sh
# $FreeBSD: src/sbin/newfs/runtest01.sh,v 1.1.40.1 2010/12/21 17:10:29 kensmith Exp $

set -e

MD=99
ME=98
s=1m
mdconfig -d -u $MD || true
mdconfig -d -u $ME || true
mdconfig -a -t malloc -s $s -u $MD
mdconfig -a -t malloc -s $s -u $ME
disklabel -r -w md$MD auto
disklabel -r -w md$ME auto
./newfs -R /dev/md${MD}c
./newfs -R /dev/md${ME}c
if cmp /dev/md${MD}c /dev/md${ME}c ; then
	echo "Test passed"
	e=0
else
	echo "Test failed"
	e=1
fi
mdconfig -d -u $MD || true
mdconfig -d -u $ME || true
exit $e

