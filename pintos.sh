#!/bin/bash

root=$(pwd);
init_filesystem=false;
reset_filesystem=false;
#pintos=$root/pintos/src/utils/pintos
#mkdisk=$root/pintos/src/utils/pintos-mkdisk

pintos=pintos
mkdisk=pintos-mkdisk

while [ "$1" != "" ]; do
    case $1 in
        -f | --file )   init_filesystem=true
                        ;;
        -reset )		reset_filesystem=true
						;;
    esac
    shift
done



if test -d ./pintos
then
	#export PATH=$PATH:$root/pintos/src/utils;

	if $reset_filesystem
	then
		rm $root/pintos/src/userprog/build/filesys.dsk
		echo ' *** filesystem removed'
	fi

	if ! test -f $root/pintos/src/userprog/build/filesys.dsk
	then
		echo ' *** filesystem not found.'
		init_filesystem=true;
	fi

	cd $root/pintos/src/userprog
	make

	if $init_filesystem
	then
		cd $root/pintos/src/threads
		make

		cd $root/pintos/src/userprog/build
		echo ' *** try to make filesystem.'
		echo $(pwd)
		
		$mkdisk filesys.dsk --filesys-size=2
		echo ' *** try to format filesystem.'
		pwd
		pintos –f -q

		cd $root/pintos/src/examples
		make

		echo ' *** filesystem & examples initialized'
	fi

	cd $root/pintos/src/userprog/build
	$pintos –p ../../examples/echo –a echo -- -q
	$pintos run "echo x"
else
	echo ' *** error | This is not invalid directory.'
	exit;
fi