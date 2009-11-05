/*
 * ninefs.c
 *  Dokan-based 9p filesystem for windows.
 *
 * TODO:
 *  - better utf8 conversion
 *  - better 8.3 support
 *  - review string ops to make sure we're ok for utf8 everywhere
 *  - make good win32 error codes
 *  - investigate user security.  right now all files appear to be
 *    owned by the user mounting the filesystem.  Is this a dokan limitation?
 *  - auth support for p9sk1 and perhaps others
 *  - ssl support
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "npfs.h"
#include "npclient.h"
#include "dokan.h"

static Npuser *user = NULL;
static Npcfsys *fs = NULL;
static int debug = 0;

static int optind = 1;
static int optpos = 0;
static char *optarg = NULL;

static int
getopt(int argc, char **argv, const char *opts)
{
    char *p, ch;

    if(optind >= argc || !argv[optind])
        return -1;
    if(optpos && !argv[optind][optpos]) {
        optind ++;
        optpos = 0;
    }
    if(optind >= argc || !argv[optind])
        return -1;
    if(optpos == 0 && argv[optind][optpos++] != '-')
        return -1;
    ch = argv[optind][optpos++];
    p = strchr(opts, ch);
    if(!p)
        return '?';
    if(p[1] != ':')
        return ch;

    optarg = argv[optind++] + optpos;
    optpos = 0;
    if(*optarg)
        return ch;
    if(optind >= argc || !argv[optind])
        return '?';
    optarg = argv[optind++];
    return ch;
}

static int
notyet(char *msg) {
    fprintf(stderr, "notyet: %s\n", msg);
    fflush(stderr);
    return -(int)ERROR_CALL_NOT_IMPLEMENTED;
}

// Return a dynamically allocated utf8 string.
static char *
utf8(LPCWSTR ws)
{
    char *s;
    UINT i;

    if(!ws)
        ws = L"";

    // XXX quick hack for now, fixme.
    for(i = 0; ws[i]; i++)
        continue;
    i++;
    s = malloc(i);
    if(!s) {
        if(debug)
            fprintf(stderr, "utf8 malloc failed\n");
        return NULL;
    }
    for(i = 0; ws[i]; i++)
        s[i] = (char)ws[i];
    s[i] = 0;
    return s;
}

static char*
utf8path(LPCWSTR ws)
{
    char *fn = utf8(ws);
    int i;

    if(fn) {
        for(i = 0; fn[i]; i++) {
            if(fn[i] == '\\')
                fn[i] = '/';
        }
    }
    return fn;
}

static void
maybeOpen(LPCWSTR fname, int omode, int *opened, Npcfid **fidp)
{
    char *fn;

    *opened = 0;
    if(*fidp)
        return;
    fn = utf8path(fname);
    *fidp = npc_open(fs, fn, omode);
    if(*fidp)
        *opened = 1;
    free(fn);
}

static void
maybeClose(int *opened, Npcfid **fidp)
{
    if(*opened)
        npc_close(*fidp);
}

static u32
fromFT(const FILETIME *f)
{
    LONGLONG dt = f->dwLowDateTime | ((LONGLONG)f->dwHighDateTime << 32);
    return (u32)((dt - 116444736000000000) / 10000000);
}

static FILETIME
toFT(u32 ut)
{
    FILETIME f;
    LONGLONG dt;

    dt = Int32x32To64(ut, 10000000) + 116444736000000000;
    f.dwLowDateTime = (DWORD)dt;
    f.dwHighDateTime = (DWORD)(dt >> 32);
    return f;
}

static FILETIME zt = { 0, 0 };

static void
toFileInfo(Npwstat *st, LPBY_HANDLE_FILE_INFORMATION fi)
{
    fi->dwFileAttributes = 0;
    if(st->qid.type & Qtdir)
        fi->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
    else
        fi->dwFileAttributes |= FILE_ATTRIBUTE_NORMAL;
    fi->ftCreationTime = zt;
    fi->ftLastAccessTime = toFT(st->atime);
    fi->ftLastWriteTime = toFT(st->mtime);
    fi->dwVolumeSerialNumber = st->dev;
    fi->nFileSizeHigh = (DWORD)(st->length >> 32);
    fi->nFileSizeLow = (DWORD)st->length;
    fi->nNumberOfLinks = 1;
    fi->nFileIndexHigh = (DWORD)(st->qid.path >> 32);
    fi->nFileIndexLow = (DWORD)st->qid.path;
}

static void
toFindData(Npwstat *st, WIN32_FIND_DATA *fd)
{
    int i, j;

    fd->dwFileAttributes = 0;
    if(st->qid.type & Qtdir)
        fd->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
    else
        fd->dwFileAttributes |= FILE_ATTRIBUTE_NORMAL;
    fd->ftCreationTime = zt;
    fd->ftLastAccessTime = toFT(st->atime);
    fd->ftLastWriteTime = toFT(st->mtime);
    fd->nFileSizeHigh = (DWORD)(st->length >> 32);
    fd->nFileSizeLow = (DWORD)st->length;
    fd->dwReserved0 = 0;
    fd->dwReserved1 = 0;

    // XXX quick hack -- need better utf8->utf16 conversion here
    for(i = 0; i < MAX_PATH-1 && st->name[i]; i++)
        fd->cFileName[i] = st->name[i];
    fd->cFileName[i] = 0;

    // XXX this is a really bad hack:
    for(j = 0, i = 0 ; j < 13 && st->name[i]; i++) {
        if(j == 8)
            fd->cAlternateFileName[j++] = '.';
        if(st->name[i] != '.')
            fd->cAlternateFileName[j++] = st->name[i];
    }
    fd->cAlternateFileName[j] = 0;
}

static int
cvtError(void)
{
    // XXX get npfs error and try to translate it into  
    // some meaningful win32 error code and return the negative
    return -(int)ERROR_INVALID_PARAMETER; // XXX bogus
}



static int
_CreateFile(
    LPCWSTR                 FileName,
    DWORD                   AccessMode,
    DWORD                   ShareMode,
    DWORD                   CreationDisposition,
    DWORD                   FlagsAndAttributes,
    PDOKAN_FILE_INFO        DokanFileInfo)
{
    Npcfid *fid = NULL;
    char *fn;
    int omode, rd, wr;

    if(debug)
        fprintf(stderr, "createfile '%ws' create %d access %x\n", FileName, CreationDisposition, AccessMode);
    rd = (AccessMode & (GENERIC_READ | FILE_READ_DATA)) != 0;
    wr = (AccessMode & (GENERIC_WRITE | FILE_WRITE_DATA)) != 0;
    if(rd && wr)
        omode = Ordwr;
    else if(wr)
        omode = Owrite;
    else
        omode = Oread;
    if(CreationDisposition == TRUNCATE_EXISTING)
        omode |= Otrunc;

    fn = utf8path(FileName);
    if(!fn)
        return -(int)ERROR_NOT_ENOUGH_MEMORY;
    fid = npc_open(fs, fn, omode);
    if(!fid && (CreationDisposition == CREATE_ALWAYS || 
                CreationDisposition == CREATE_NEW ||
                CreationDisposition == OPEN_ALWAYS)) {
        fid = npc_create(fs, fn, 0666, omode);
    }
    free(fn);
    if(!fid) {
        if(debug)
            fprintf(stderr, "open %ws failed\n", FileName);
        return cvtError();
    }
    DokanFileInfo->Context = (ULONG64)fid;
    return 0;
}

static int
_CreateDirectory(
    LPCWSTR                 FileName,
    PDOKAN_FILE_INFO        DokanFileInfo)
{
    Npcfid *fid;
    char *fn;
    int perm;

    if(debug)
        fprintf(stderr, "create directory '%ws'\n", FileName);
    fn = utf8path(FileName);
    perm = Dmdir | 0777; // XXX figure out perm
    fid = npc_create(fs, fn, perm, Oread);
    if(fid)
        npc_close(fid);
    free(fn);
    if(!fid) {
        if(debug)
            fprintf(stderr, "create directory %ws failed\n", FileName);
        return cvtError();
    }
    return 0;
}

static int
_OpenDirectory(
    LPCWSTR                 FileName,
    PDOKAN_FILE_INFO        DokanFileInfo)
{
    Npcfid *fid = NULL;
    char *fn;
    int e;

    if(debug)
        fprintf(stderr, "open directory '%ws'\n", FileName);
    fn = utf8path(FileName);
    if(!fn)
        return -(int)ERROR_NOT_ENOUGH_MEMORY;

    e = 0;
    fid = npc_open(fs, fn, Oread);
    if(fid && !(fid->qid.type & Qtdir)) {
        npc_close(fid);
        fid = NULL;
        e = -(int)ERROR_DIRECTORY; // XXX?
    } else if(!fid) {
        e = cvtError();
    }
    free(fn);

    if(e) {
        if(debug)
            fprintf(stderr, "diropen %ws failed\n", FileName);
        return e;
    }
    DokanFileInfo->Context = (ULONG64)fid;
    return 0;
}

static int
_CloseFile(
    LPCWSTR                 FileName,
    PDOKAN_FILE_INFO        DokanFileInfo)
{
    Npcfid *fid = (Npcfid *)DokanFileInfo->Context;

    if(fid) {
        DokanFileInfo->Context = 0;
        npc_close(fid);
    }
    return 0;
}

static int
_Cleanup(
    LPCWSTR                 FileName,
    PDOKAN_FILE_INFO        DokanFileInfo)
{
    // XXX handle delete-on-close?
    return _CloseFile(FileName, DokanFileInfo);
}

static int
_ReadFile(
    LPCWSTR             FileName,
    LPVOID              Buffer,
    DWORD               BufferLength,
    LPDWORD             ReadLength,
    LONGLONG            Offset,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    Npcfid *fid = (Npcfid *)DokanFileInfo->Context;
    int e, r, opened;

    if(debug)
        fprintf(stderr, "readfile\n");
    maybeOpen(FileName, Oread, &opened, &fid);
    if(!fid)
        return cvtError();
    e = 0;
    r = npc_read(fid, Buffer, BufferLength, Offset);
    if(r < 0)
        e = cvtError();
    maybeClose(&opened, &fid);
    if(e) {
        if(debug)
            fprintf(stderr, "readfile error\n");
        return e;
    }
    *ReadLength = r;
    return 0;
}

static int
_WriteFile(
    LPCWSTR     FileName,
    LPCVOID     Buffer,
    DWORD       NumberOfBytesToWrite,
    LPDWORD     NumberOfBytesWritten,
    LONGLONG            Offset,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    Npcfid *fid = (Npcfid *)DokanFileInfo->Context;
    int e, r, opened;

    if(debug)
        fprintf(stderr, "writefile\n");
    maybeOpen(FileName, Owrite, &opened, &fid);
    if(!fid)
        return cvtError();
    e = 0;
    r = npc_write(fid, (u8*)Buffer, NumberOfBytesToWrite, Offset);
    if(r < 0)
        e = cvtError();
    maybeClose(&opened, &fid);
    if(e) {
        if(debug)
            fprintf(stderr, "writefile error\n");
        return e;
    }
    *NumberOfBytesWritten = r;
    return 0;
}

static int
_FlushFileBuffers(
    LPCWSTR     FileName,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    Npwstat st;
    char *fn;
    int e, r;

    if(debug)
        fprintf(stderr, "flushfilebuffers '%ws'\n", FileName);
    fn = utf8path(FileName);
    npc_emptystat(&st);
    e = 0;
    r = npc_wstat(fs, fn, &st);
    if(r < 0)
        e = cvtError();
    free(fn);
    if(e) {
        if(debug)
            fprintf(stderr, "flushfilebuffers error\n");
        return e;
    }
    return 0;
}

static int
_GetFileInformation(
    LPCWSTR                         FileName,
    LPBY_HANDLE_FILE_INFORMATION    fi,
    PDOKAN_FILE_INFO                DokanFileInfo)
{
    Npwstat *st = NULL;
    char *fn;
    int e;

    if(debug)
        fprintf(stderr, "getfileinfo '%ws'\n", FileName);
    fn = utf8path(FileName);
    if(!fn)
        return -(int)ERROR_NOT_ENOUGH_MEMORY;
    e = 0;
    st = npc_stat(fs, fn);
    if(st) {
        toFileInfo(st, fi);
        free(st);
    } else {
        e = cvtError();
    }
    free(fn);
    if(e) {
        if(debug)
            fprintf(stderr, "getfileinfo error\n");
        return e;
    }
    return 0;
}

static int
_FindFiles(
    LPCWSTR             FileName,
    PFillFindData       FillFindData, // function pointer
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    Npwstat *st;
    WIN32_FIND_DATA findData;
    Npcfid *fid = NULL;
    char *fn;
    int cnt, i, e;

    if(debug)
        fprintf(stderr, "findfiles '%ws'\n", FileName);
    fn = utf8path(FileName);
    if(!fn)
        return -(int)ERROR_NOT_ENOUGH_MEMORY;
    e = 0;
    fid = npc_open(fs, fn, Oread);
    if(!fid)
        e = cvtError();
    while(fid) {
        cnt = npc_dirread(fid, &st);
        if(cnt == 0)
            break;
        if(cnt < 0) {
            e = cvtError();
            break;
        }
        for(i = 0; i < cnt; i++) {
            if(!st[i].name[0])
                continue;
            toFindData(&st[i], &findData);
            FillFindData(&findData, DokanFileInfo);
        }
    }
    if(fid)
        npc_close(fid);
    free(fn);
    if(e) {
        if(debug)
            fprintf(stderr, "findfiles failed\n");
        return e;
    }
    return 0;
}

static int
_DeleteFile(
    LPCWSTR             FileName,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    char *fn;
    int r;

    if(debug)
        fprintf(stderr, "deletefile %ws\n", FileName);
    fn = utf8path(FileName);
    if(!fn)
        return -(int)ERROR_NOT_ENOUGH_MEMORY;
    r = npc_remove(fs, fn);
    free(fn);
    if(r < 0) {
        if(debug)
            fprintf(stderr, "deletefile %ws failed\n", FileName);
        return cvtError();
    }
    return 0;
}

static int
_DeleteDirectory(
    LPCWSTR             FileName,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    return _DeleteFile(FileName, DokanFileInfo);
}

static int
_MoveFile(
    LPCWSTR             FileName, // existing file name
    LPCWSTR             NewFileName,
    BOOL                ReplaceIfExisting,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    Npwstat st;
    char *fn, *fn2, *p, *newname;
    int dirlen, r, e;

    e = 0;
    if(debug)
        fprintf(stderr, "move %ws to %ws\n", FileName, NewFileName);
    fn = utf8path(FileName);
    fn2 = utf8path(NewFileName);
    if(!fn || !fn2) {
        e = -(int)ERROR_NOT_ENOUGH_MEMORY;
        goto err;
    }

    p = strrchr(fn, '/');
    if(p) {
        dirlen = p - fn;
        newname = fn2 + dirlen + 1;
    } else {
        dirlen = 0;
        newname = fn2;
    }
    // same directory?
    if(strncmp(fn, fn2, dirlen) != 0 || strchr(newname, '/') != NULL) {
        // XXX better error?  cant move files between directories...
        e = -(int)ERROR_NOT_SAME_DEVICE;
        goto err;
    }

    npc_emptystat(&st);
    st.name = newname;
    r = npc_wstat(fs, fn, &st);
    if(r < 0) {
        e = cvtError();
        goto err; 
    }
    /* fall through */

