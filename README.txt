Ninefs
A 9p filesystem for windows using dokan
Tim Newsham
2009 Nov 5

This is still work in progress and has some rough edges.
It has not yet seen heavy use and may have bugs and may corrupt
your data or crash your system.




BUILDING

To build you will need a microsoft compiler.  I'm using WinDDK
and do my builds using the x86 Checked Build Environment.  Other
compilers will probably work, but if you use WinDDK you can also
build Dokan from sources.  This is how I build:

  - Get dokan, npfs and ninefs sources.  Place all source trees 
    under a common directory.

  - Build npfs for windows

    svn co https://npfs.svn.sourceforge.net/svnroot/npfs/npfs/trunk npfs
    cd npfs
    cd libnpfs; nmake /f ntmakefile
    cd ..\libnpclient; nmake /f ntmakefile
    cd ..\..

  - Build and install dokan according to 
    http://dokan-dev.net/en/docs/how-to-build-dokan-library/

    <fetch http://dokan-dev.net/wp-content/uploads/dokan-0421238src.zip>
    <unzip it, rename "src" to "dokan">
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

  - Build ninefs

    svn co http://XXXX/ninefs/trunk ninefs
    cd ninefs
    build /wcbg
    copy objchk_wxp_x86\i386\ninefs.exe c:\windows\system32
    cd ..

  - Mount something

    ninefs tcp!sources.cs.bell-labs.com s
    dir s:\                                (in another window)

Note: if you place the files in different locations you will
likely have to edit the ninefs/sources file to reflect your
chosen paths.  Likewise if you don't build dokan yourself, you
will need to update the paths to point to the prebuilt dokan.lib.

