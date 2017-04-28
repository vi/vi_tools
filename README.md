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


# cgroup_memory_pressure_monitor

Command-line access to control groups memory pressume monitoring events

```
Usage: cgroup_memory_pressure_monitor {low|medium|critical} /sys/fs/cgroup/memory/your_cgroup
```

# uksm_tease

Fill up memory with non-zero content compressible by Ultra Kernel Same page Merging.

# runso

Rundll for Linux. Loads main-like symbol from a library and executes it.

```
$ ./runso  ./runso main  ./runso main
Usage: runso ./libsomelibrary.so main <args>
```

# mempig

Set oom_score_adj, then fill up memory with zeroes.

```
$ ./mempig 
Killed

$ dmesg | grep -i oom
[228859.359178] mempig invoked oom-killer: gfp_mask=0x24280ca(GFP_HIGHUSER_MOVABLE|__GFP_ZERO), nodemask=0, order=0, oom_score_adj=1000
[228859.359234]  [<ffffffff81168397>] oom_kill_process+0x237/0x450
[228859.359372] [ pid ]   uid  tgid total_vm      rss nr_ptes nr_pmds swapents oom_score_adj name
[228860.221001] oom_reaper: reaped process 27664 (mempig), now anon-rss:0kB, file-rss:0kB, shmem-rss:0kB
```
