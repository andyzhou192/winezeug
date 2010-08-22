/* Wrapper to reliably execute a Wine conformance test
 * inside an automated test system.
 *
 * Usage:
 *  alarm NNN runtest foo_test.exe foo.c
 *
 * Runs the given test, with the following twists:
 *
 * 1. If the test takes longer than NNN seconds, forcibly
 * kills the test, and prints the message
 *  alarm: Timeout!  Killing child.
 * and exits with status 1.
 * (See e.g. http://bugs.winehq.org/show_bug.cgi?id=15958 )
 *
 * If the test crashes, prints the message
 *  alarm: Terminated abnormally.
 *
 * 2. If the test's name is not on the whitelist (see below),
 * compares the screens graphics mode before and after the test.
 * If they are not identical, prints the message
 *  alarm: video mode changed!  Was %d, now %d.
 * and exits with status 1.
 * (See http://bugs.winehq.org/show_bug.cgi?id=15956 )
 *
 * If the test is being run inside "make -jN":
 *
 * 3. The test's stdout and stderr are saved to a temporary file,
 * and when the test is done, the test ID the
 * temporary file's contents are output all at once.
 * This helps prevent interdigitating of results from tests being 
 * run in parallel.
 *
 * 4. If the test's name is not in the whitelist,
 * takes an exclusive lock on the file "alarm.lock" while the test executes.  
 * This helps prevent spurious test failures due to tests being
 * run in parallel.
 *
 * The whitelist referred to above is a list of tests known to
 * run well in parallel, e.g. that don't do any of the following things:
 *  mess with the screen's video mode
 *  depend on the location of the cursor
 *  send events via the cursor
 *  bind to a fixed port number
 *
 * Copyright 2008, Google (Dan Kegel)
 * License: LGPL
 */

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>

pid_t child_pid;

static void handler(int x)
{
    fprintf(stderr, "alarm: Timeout!  Killing child.\n");
    /* TODO: This probably won't kill grandchildren.  Do we want to create a process group? */
    kill(child_pid, SIGKILL);
    exit(1);
}

/* a real implementation would not have a limit on commandline length */
#define MAXARGS 1000

/* Extract test id (module:filename) from commandline, return true if ok */
static int getTestID(int argc, char **argv, char *buf)
{
    int i;
    const char *module, *module_end;
    const char *file, *file_end;
    char *p = buf;
    int argi;

    for (argi=1; argi < argc; argi++)
        if (strstr(argv[argi], "wine"))
            break;
    if (argi + 2 >= argc)
        return 0;

    module = argv[argi+1];
    if (!module) return 0;
    module_end = strrchr(module, '_');
    if (!module_end) return 0;

    file = argv[argi+2];
    if (!file) return 0;
    file_end = strrchr(file, '.');
    if (!file_end) return 0;

    while (module != module_end)
      *p++ = *module++;
    *p++ = ':';
    while (file != file_end)
      *p++ = *file++;
    *p = 0;
    return 1;
}

/*
 * Whitelist generated by doing "make -j50 -k test" and then
 *  find dlls -name '*.ok' | sort | sed 's,dlls/,,;s,/tests/,:,;s,.ok$,,;s/^/"/;s/$/",/'
 * TODO: Update it as we learn about the test suite, and as it changes.
 */
