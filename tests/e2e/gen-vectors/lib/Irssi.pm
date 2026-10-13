# Minimal stand-in for the irssi Perl API, just what rpe2e.pl calls, so the
# script can be loaded and driven by test/rpe2e.t without a running client.
#
# Like the real glue (src/perl/perl-common.h new_pv, perl-signals.c gstring),
# every string a test hands to the script should be a BYTE string - irssi
# never sets Perl's UTF-8 flag.
package Irssi;

use strict;
use warnings;

our ($DIR, @PRINTED, %SIGNALS, %COMMANDS, @SERVERS, $STOPPED, $CONTINUE);
our $BINARY     = '/usr/bin/erssi';
our $ABI_DATE   = 20260408;
our %SPECIAL    = ('$J' => '1.3.1', '$abiversion' => 58);

sub MSGLEVEL_CLIENTCRAP () { 0x80000 }

sub get_irssi_dir    { $DIR }
sub get_irssi_binary { $BINARY }
sub version          { $ABI_DATE }
sub parse_special    { my ($s) = @_; exists $SPECIAL{$s} ? $SPECIAL{$s} : '' }
sub print            { push @PRINTED, { window => undef, text => $_[0] } }
sub command_bind     { $COMMANDS{ $_[0] } = $_[1] }
sub signal_add       { push @{ $SIGNALS{ $_[0] } }, $_[1] }
sub signal_add_first { unshift @{ $SIGNALS{ $_[0] } }, $_[1] }
sub signal_stop      { $STOPPED = 1 }
sub signal_continue  { $CONTINUE = [@_] }
sub servers          { @SERVERS }

# Run the script's handler for one signal; returns (stopped, continued-args).
sub emit {
    my ($name, @args) = @_;
    local $STOPPED = 0;
    local $CONTINUE;
    $_->(@args) for @{ $SIGNALS{$name} || [] };
    return ($STOPPED, $CONTINUE);
}

sub printed_text { join "\n", map { $_->{text} } @PRINTED }

package Irssi::Test::Server;

sub new {
    my ($class, %opt) = @_;
    return bless { tag => 'net', nick => 'me', connected => 1, sent => [], queued => [],
                   items => {}, chans => [], %opt }, $class;
}
sub send_raw_now     { push @{ $_[0]{sent} }, $_[1] }
sub send_raw         { push @{ $_[0]{queued} }, $_[1] }
sub channels         { @{ $_[0]{chans} } }
sub window_item_find { $_[0]{items}{ lc $_[1] } }
sub query_find       { my $i = $_[0]{items}{ lc $_[1] }; $i && $i->{type} eq 'QUERY' ? $i : undef }

package Irssi::Test::Item;

sub new {
    my ($class, %opt) = @_;
    my $self = bless { type => 'CHANNEL', nicks => [], %opt }, $class;
    $self->{server}{items}{ lc $self->{name} } = $self if $self->{server};
    return $self;
}
sub print     { push @Irssi::PRINTED, { window => $_[0], text => $_[1] } }
sub nick_find { my ($c, $n) = @_; (grep { lc $_->{nick} eq lc $n } @{ $c->{nicks} })[0] }

1;
