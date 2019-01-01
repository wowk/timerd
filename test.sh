#!/bin/bash

TIMES_FILE='/tmp/times'
TIMERCTL='/media/lion/Software/HD_DL/Development/C-C++/timerd/timerctl'

if [ ! -f "$TIMES_FILE" ]; then
    echo -n '5000' > $TIMES_FILE
fi

TIMES=`cat $TIMES_FILE`

echo "Hello: $TIMES" > /tmp/12345

$TIMERCTL -m -n test4 -t $TIMES
let TIMES=$TIMES-1000

if [ "$TIMES" == "0" ]; then
    $TIMERCTL -d -n test4
    rm -f $TIMES_FILE
else
    echo -n $TIMES > $TIMES_FILE
fi


