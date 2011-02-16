# $FreeBSD: src/sys/conf/kmod_syms.awk,v 1.4.40.1 2010/12/21 17:10:29 kensmith Exp $

# Read global symbols from object file.
BEGIN {
        while ("${NM:='nm'} -g " ARGV[1] | getline) {
                if (match($0, /^[^[:space:]]+ [^AU] (.*)$/)) {
                        syms[$3] = $2
                }
        }
        delete ARGV[1]
}

# De-list symbols from the export list.
{
        delete syms[$0]
}

# Strip commons, make everything else local.
END {
        for (member in syms) {
                if (syms[member] == "C")
                        print "-N" member
                else
                        print "-L" member
        }
}
