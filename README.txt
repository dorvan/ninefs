Ninefs
http://code.google.com/p/ninefs
A 9p filesystem for windows using dokan
Tim Newsham
2010 Jan 5

This is still work in progress and has some rough edges.  It has not 
yet seen heavy use and may have bugs and may corrupt your data or 
crash your system.  The code is listed as a BSD license on the google 
code hosting page, but all code is placed in the public domain.



NAME
    ninefs [-cdDtU] [-a authserv] [-p passwd] [-u user] addr driveletter
    dokanctl /u driveletter

DESCRIPTION
    Ninefs mounts a remote 9p resource as a filesystem on the local 
    windows machine.  It takes an address in the form "tcp!hostname!port" 
    or simply "hostname" or "hostname!port" and makes a connection to 
    a 9p server at that address and mounts it as the windows driveletter 
    specified.  After the drive has been mounted it can be unmounted 
    using the dokanctl program.

    The p option specifies a password to use for authentication. If
    unspecified, authentication is not attempted. Authentication makes 
    use of an authentication server.  The a option specifies an address
    for this server in the same format as the addr argument. If ommited,
    addr argument is used in its place except with a different default
    port.

    The U option disables 9P2000.u support.

    The c, d and D options turn on different debug tracing options.  D 
    turns on dokan debugging messages, c turns on chatty npfs messages 
    and d turns on ninefs's own debug messages.

    In normal operation spaces in windows filenames are converted to
    question marks and a best effort is made to convert characters to
    unicode. When the t option is enabled this translation is disabled
    and errors caused during unicode translation cause errors to be
    returned.  If a filename translation cause an error during a directory
    listing, that entry is silently dropped from the listing.

BUGS
    This is an early release and is sure to have many.  In particular,
    error codes are not well mapped and very simplistic rules are used
    to convert between unicode utf16 and utf8 strings.  Many
    important features are missing such as the ability to specify an 
    attach name.

    If the addr argument specifies a port it will not be suitable to
    specify the authentication address. In this case the a option must
    be used even if the authentication server is running on the same
    machine.


SOURCE 
    svn checkout http://ninefs.googlecode.com/svn/trunk/ ninefs

SEE ALSO
    http://code.google.com/p/ninefs
    dokanctl




BINARY INSTALL

The download site has prebuilt binaries:

  http://ninefs.googlecode.com/files/ninefs.exe
  http://ninefs.googlecode.com/files/dokan-binaries.zip

To install and use these:

  - Download and install the latest OpenSSL from
    http://www.slproweb.com/products/Win32OpenSSL.html
    The "Light" version is sufficient.  You may also have to
    install the VC++ 2008 Redistributables likned from this site.

  - unzip dokan-binaries.zip and copy the files into place.
    You will need to be administrator:

    copy *.exe c:\windows\system32
    copy *.dll c:\windows\system32
    copy *.sys c:\windows\system32\drivers

  - install dokan

    dokanctl /i a

  - install ninefs

    copy ninefs.exe c:\windows\system32

  - Mount something and test it out

    ninefs tcp!sources.cs.bell-labs.com s

    (in another window)
    dir s:\plan9\sys\src\9\port
    dokanctl /u s
  



BUILDING

To build you will need a microsoft compiler.  I'm using WinDDK
and do my builds using the x86 Checked Build Environment.  Other
compilers will probably work, but if you use WinDDK you can also
build Dokan from sources.  This is how I build:

  - Get a binary copy of the OpenSSL library and install it.  The
    build files expect it to be in c:\openssl. If it is placed elsewhere
    edit the OPENSSL definition in the "sources" files.

  - Get dokan, npfs and ninefs sources.  Place all source trees 
    under a common directory.

  - Build npfs for windows.  Note, you don't need to make all of the
    directories, just npfs, libnpclient and libnpauth.  If you're
    OpenSSL install is not in c:\openssl you will need to edit
    libnpauth\sources appropriately.

    svn co https://npfs.svn.sourceforge.net/svnroot/npfs/npfs/trunk npfs
    cd npfs
    cd libnpfs; nmake /f ntmakefile
    cd ..\libnpclient; nmake /f ntmakefile
    cd ..\libnpauth; nmake /f ntmakefile
    cd ..\..

  - Build and install dokan according to 
    http://dokan-dev.net/en/docs/how-to-build-dokan-library/

    svn co http://dokan.googlecode.com/svn/trunk dokan
    cd dokan
    cd dokan; build /wcbg
    copy objchk_wxp_x86\i386\dokan.dll c:\windows\system32
    cd ..\dokan_control; build /wcbg
    copy objchk_wxp_x86\i386\dokanctl.exe c:\windows\system32
    cd ..\dokan_mount; build /wcbg
    copy objchk_wxp_x86\i386\mounter.exe c:\windows\system32
    cd ..\sys; build /wcbg
    copy objchk_wxp_x86\i386\dokan.sys c:\windows\system32\drivers
    cd ..\..

    dokanctl /i a

  - Build ninefs.  If your OpenSSL installation is not in c:\openssl
    you will need to edit ninefs\sources appropriately.

    svn checkout http://ninefs.googlecode.com/svn/trunk/ ninefs
    cd ninefs
    build /wcbg
    copy objchk_wxp_x86\i386\ninefs.exe c:\windows\system32
    cd ..

  - Mount something and test it out

    ninefs tcp!sources.cs.bell-labs.com s

    (in another window)
    dir s:\plan9\sys\src\9\port
    dokanctl /u s

Note: if you place the files in different locations you will
likely have to edit the ninefs/sources file to reflect your
chosen paths.  Likewise if you don't build dokan yourself, you
will need to update the paths to point to the prebuilt dokan.lib.