static const char *whitelist[] = {
    "advapi32:cred",
    "advapi32:crypt",
    "advapi32:crypt_lmhash",
    "advapi32:crypt_md4",
    "advapi32:crypt_md5",
    "advapi32:crypt_sha",
    "advapi32:lsa",
    "advapi32:registry",
    "advapi32:security",
    "advapi32:service",
    "advpack:advpack",
    "advpack:files",
    "comctl32:misc",
    "comctl32:mru",
    "comdlg32:printdlg",
    "credui:credui",
    "crypt32:base64",
    "crypt32:cert",
    "crypt32:chain",
    "crypt32:crl",
    "crypt32:ctl",
    "crypt32:encode",
    "crypt32:main",
    "crypt32:message",
    "crypt32:oid",
    "crypt32:protectdata",
    "crypt32:sip",
    "crypt32:store",
    "crypt32:str",
    "cryptnet:cryptnet",
    "cryptui:cryptui",
    "d3drm:vector",
    "d3dx8:math",
    "d3dx9_36:math",
    "d3dxof:d3dxof",
    "dnsapi:name",
    "dnsapi:record",
    "dplayx:dplayx",
    "fusion:asmcache",
    "fusion:asmname",
    "fusion:fusion",
    "gdi32:bitmap",
    "gdi32:brush",
    "gdi32:gdiobj",
    "gdi32:generated",
    "gdi32:icm",
    "gdi32:mapping",
    "gdi32:palette",
    "gdi32:path",
    "gdi32:pen",
    "gdiplus:brush",
    "gdiplus:customlinecap",
    "gdiplus:graphics",
    "gdiplus:graphicspath",
    "gdiplus:matrix",
    "gdiplus:pathiterator",
    "gdiplus:pen",
    "gdiplus:region",
    "gdiplus:stringformat",
    "inetmib1:main",
    "iphlpapi:iphlpapi",
    "kernel32:actctx",
    "kernel32:alloc",
    "kernel32:atom",
    "kernel32:change",
    "kernel32:codepage",
    "kernel32:comm",
    "kernel32:debugger",
    "kernel32:directory",
    "kernel32:drive",
    "kernel32:environ",
    "kernel32:file",
    "kernel32:format_msg",
    "kernel32:generated",
    "kernel32:heap",
    "kernel32:loader",
    "kernel32:locale",
    "kernel32:mailslot",
    "kernel32:module",
    "kernel32:path",
    "kernel32:pipe",
    "kernel32:profile",
    "kernel32:resource",
    "kernel32:sync",
    "kernel32:time",
    "kernel32:timer",
    "kernel32:toolhelp",
    "kernel32:version",
    "kernel32:virtual",
    "kernel32:volume",
    "localspl:localmon",
    "localui:localui",
    "lz32:lzexpand_main",
    "mapi32:imalloc",
    "mapi32:prop",
    "mapi32:util",
    "msacm32:msacm",
    "mscms:profile",
    "msvcrt:cpp",
    "msvcrt:data",
    "msvcrtd:debug",
    "msvcrt:dir",
    "msvcrt:environ",
    "msvcrt:file",
    "msvcrt:headers",
    "msvcrt:heap",
    "msvcrt:printf",
    "msvcrt:scanf",
    "msvcrt:string",
    "msvcrt:time",
    "netapi32:access",
    "netapi32:apibuf",
    "netapi32:ds",
    "netapi32:wksta",
    "ntdll:atom",
    "ntdll:change",
    "ntdll:env",
    "ntdll:error",
    "ntdll:exception",
    "ntdll:file",
    "ntdll:generated",
    "ntdll:info",
    "ntdll:large_int",
    "ntdll:om",
    "ntdll:path",
    "ntdll:port",
    "ntdll:reg",
    "ntdll:rtl",
    "ntdll:rtlbitmap",
    "ntdll:rtlstr",
    "ntdll:string",
    "ntdll:time",
    "ntdsapi:ntdsapi",
    "ntprint:ntprint",
    "odbccp32:misc",
    "ole32:errorinfo",
    "ole32:hglobalstream",
    "ole32:propvariant",
    "ole32:stg_prop",
    "ole32:storage32",
    "oleacc:main",
    "oleaut32:olefont",
    "oleaut32:olepicture",
    "oleaut32:safearray",
    "oleaut32:typelib",
    "oleaut32:varformat",
    "oleaut32:vartest",
    "oleaut32:vartype",
    "pdh:pdh",
    "psapi:psapi_main",
    "rasapi32:rasapi",
    "rpcrt4:generated",
    "rpcrt4:ndr_marshall",
    "rpcrt4:rpc_async",
    "rpcrt4:server",
    "rsaenh:rsaenh",
    "schannel:main",
    "secur32:main",
    "secur32:ntlm",
    "secur32:schannel",
    "secur32:secur32",
    "serialui:confdlg",
    "setupapi:devinst",
    "setupapi:parser",
    "setupapi:query",
    "setupapi:stringtable",
    "shdocvw:shdocvw",
    "shell32:generated",
    "shell32:shfldr_special",
    "shell32:shlfileop",
    "shlwapi:assoc",
    "shlwapi:clist",
    "shlwapi:clsid",
    "shlwapi:generated",
    "shlwapi:istream",
    "shlwapi:ordinal",
    "shlwapi:path",
    "shlwapi:url",
    "snmpapi:util",
    "spoolss:spoolss",
    "urlmon:generated",
    "user32:generated",
    "user32:resource",
    "user32:wsprintf",
    "userenv:userenv",
    "version:info",
    "version:install",
    "winhttp:notification",
    "winhttp:url",
    "winhttp:winhttp",
    "wininet:generated",
    "wininet:http",
    "wininet:internet",
    "wininet:url",
    "wininet:urlcache",
    "winmm:capture",
    "winmm:mixer",
    "winmm:mmio",
    "winmm:timer",
    "winmm:wave",
    "winspool.drv:info",
    "wintrust:asn",
    "wintrust:crypt",
    "wintrust:register",
    "wintrust:softpub",
    "wldap32:parse",
    "ws2_32:protocol",
    "ws2_32:sock",

};
static const size_t whitelist_len = sizeof(whitelist) / sizeof(whitelist[0]);

/* this isn't quite legal, but it works */
static int mystrcmp(const char *a, const char **bp)
{
    const char *b = *bp;
    return strcmp(a, b);
}

