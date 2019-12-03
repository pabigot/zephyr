#!/bin/bash

(
cat <<EOText
n north 78
nw northwest 77
w west 47
sw southwest 43
EOText
) | while read tag ctrl tot ; do
    cat <<EOText
gpio_${tag}: gpio-${ctrl} {
	/* $tag $ctrl $tot */
	compatible = "intel,apollo-lake-gpio";
	#gpio-cells = <2>;
	gpio-map-mask = <0xffffffff 0xffffffc0>;
	gpio-map-pass-thru = <0 0x3f>;
	gpio-map =
EOText
    n=0
    while [ $n -lt $tot ] ; do
	begin=${n}
        base=$(echo "00$n" | sed -e 's@^.*\(...\)$@\1@')
	span=$(($tot - $n))
	if [ $span -gt 32 ] ; then
	    span=32
	fi
	lim=$(($n + $span - 1))
	end=$(echo "00${lim}" | sed -e 's@^.*\(...\)$@\1@')
	while [ $n -le $lim ] ; do
            offs=$((n - $begin))
	    echo "		<$n 0 &gpio_${tag}_${base}_${end} ${offs} 0>,"
	    n=$(($n + 1))
	done
    done
    cat <<EOText
};

EOText
done
