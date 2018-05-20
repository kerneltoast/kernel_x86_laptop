#!/bin/bash

if [[ $# -eq 0 ]] ; then
	echo 'Pass the kernel $(uname -r) as arg'
	exit 1
fi

KERNEL="$1"

sudo rm -f /boot/*$KERNEL*
sudo rm -rf /lib/modules/$KERNEL
sudo grub-mkconfig -o /boot/grub/grub.cfg
