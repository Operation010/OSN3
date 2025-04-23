EdFS – Assignment 3 (Operating Systems & Networks 2025)
======================================================

This project contains our FUSE 2 implementation of EdFS 1.

Prerequisites
-------------
• FUSE 2.9.x  (package libfuse2 – do NOT use FUSE 3)
• GNU make, gcc (C 99), Python 3
• populated.img  – the reference image from Brightspace

Directory layout

  OSN3/
  ├─ edfs-start/      source + Makefile        (Terminal 1)
  ├─ edfs-utils/      helper programs/scripts  (Terminal 2)
  └─ populated.img    file-system image (not under git)

We mount the file system on /tmp/osn3-mnt.

-----------------------------------------------------------------
1.  TERMINAL 1  –  BUILD AND RUN (stay in edfs-start/)
-----------------------------------------------------------------

  cd ~/OSN3/edfs-start
  make                               # compile

  fusermount -u /tmp/osn3-mnt 2>/dev/null || true
  sudo umount -l /tmp/osn3-mnt 2>/dev/null || true

  mkdir -p /tmp/osn3-mnt
  rm -rf /tmp/osn3-mnt/*             # ensure empty

  ./edfuse -f -s ../populated.img /tmp/osn3-mnt
  (leave this terminal running; Ctrl-C stops the FS)

-----------------------------------------------------------------
2.  TERMINAL 2  –  TEST (stay in ~/OSN3, do NOT unmount here)
-----------------------------------------------------------------

  # quick manual tests
  echo "abc" > /tmp/osn3-mnt/test.txt
  truncate -s 1000 /tmp/osn3-mnt/test.txt
  truncate -s   10 /tmp/osn3-mnt/test.txt
  ls -l /tmp/osn3-mnt/test.txt      # owner root root, size 10

  # full read-test suite
  python3 edfs-utils/testread.py /tmp/osn3-mnt   # prints “done.”

  # overwrite stress test
  for f in file1.txt file2.txt file3.txt file4.txt file5.txt; do
      edfs-utils/overwrite /tmp/osn3-mnt/$f
  done

  # when all testing is done
  fusermount -u /tmp/osn3-mnt

-----------------------------------------------------------------
3.  INTEGRITY CHECK (after unmount)
-----------------------------------------------------------------

  edfs-utils/fsck.edfs populated.img
  → should state “File system check completed successfully.”

-----------------------------------------------------------------
Clean rebuild
-----------------------------------------------------------------

  cd ~/OSN3/edfs-start
  make clean
  make

-----------------------------------------------------------------
Common pitfalls
-----------------------------------------------------------------

* “mountpoint is not empty”          →  rm -rf /tmp/osn3-mnt/*
* “Numerical result out of range”    →  using old binary; unmount, rebuild
* Owner shows hammad hammad          →  you unmounted; writing to host FS
* “bad error value: 16”              →  truncate must return 0 on success