err:
    if(fn)
        free(fn);
    if(fn2)
        free(fn2);
    if(e) {
        if(debug)
            fprintf(stderr, "move failed\n");
        return e;
    }
    return 0;
}

static int
_LockFile(
    LPCWSTR             FileName,
    LONGLONG            ByteOffset,
    LONGLONG            Length,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    // always fail; we dont do locking.
    return -(int)ERROR_NOT_SUPPORTED;
}

static int
_SetEndOfFile(
    LPCWSTR             FileName,
    LONGLONG            ByteOffset,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    Npwstat st;
    char *fn;
    int r;

    fn = utf8path(FileName);
    if(!fn)
        return -(int)ERROR_NOT_ENOUGH_MEMORY;
    npc_emptystat(&st);
    st.length = ByteOffset;
    r = npc_wstat(fs, fn, &st);
    free(fn);
    if(r < 0)
        return cvtError();
    return 0;
}

static int
_SetAllocationSize(
    LPCWSTR             FileName,
    LONGLONG            AllocSize,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    // XXX what exactly is this?
    return notyet("SetAllocationSize");
}

static int
_SetFileAttributes(
    LPCWSTR             FileName,
    DWORD               FileAttributes,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    if(debug)
        fprintf(stderr, "setfileattributes '%ws' %x\n", FileName, FileAttributes);
    FileAttributes &= ~FILE_ATTRIBUTE_NORMAL;
    if(FileAttributes) {
        if(debug)
            fprintf(stderr, "setfileattributes error (unsupported bits)\n");
        return -(int)ERROR_NOT_SUPPORTED;
    }
    return 0;
}


