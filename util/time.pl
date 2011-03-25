#!/usr/bin/perl -w
# vim: set sw=4 ts=4 si et:
# Copyright: Guido Socher
# $Revision: $, last changed: $Date: $
#
use strict;
#
my $t;
$t=time();
printf("sec since Jan 1st 1970: %d\n",$t);
my $now_string = gmtime($t);
print "gmtime: $now_string\n";
__END__ 

