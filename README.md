# crsh-lite

crsh-lite is a light shell handler written in C, meant to make the process of remote administration easier. 

It uses netcat-like functinality to provide asynchronous I/O operations on the remote host. It also automatically spawns a PTY, stabilizes your prompt and resizes the remote terminal, as your local one changes.

## Example Compilation

```bash
make
```

## Syntax

```bash
# Server mode (listen)
./crsh-lite <LPORT>

# Client Mode (connect)
./crsh-lite <RPORT> <RHOST>
```

## Example Usage

```bash
./crsh-lite 9001
./crsh-lite 1337 localhost
```
