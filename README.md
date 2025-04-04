# Name

`ngx_http_loop_check_module` allows NGINX to use the [CDN-Loop](https://datatracker.ietf.org/doc/rfc8586/) header to prevent request loops.

# Table of Content

- [Name](#name)
- [Table of Content](#table-of-content)
- [Status](#status)
- [Synopsis](#synopsis)
- [Installation](#installation)
- [Directives](#directives)
  - [loop\_check](#loop_check)
  - [loop\_check\_cdn\_id](#loop_check_cdn_id)
  - [loop\_check\_status](#loop_check_status)
  - [loop\_check\_max\_allow\_loops](#loop_check_max_allow_loops)
- [Variables](#variables)
  - [$loop\_check\_current\_loops](#loop_check_current_loops)
  - [$loop\_check\_proxy\_cdn\_loop](#loop_check_proxy_cdn_loop)
- [How It Works](#how-it-works)
- [Author](#author)
- [License](#license)

# Status

This Nginx module is currently considered experimental. Issues and PRs are welcome if you encounter any problems.

# Synopsis

```nginx
http {
    # Enable the module in a location block
    loop_check on;
    loop_check_cdn_id my_cdn_id;
    loop_check_status 508;
    loop_check_max_allow_loops 10;

    server {
        listen 80;
        server_name example.com;
        location / {
            proxy_set_header CDN-Loop $loop_check_proxy_cdn_loop;
            proxy_pass http://example.upstream.com;
        }
    }
}
```

# Installation

To use theses modules, configure your nginx branch with `--add-module=/path/to/ngx_http_loop_check_module`.

# Directives

## loop_check

**Syntax:** *loop_check on | off;*

**Default:** *loop_check off;*

**Context:** *http, server, location*

Enables or disables the loop detection for the current scope. When enabled, the module checks the `CDN-Loop` header to track the number of hops and blocks requests exceeding the allowed limit.

## loop_check_cdn_id

**Syntax:** *loop_check_cdn_id string;*

**Default:** *loop_check_cdn_id openresty;*

**Context:** *http, server, location*

Sets the unique identifier for your clusters. This identifier is used to parse and track loops in the CDN-Loop header.

## loop_check_status

**Syntax:** *loop_check_status code;*

**Default:** *loop_check_status 508;*

**Context:** *http, server, location*

Sets the HTTP status code returned when a request exceeds the allowed loop limit. The code must be between `400` and `599` (client or server errors).

## loop_check_max_allow_loops

**Syntax:** *loop_check_max_allow_loops number;*

**Default:** *loop_check_max_allow_loops 10;*

**Context:** *http, server, location*

Sets the maximum number of allowed loops before blocking the request. The number must be greater than 0.

# Variables

## $loop_check_current_loops

Returns the current detected loop count extracted from the CDN-Loop header. This value represents the number of hops your request has already passed through CDN nodes.

## $loop_check_proxy_cdn_loop

Constructs the new `CDN-Loop` header value to be sent to downstream proxies. This value includes:

1. The current CDN node's identifier and incremented loop count (e.g., `my_cdn; loops=2`).
2. Remaining other entries from the original `CDN-Loop` header (if any).

Example Usage:

```nginx
location / {
    proxy_set_header CDN-Loop $loop_check_proxy_cdn_loop;
    proxy_pass http://backend;
}
```


# How It Works
1. Detection:
The module parses the `CDN-Loop` header to identify the number of hops. Each hop is formatted as:
Format: `Cdn-Loop: <cdn_id>; loops=<count>, ...`
Example: `Cdn-Loop: my_cdn; loops=2, another_cdn; loops=1`.

2. Tracking:
The current hop count (current_loops) is extracted from the header.
The module increments the count and constructs a new `CDN-Loop` value for downstream proxies.

3. Blocking:
If the detected loop count exceeds `loop_check_max_allow_loops`, NGINX returns the configured `loop_check_status` (e.g., 508).

# Author

Hanada im@hanada.info

# License

This Nginx module is licensed under [BSD 2-Clause License](LICENSE).
