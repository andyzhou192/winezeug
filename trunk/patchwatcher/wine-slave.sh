#!/bin/sh
# Simple continuous build slave for Wine 
# Copyright 2008 Google (Dan Kegel)
# License: LGPL
#
# Warning:
# This script executes source code sent in by anonymous users,
# and is therefore very dangerous.  Run this in as isolated
# and low-privilege environment as possible.
#
# This script does a baseline build and test of wine, and leaves
# it in a directory called 'active'.
#
# It then watches a directory 'shared/slave' for incoming jobs.
# whenever it finds one, it moves 'active' aside and replaces it with a copy.
# When it's done trying out the patch, it creates a log file
# inside the job directory to signal the master to collect the
# results.
# When the job is done, it deletes the copy of 'active',
# replaces it with the original, and goes back to wait for 
# another patch.

. `dirname $0`/libpatchwatcher.sh
lpw_init `dirname $0`

set -e
set -x

# Set this to true on first run and after debugging
initialize=false

WORK="`pwd`/wine-continuous-workdir"

WINE=$WORK/active/wine
WINESERVER=$WORK/active/server/wineserver
WINEPREFIX=$HOME/.wine
# export so we can invoke winetricks
export WINE WINEPREFIX

# Builds wine, returns nonzero status if anything goes wrong
# Saves log in file $1
# $2 is -j3 for fast build, blank for clean log
build_wine()
{
    log=$1
    parallel=$2

    # Regenerate everything in case patch adds a new dll or changes configure.ac
    tools/make_makefiles && 
    autoconf && 
    ./configure && 
    make depend && 
    make $parallel 2>&1 | perl "$LPW_BIN/trim-build-log.pl" > $log &&
    grep -q "^Wine build complete" $log 
}

# Given a clean tree, gather list of tests that fail at least once in N runs
baseline_tests()
{
    # Once this script is debugged, crank up the number of runs a bit here
    cd $WORK/active
    for try in 1 # 2 3 4 5
    do
        make testclean
        $WINESERVER -k || true
        rm -rf $WINEPREFIX || true
        sh "$LPW_BIN/../winetricks" gecko
        WINETEST_WRAPPER="$TOP/alarm 150" make -k test || true
    done > flaky.log 2>&1

    perl "$LPW_BIN/get-dll.pl" < flaky.log | egrep ": Test failed: |: Test succeeded inside todo block: " | sort -u | egrep -v $blacklist_regex > flaky.dat || true
    # Record for posterity
    cp flaky.log $WORK/baseline.testlog
    cp flaky.dat $WORK/baseline.testdat
}

# Given an updated tree, run the tests again.
# Output log to stdout.
# Output "Patchwatcher:ok" if no new tests fail relative to baseline.
retest_wine()
{
    make testclean > /dev/null
    $WINESERVER -k > /dev/null || true
    rm -rf $WINEPREFIX || true
    sh "$LPW_BIN/../winetricks" gecko > /dev/null
    WINETEST_WRAPPER="$TOP/alarm 150" make -k test > $thepatch.testlog 2>&1 || true
    perl "$LPW_BIN/get-dll.pl" < $thepatch.testlog | egrep ": Test failed: |: Test succeeded inside todo block: " | sort -u | egrep -v `cat "$LPW_BIN/blacklist.txt"` > $thepatch.testdat || true
    cat $thepatch.testlog
    echo "Regression test changes vs. baseline test runs:"
    diff flaky.dat $thepatch.testdat || true
    # Report failure if any new errors
    diff flaky.dat $thepatch.testdat > $thepatch.testdiff || true
    if grep -q '^> ' < $thepatch.testdiff
    then
        echo "Ditto, but just the new errors:"
        grep '^> ' < $thepatch.testdiff | sed 's/^>//' || true
    else
        echo "Patchwatcher:ok"
    fi
}

initialize_tree()
{
    test -x $TOP/alarm || gcc $TOP/alarm.c -o $TOP/alarm

    rm -rf $WORK
    mkdir -p $WORK
    cd $WORK
    git clone git://source.winehq.org/git/wine.git active
    cd active
    build_wine baseline.log -j3
    baseline_tests
}

refresh_tree()
{
    cd $WORK/active
    # Recover from any accidental damage
    git diff | grep -v '^diff --git' > git.diff && patch -R -p1 < git.diff
    # Grab latest source
    git pull > git.log 2>&1
    cat git.log
    if ! grep -q "Already up-to-date." < git.log
    then
       build_wine baseline.log -j3
       baseline_tests
    fi
}

try_one_patch()
{
    thepatch=$1

    cd $WORK/active
    # Should we use -p1 or -p0?
    # CVS patches need -p0, git patches need -p1
    # For now, always use -p0 unless it's obvious patch was
    # generated with cvs or svn
    if egrep -q 'RCS file|^+++.*working copy' < $thepatch.patch
    then
        p=0
    else
        p=1
    fi

    patch -p$p < $thepatch.patch > $thepatch.log 2>&1 &&
      build_wine $thepatch.log &&
      retest_wine > $thepatch.log
}

# Return true if a patch was tried, false if no patches left to try
try_one_job()
{
    rm -rf $WORK/golden
    mv $WORK/active $WORK/golden
    cp -a $WORK/golden $WORK/active

    patchnum=1
    jobdir=$LPW_SHARED/slave/$LPW_JOB
    while test -f $jobdir/$patchnum.patch
    do
        try_one_patch $jobdir/$patchnum
        patchnum=`expr $patchnum + 1`
    done

    lpw_summarize_job slave $LPW_JOB

    cd $WORK
    rm -rf active
    mv golden active
    return 0
}

continuous_build_slave()
{
    if $initialize
    then
        initialize_tree
    else
        # Recover from run aborted with ^C
        if test -d $WORK/golden
        then
            rm -rf $WORK/active
            mv $WORK/golden $WORK/active
        fi
    fi
    while true
    do
       if lpw_lowest_job slave
       then
           try_one_job $LPW_JOB
       fi
       sleep 5
    done
}

main()
{
    set -x
    case "$1" in
    "")
       set +x
       echo "usage: $0 cmd [arg]"
       echo "Commands: init, job NNN, run"
       echo "Or any libpatchwatcher demo shell command"
       ;;

    init) initialize_tree ;;

    job) LPW_JOB=$2; try_one_job ;;

    run) continuous_build_slave ;;

    *) demo_shell "$@" ;;
    esac
}

main "$@"
