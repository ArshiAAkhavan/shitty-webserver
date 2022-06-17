# shitty-webserver

## build
compile using multi threaded architecture (recommended):
```bash 
make clean 
make type=mhtread
```

Compile using fork architecture:
```bash 
make clean 
make type=mprocess
```

## install 
```bash 
sudo make install
```

## uninstall 
```bash 
sudo make uninstall
```
## configuration 
sample configuration can be found in `httpserver.conf` file

By default, `make install` would put a copy of `server.conf` in `/etc/httpserver.conf`, but you can disable this feature and add your configuration manually

### Configuration option 

- port: the port which server binds to.
- files: the path of which, server serves files from. 
- log_path: path to the log file
- concurrency_level: maximum number of concurrent connection handled by the server. 


