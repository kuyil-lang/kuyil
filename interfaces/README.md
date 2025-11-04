# Kuyil Interface Files - Quick Reference

## All Available Interfaces

| Interface | File | Description |
|-----------|------|-------------|
| `str` | `interface_str.kyl` | String manipulation |
| `math` | `interface_math.kyl` | Mathematical operations |
| `http` | `interface_http.kyl` | HTTP client |
| `date`/`datetime` | `interface_datetime.kyl` | Date/time operations |
| `crypto` | `interface_crypto.kyl` | Cryptography & hashing |
| `sqlite` | `interface_sqlite.kyl` | SQLite database |
| `file` | `interface_fileio.kyl` | File I/O operations |
| `rpc` | `interface_rpc.kyl` | RPC client/server |
| `webview` | `interface_webview.kyl` | Desktop GUI |
| `audio`/`ffmpeg` | `interface_ffmpeg.kyl` | Audio/video processing |
| `compress`/`decompress` | `interface_transcoder.kyl` | Compression |

## Quick Start

```kuyil
// Import the interface you need
import("../interfaces/interface_str.kyl")

// Use the methods directly
print(length("hello"))       // 5
print(upper("hello"))        // "HELLO"
print(substring("kuyil", 1, 3))  // "uy"
```

## String Operations (`interface_str.kyl`)

```kuyil
length(input: string) -> number
substring(input: string, start: number, end: number) -> string
upper(input: string) -> string
lower(input: string) -> string
trim(input: string) -> string
contains(haystack: string, needle: string) -> bool
replace(input: string, from: string, to: string) -> string
split(input: string, delimiter: string) -> array
to_number(input: string) -> number
to_string(value: string) -> string
```

## Math Operations (`interface_math.kyl`)

```kuyil
abs(x: number) -> number
floor(x: number) -> number
ceil(x: number) -> number
round(x: number) -> number
sqrt(x: number) -> number
pow(x: number, y: number) -> number
sin(x: number) -> number
cos(x: number) -> number
tan(x: number) -> number
```

## HTTP Client (`interface_http.kyl`)

```kuyil
client_get(url: string) -> string
client_post(url: string, body: string) -> string
```

## DateTime (`interface_datetime.kyl`)

```kuyil
date_now() -> number
date_current() -> string
datetime_now() -> number
datetime_current() -> string
date_add(timestamp: number, seconds: number) -> number
date_sub(timestamp: number, seconds: number) -> number
date_diff(timestamp1: number, timestamp2: number) -> number
date_unix(timestamp: number) -> number
date_from_unix(unix: number) -> number
date_iso(timestamp: number) -> string
date_format(timestamp: number, format: string) -> string
```

## Crypto (`interface_crypto.kyl`)

```kuyil
crypto_md5(input: string) -> string
crypto_sha256(input: string) -> string
crypto_sha512(input: string) -> string
crypto_blake2b(input: string) -> string
crypto_hmac_sha256(message: string, key: string) -> string
crypto_base64_encode(input: string) -> string
crypto_base64_decode(input: string) -> string
crypto_random_hex(length: number) -> string
crypto_generate_uuid() -> string
crypto_generate_token(length: number) -> string
```

## SQLite (`interface_sqlite.kyl`)

```kuyil
open_database(path: string) -> number
close_database(db: number) -> bool
execute_sql(db: number, sql: string) -> bool
execute_query(db: number, sql: string) -> number
result_first_row(result: number) -> number
result_next_row(result: number) -> number
row_get_int(row: number, index: number) -> number
row_get_text(row: number, index: number) -> string
row_get_real(row: number, index: number) -> number
free_result(result: number) -> bool
get_last_error() -> string
```

## File I/O (`interface_fileio.kyl`)

```kuyil
read_text(path: string) -> string
read_csv(path: string) -> array
read_json(path: string) -> object
read_yaml(path: string) -> object
exists(path: string) -> bool
size(path: string) -> number
validate(path: string) -> bool
write_text(path: string, content: string) -> bool
```

## RPC (`interface_rpc.kyl`)

### Client Operations
```kuyil
rpc_create_default_client_config() -> number
rpc_client_create(config: number) -> number
rpc_client_connect(client: number) -> bool
rpc_client_disconnect(client: number) -> bool
rpc_client_call_method(client: number, method: string, args: string) -> string
rpc_client_is_connected(client: number) -> bool
rpc_client_destroy(client: number) -> bool
```

### Server Operations
```kuyil
rpc_create_default_server_config() -> number
rpc_server_create(config: number) -> number
rpc_server_start(server: number) -> bool
rpc_server_stop(server: number) -> bool
rpc_server_register_method(server: number, name: string, handler: string) -> bool
rpc_server_is_running(server: number) -> bool
rpc_server_destroy(server: number) -> bool
```

## WebView (`interface_webview.kyl`)

```kuyil
webview_init() -> number
webview_create(title: string, width: number, height: number) -> number
webview_load_html(webview: number, html: string) -> bool
webview_load_url(webview: number, url: string) -> bool
webview_run(webview: number) -> bool
webview_show(webview: number) -> bool
webview_hide(webview: number) -> bool
webview_destroy(webview: number) -> bool
webview_cleanup() -> bool
```

## FFmpeg Audio (`interface_ffmpeg.kyl`)

```kuyil
audio_editor_create() -> number
audio_load(path: string) -> number
audio_save(segment: number, path: string) -> bool
audio_get_duration(segment: number) -> number
audio_trim(segment: number, start: number, end: number) -> number
audio_fade_in(segment: number, duration: number) -> number
audio_fade_out(segment: number, duration: number) -> number
audio_adjust_volume(segment: number, factor: number) -> number
audio_normalize(segment: number) -> number
audio_concat(segments: array) -> number
audio_overlay(base: number, overlay: number, position: number) -> number
audio_editor_destroy(editor: number) -> bool
```

## Compression (`interface_transcoder.kyl`)

```kuyil
// Compression
compress.gzip(input: string) -> string
compress.zip(input: string) -> string

// Decompression
decompress.gzip(input: string) -> string
decompress.zip(input: string) -> string

// Utilities
unzip.to_directory(zipPath: string) -> bool
```

## Usage Patterns

### Single Library
```kuyil
import("../interfaces/interface_str.kyl")
print(upper("hello"))
```

### Multiple Libraries
```kuyil
import("../interfaces/interface_str.kyl")
import("../interfaces/interface_math.kyl")

let text = "value: " + round(sqrt(16))
print(upper(text))
```

### Mixed Aliases
```kuyil
import("../interfaces/interface_str.kyl")

// All equivalent:
length("test")      // Unprefixed
str_length("test")  // Prefixed
str.length("test")  // Dotted
```

## Installation

Interface files are already included in Kuyil under `interfaces/`. No additional setup needed!

## See Full Documentation

- [Interface System Guide](INTERFACE_SYSTEM_GUIDE.md)
- [FFI System Guide](FFI_SYSTEM_GUIDE.md)
- [Language Reference](language-reference.md)
