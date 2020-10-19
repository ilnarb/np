# np

NetPipe is pipe over the TCP like **nc** (netcat) with additional features:
1. listens to connections infinitely
1. could accept parallel connections 
1. starts specified command for all connections separately
1. returns back command stdout to the client
1. sends a signal in case of command failed and client side exits with 1

## Build and install

```
mkdir build && cd build && cmake .. && make install
```

### example 1

```
server$ np -l 3000 md5sum

client$ np localhost 3000 < file
6de5dd9caade388447c1d4747472cfcf  -

client$ np localhost 3000 < file
6de5dd9caade388447c1d4747472cfcf  -
```

### example 2

```
server$ np -l 3000 tar -C /path/to -xpv

client$ tar -c file1 sub/file2 | np localhost 3000
file1
sub/file2

client$ tar -c file3 sub/file4 | np localhost 3000`
file3
sub/file4
```
