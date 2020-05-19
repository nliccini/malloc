#!/bin/bash
[[ $(sha512sum my_malloc.h | cut -d " " -f 1) == 2c70ce2fcf910676fa7cf1af5a48cdccc769af4e41b8ab8363528ed7d22c4c6187ca2116b75fb7f15900a76db566dec2a29e5168809e6b83bee7d53f26447b07 ]] || { printf 'error: my_malloc.h was modified! re-download it and try again\n' >&2; exit 1; }
