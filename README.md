# np

NetPipe makes a pipe over the TCP like **nc** (Ncat) with additional features:
1. listens to connections infinitely
1. accepts parallel connections
1. starts specified command for all connections separately
1. returns back command's stdout to the client
1. sends a signal in case of command failed and client side exits with 1

## Build and install

```
mkdir build && cd build && cmake .. && make install
```

### Example 1
##### NetPipe
```
server$ np -l 3000 -- md5sum

client$ np localhost 3000 < file
6de5dd9caade388447c1d4747472cfcf  -

client$ np localhost 3000 < file
6de5dd9caade388447c1d4747472cfcf  -
```
##### Ncat
```
server$ nc -l 3000 -c md5sum

client$ nc localhost 3000 < file
client$ np localhost 3000 < file
Ncat: Connection refused.
```
As you see, *nc* supports/listens only one connection per launch.

### Example 2
##### NetPipe
```
server$ np -l 3000 -- tar -C /path/to -xpv

client$ tar -c file1 sub/file2 | np localhost 3000
file1
sub/file2

client$ tar -c file3 sub/file4 | np localhost 3000`
file3
sub/file4
```
##### Ncat
```
server$ nc -l 3000 -c "tar -C /path/to -xpv"

client$ tar -c file1 sub/file2 | nc localhost 3000
client$ tar -c file3 sub/file4 | nc localhost 3000`
Ncat: Connection refused.
```

### Benchmark on 15Gb file
##### NetPipe
```
server$ np -l 3000 -c "cat >/dev/null"

client$ time np localhost 3000 < file
real	0m12.963s
user	0m0.767s
sys	0m12.190s
```
##### Ncat
```
server$ nc -l 3000 -c "cat >/dev/null"

client$ time nc localhost 3000 < file
real	0m25.887s
user	0m7.592s
sys	0m17.496s
```
