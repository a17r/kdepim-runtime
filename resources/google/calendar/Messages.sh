#! /usr/bin/env bash
$EXTRACTRC *.ui *.kcfg >> rc.cpp
$XGETTEXT *.cpp -o $podir/akonadi_googlecalendar_resource.pot
$XGETTEXT ../common/* -j -o $podir/akonadi_googlecalendar_resource.pot
