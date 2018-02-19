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

# udpfilepecker

Listed UDP port and do two things:

* Periodically send a UDP packet to specified destination (to keep NAT connection alive). The receiver is probably the udppair (see below).
* For each received UDP packet containing two numbers x and y (scanf "%d%d", &x,&y), write y to file descriptor number x.

Also tries to raise it's scheduling priority and lock all pages to memory.

Intended for remotely controlling various /sys/class/{brightness,gpio,pwm}/ knobs in a simplistic way.

# udppair

Listen two UDP ports and exchange information between them. Each reply goes to the lastly received peer address. Something like:

    socat udp-l:1234 udp-l:1235

But it does not "lock in" to one peer.

# udptimeoutchecker

Measure UDP connection timeout in NAT by probing with different delays and testing if connectin still works or not. Server part replies to UDP packets based on timeout in incoming UDP packets. Client checks increasing timeouts until it detects missing reply packets, then "bisects" to find out more exact shut-off delay. Range is from 2 to 512 seconds. There may be running public server on vi-server.org:909 port.

```
$ udptimeoutchecker probe 0.0.0.0 0 vi-server.org 909
Trying with timeout 1 seconds...OK
Trying with timeout 2 seconds...OK
Trying with timeout 4 seconds...OK
Trying with timeout 8 seconds...OK
Trying with timeout 16 seconds...OK
Trying with timeout 32 seconds...OK
Trying with timeout 64 seconds...OK
Trying with timeout 128 seconds...FAIL
Trying with timeout 128 seconds...FAIL
Trying with timeout 128 seconds...FAIL
Trying with timeout 96 seconds...FAIL
Trying with timeout 80 seconds...FAIL
Trying with timeout 72 seconds...FAIL
Trying with timeout 68 seconds...FAIL
Trying with timeout 66 seconds...FAIL
Intermediate result: 64
Trying with timeout 1 seconds...OK
Trying with timeout 2 seconds...OK
Trying with timeout 4 seconds...OK
Trying with timeout 8 seconds...OK
Trying with timeout 16 seconds...OK
Trying with timeout 32 seconds...OK
Trying with timeout 64 seconds...OK
Trying with timeout 128 seconds...FAIL
Trying with timeout 128 seconds...FAIL
Trying with timeout 128 seconds...FAIL
Trying with timeout 96 seconds...FAIL
Trying with timeout 80 seconds...FAIL
Trying with timeout 72 seconds...FAIL
Trying with timeout 68 seconds...FAIL
Trying with timeout 66 seconds...FAIL
Intermediate result: 64
Trying with timeout 1 seconds...OK
Trying with timeout 2 seconds...OK
Trying with timeout 4 seconds...OK
Trying with timeout 8 seconds...OK
Trying with timeout 16 seconds...OK
Trying with timeout 32 seconds...OK
Trying with timeout 64 seconds...OK
Trying with timeout 128 seconds...FAIL
Trying with timeout 128 seconds...FAIL
Trying with timeout 128 seconds...FAIL
Trying with timeout 96 seconds...FAIL
Trying with timeout 80 seconds...FAIL
Trying with timeout 72 seconds...FAIL
Trying with timeout 68 seconds...FAIL
Trying with timeout 66 seconds...FAIL
Intermediate result: 64
64
```

# udp_ts_seq_reply

Prepend each UDP packets with 12 bytes of nanosecond timestamp (8 bytes for seconds, 4 for nanoseconds) and 4 bytes of per-client sequence number and send them back.

```
$ ./udp_ts_seq_reply 0.0.0.0 1919&
$ socat -x -v - udp:127.0.0.1:1919 > /dev/null
123
> 2018/02/19 19:52:08.981246  length=4 from=0 to=3
 31 32 33 0a                                      123.
--
< 2018/02/19 19:52:08.981712  length=20 from=0 to=19
 00 00 00 00 00 57 2f 47 09 a8 bf 83 00 00 00 01  .....W/G........
 31 32 33 0a                                      123.
--
ABC
> 2018/02/19 19:52:11.439685  length=4 from=4 to=7
 41 42 43 0a                                      ABC.
--
< 2018/02/19 19:52:11.440078  length=20 from=20 to=39
 00 00 00 00 00 57 2f 49 24 fa c5 ed 00 00 00 02  .....W/I$.......
 41 42 43 0a                                      ABC.
--
DEF
> 2018/02/19 19:52:13.094035  length=4 from=8 to=11
 44 45 46 0a                                      DEF.
--
< 2018/02/19 19:52:13.094455  length=20 from=40 to=59
 00 00 00 00 00 57 2f 4b 10 61 48 68 00 00 00 03  .....W/K.aHh....
 44 45 46 0a                                      DEF.
--