static int
_SetFileTime(
    LPCWSTR             FileName,
    CONST FILETIME*     CreationTime,
    CONST FILETIME*     LastAccessTime,
    CONST FILETIME*     LastWriteTime,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    Npwstat st;
    char *fn;
    int r;

    if(!LastAccessTime && !LastWriteTime)
        return 0;
    fn = utf8path(FileName);
    if(!fn)
        return -(int)ERROR_NOT_ENOUGH_MEMORY;
    npc_emptystat(&st);
    if(LastAccessTime)
        st.atime = fromFT(LastAccessTime);
    if(LastWriteTime)
        st.mtime = fromFT(LastWriteTime);
    r = npc_wstat(fs, fn, &st);
    free(fn);
    if(r < 0)
        return cvtError();
    return 0;
}

static int
_UnlockFile(
    LPCWSTR             FileName,
    LONGLONG            ByteOffset,
    LONGLONG            Length,
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    // always fail; we dont do locking.
    return -(int)ERROR_NOT_SUPPORTED;
}

static int
_Unmount(
    PDOKAN_FILE_INFO    DokanFileInfo)
{
    if(debug)
        fprintf(stderr, "unmount\n");
    npc_umount(fs);
    fs = NULL;
    return 0;
}

static void
usage(char *prog)
{
    fprintf(stderr, "usage:  %s [-cdD] [-u user] addr driveletter\n", prog);
    fprintf(stderr, "\taddress may be of form tcp!hostname!port\n");
    _exit(1);
}

