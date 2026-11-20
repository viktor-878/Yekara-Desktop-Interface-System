#!/bin/sh
grep -r "XtAddCallback( \
    gridContainer, \
    XmNpopupHandlerCallback, \
    container_popup_cb, \
    NULL \
);" ./*
