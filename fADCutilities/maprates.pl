#!/usr/bin/perl -wall

# this program is used in conjunction with the feedback implemented in epics

use Time::HiRes qw(usleep nanosleep);
use Time::Piece;
use POSIX qw/strftime/;
#use Math::Round;

while (1) {
	$command = "caput ".$cratenames[$cratenum].":t_set  $newtempsetpoint";
	system $command;

}

