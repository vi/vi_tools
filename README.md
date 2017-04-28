# lease
Command-line access to `F_SETLEASE`

```
$ touch q
$ ./lease q rw -- sh -c 'echo Cleanup request && sleep 1 && echo Now clean'&
[1] 28330
$ echo "Before trying to access" && cat q && echo "After tying to access"
Before trying to access
Cleanup request
Now clean
[1]+  Done                    ./lease q rw -- sh -c 'echo Cleanup request && sleep 1 && echo Now clean'
After tying to access
```

Like `flock` tool, can also operate on file descriptors.
