TEMPLATE=subdirs

SUBDIRS += \
    client \
    datadevicev1 \
    fullscreenshellv1 \
    iviapplication \
    output \
    primaryselectionv1 \
    seatv4 \
    seatv5 \
    surface \
    wl_connect \
    xdgdecorationv1 \
    xdgoutput \
    xdgshell \
    xdgshellv6

qtConfig(im): SUBDIRS += inputcontext