int __cdecl
main(int argc, char **argv)
{
    extern int npc_chatty;
    DOKAN_OPERATIONS ops;
    DOKAN_OPTIONS opt;
    WSADATA wsData;
    char *serv, *uname, *prog;
    int x, ch;
    char letter;

    WSAStartup(MAKEWORD(2,2), &wsData);
    memset(&opt, 0, sizeof opt);

    uname = "nobody";
    prog = argv[0];
    while((ch = getopt(argc, argv, "cdDu:")) != -1) {
        switch(ch) {
        case 'c':
            npc_chatty = 1;
            break;
        case 'd':
            debug = 1;
            break;
        case 'D':
            opt.Options |= DOKAN_OPTION_DEBUG | DOKAN_OPTION_STDERR;
            break;
        case 'u':
            uname = optarg;
            break;
            
        default:
            usage(prog);
        }
    
    }
    argc -= optind;
    argv += optind;
    if(argc != 2 || !argv[1][0])
        usage(prog);
    serv = argv[0];
    letter = argv[1][0];

    user = np_default_users->uname2user(np_default_users, uname);
    fs = npc_netmount(npc_netaddr(serv, 564), user, 564, NULL, NULL);
    if(!fs) {
        fprintf(stderr, "failed to mount %s\n", serv);
        return 1;
    }

    opt.ThreadCount = 0;
    opt.DriveLetter = letter;
    //opt.Options |= DOKAN_OPTION_KEEP_ALIVE;

    memset(&ops, 0, sizeof ops);
    ops.CreateFile = _CreateFile;
    ops.OpenDirectory = _OpenDirectory;
    ops.CreateDirectory = _CreateDirectory;
    ops.Cleanup = _Cleanup;
    ops.CloseFile = _CloseFile;
    ops.ReadFile = _ReadFile;
    ops.WriteFile = _WriteFile;
    ops.FlushFileBuffers = _FlushFileBuffers;
    ops.GetFileInformation = _GetFileInformation;
    ops.FindFiles = _FindFiles;
    ops.FindFilesWithPattern = NULL;
    ops.SetFileAttributes = _SetFileAttributes;
    ops.SetFileTime = _SetFileTime;
    ops.DeleteFile = _DeleteFile;
    ops.DeleteDirectory = _DeleteDirectory;
    ops.MoveFile = _MoveFile;
    ops.SetEndOfFile = _SetEndOfFile;
    ops.SetAllocationSize = _SetAllocationSize;
    ops.LockFile = _LockFile;
    ops.UnlockFile = _UnlockFile;
    ops.GetDiskFreeSpace = NULL;
    ops.GetVolumeInformation = NULL;
    ops.Unmount = _Unmount;
    x = DokanMain(&opt, &ops);
    if(debug)
        fprintf(stderr, "dokan main: %x\n", x);
    return 0;
}

