#!/usr/bin/perl

# Tests for ngx_http_loop_detect_module.

###############################################################################

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use Test::Nginx qw/ :DEFAULT http_content /;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http ngx_http_loop_detect_module/)
	->plan(7);

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location = /off {
            alias %%TESTDIR%%/ok;
        }

        location = /on {
            loop_detect on;
            loop_detect_cdn_id edge;
            loop_detect_max_allow_loops 2;
            add_header X-Loop
                "$loop_detect_current_loops|$loop_detect_proxy_add_cdn_loop";
            alias %%TESTDIR%%/ok;
        }

        location = /custom {
            loop_detect on;
            loop_detect_cdn_id edge;
            loop_detect_max_allow_loops 1;
            loop_detect_status 429;
            alias %%TESTDIR%%/ok;
        }
    }
}

EOF

$t->write_file('ok', 'ok');
$t->run();

###############################################################################

is(loop_value(http_get('/on')), '0|edge; loops=1', 'new loop header');
is(loop_value(cdn_get('/on', 'edge; loops=1')),
	'1|edge; loops=2', 'existing loop header');
is(loop_value(cdn_get('/on',
	'other; loops=4, edge; loops=2, tail; loops=1')),
	'2|edge; loops=3, other; loops=4, tail; loops=1',
	'preserve other CDN entries');
like(cdn_get('/on', 'edge; loops=3'), qr/508 /,
	'default loop status');
like(cdn_get('/custom', 'edge; loops=2'), qr/429 /,
	'custom loop status');
like(cdn_get('/off', 'nginx; loops=99'), qr/200 OK/,
	'loop detection disabled');
is(loop_value(cdn_get('/on', 'invalid')), '0|edge; loops=1',
	'ignore malformed entry');

###############################################################################

sub cdn_get {
	my ($uri, $value) = @_;
	return http(<<EOF);
GET $uri HTTP/1.0
Host: localhost
CDN-Loop: $value

EOF
}

sub loop_value {
	my ($response) = @_;
	$response =~ /^X-Loop:\s*(.+?)\x0d?$/mi;
	return $1;
}

###############################################################################