static int inWhitelist(const char *testid)
{
    typedef int (*compar)(const void *, const void *);

    return bsearch(testid, whitelist, whitelist_len, 
         sizeof(whitelist[0]), (compar)mystrcmp) != NULL;
}

static int isParallelRun(void)
{
    char *makeflags = getenv("MAKEFLAGS");
    return (makeflags != NULL) && (strstr(makeflags, "jobserver") != NULL);
}

static int getVideoMode(void)
{
    FILE *xrandr = popen("xrandr -q | grep -n '\\*'", "r");
    char buf[256];
    char *p;

    if (!xrandr)
        return 0;

    p = fgets(buf, sizeof(buf), xrandr);
    fclose(xrandr);

    if (!p)
        return 0;

    return atoi(buf) - 2;  /* skip two lines at top */
}


int main(int argc, char **argv)
{
    char *newargv[MAXARGS];
    int newargc;
    int timeout;
    pid_t pid;
    int i;
    int waitresult;
    int ret;
    char testid[64];
    int lockfd, logfd;
    char logfilename[1024];
    int notWhitelisted;
    int origVideoMode;

    if (argc < 3) {
        fprintf(stderr, "Usage: alarm timeout-in-seconds command ...\n");
        exit(1);
    }
    timeout = atoi(argv[1]);
    if (timeout < 1) {
        fprintf(stderr, "Timeout must be positive, was %s\n", argv[1]);
        exit(1);
    }

    /* Prepare new argv with timeout removed. */
    for (newargc=0; newargc < argc-2 && newargc < MAXARGS-1; newargc++)
        newargv[newargc] = argv[newargc+2];
    newargv[newargc] = NULL;
    notWhitelisted = getTestID(newargc,newargv,testid) && !inWhitelist(testid);

    origVideoMode = 0;
    if (notWhitelisted)
        origVideoMode = getVideoMode();

    lockfd = -1;
    logfd = -1;
    if (isParallelRun()) {
        /* Get an exclusive lock, if needed */
        if (notWhitelisted) {
            lockfd = open("alarm.lock", O_RDWR|O_CREAT, 0600);
            if (lockfd != -1) 
               flock(lockfd, LOCK_EX);
        }

	/* Redirect child's output and stderr to a temporary file ... */
	sprintf(logfilename, "%s.tmplog", testid);
        logfd = open(logfilename, O_RDWR|O_CREAT, 0660);
    }

    /* Run test */
    child_pid = fork();
    if (child_pid == 0) {
        /* child */
        if (logfd > -1) {
	    /* Finish redirecting */
            dup2(logfd, 1);
            dup2(logfd, 2);
        }
        /* TODO: Do we want to make this its own process group for ease of killing grandchildren? */
        execvp(newargv[0], newargv);
        /* notreached */
        perror(newargv[0]);
        exit(1);
    }

    /* Wait timeout seconds for it to finish */
    signal(SIGALRM, handler);
    alarm(timeout);
    waitresult = 0;
    while ((ret = wait(&waitresult)) == -1 && errno == EINTR)
        ;

    if (logfd != -1 && lseek(logfd, 0, SEEK_CUR) != 0) {
#define MYBUFLEN 128000
	static char buf[MYBUFLEN];
	int n;

        /* Get an exclusive lock so logs don't get mixed together */
	int loglockfd = open("log.lock", O_RDWR|O_CREAT, 0600);
	if (loglockfd != -1) 
	   flock(loglockfd, LOCK_EX);

        /* Print the test id, so log postprocessors
         * know what test this log is for.
         * (In non-parallel runs, make outputs the command nicely,
         * but in parallel runs, that's too far back, some other
         * test's commandline might have been printed in the meantime.)
         */
        n = sprintf(buf, "alarm: runtest %s log:\n", testid);
        write(1, buf, n);

	/* Copy the temporary file to stdout */
        lseek(logfd, 0, SEEK_SET);
	while ((n = read(logfd, buf, MYBUFLEN)) > 0)
	    write(1, buf, n);
	close(logfd);

	remove(logfilename);

        n = sprintf(buf, "alarm: log end\n");
        write(1, buf, n);

	if (loglockfd != -1) 
	   flock(loglockfd, LOCK_UN);
    }

    /* Release exclusive lock, if taken */
    if (lockfd != -1)
        flock(lockfd, LOCK_UN);

    /* Report crashes */
    if (!WIFEXITED(waitresult)) {
        printf("alarm: Terminated abnormally\n");
        exit(99);
    }

    /* Verify video mode was restored */
    if (notWhitelisted) {
        int videoMode = getVideoMode();
        if (videoMode != origVideoMode) {
	    printf("alarm: video mode changed! was %d, now %d\n",
                   origVideoMode, videoMode);
            /* and restore */
            system("xrandr -s 0");
            exit(1);
        }
    }

    /* Finally, exit with test program's exit code */
    return WEXITSTATUS(waitresult);
}